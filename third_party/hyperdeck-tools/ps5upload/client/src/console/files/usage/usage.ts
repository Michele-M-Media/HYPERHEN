/**
 * Disk usage: how much each subfolder of a folder holds, walked with
 * /api/fs/list paging (folderSize.ts's walker, one subfolder after another).
 * Bounded for the whole measurement (USAGE_MAX_ENTRIES entries, USAGE_MAX_MS),
 * cancellable between pages, and one at a time page-wide: it refuses to start
 * while another measurement or a "Calculate folder size" runs.
 */
import { ApiError, call } from "../../api";
import { holdWalk, walkFolder, type ListPage, type WalkDeps } from "../folderSize";
import { joinPath } from "../move";
import { LIST_PAGE } from "../useDir";

export const USAGE_MAX_ENTRIES = 500_000;
export const USAGE_MAX_MS = 180_000;
/** Subfolders measured at most (the rest are listed as not measured). */
export const USAGE_MAX_FOLDERS = 2000;

export type RowState = "waiting" | "walking" | "done" | "partial" | "unreadable" | "skipped";

export interface UsageRow {
  /** A subfolder's name; "" for the files directly in the folder. */
  name: string;
  path: string;
  files: boolean;
  bytes: number;
  /** Entries counted inside it. */
  entries: number;
  state: RowState;
}

/** How a measurement ended (null while it runs). */
export type UsageEnd = "done" | "capped" | "timeout" | "cancelled" | "error" | null;

export interface UsageSnapshot {
  root: string;
  rows: UsageRow[];
  /** Subfolders measured so far, and how many there are. */
  measured: number;
  folders: number;
  entries: number;
  end: UsageEnd;
  error?: unknown;
}

export interface UsageLimits {
  maxEntries?: number;
  maxMs?: number;
  maxFolders?: number;
}

const defaultDeps: WalkDeps = {
  list: (path, offset, limit) => call<ListPage>("/api/fs/list", { path, offset, limit }),
  now: () => Date.now(),
};

/** Biggest first; rows still waiting (or skipped) after the measured ones, by name. */
export function sortRows(rows: UsageRow[]): UsageRow[] {
  const rank = (r: UsageRow) => (r.state === "waiting" || r.state === "skipped" ? 1 : 0);
  return rows.slice().sort((a, b) => rank(a) - rank(b) || b.bytes - a.bytes || a.name.localeCompare(b.name));
}

/** A row's bar, 0-100, against the biggest row. */
export function barPercent(bytes: number, max: number): number {
  if (!(max > 0) || !(bytes > 0)) return 0;
  return Math.max(1, Math.round((bytes / max) * 100));
}

/** Measure `root` (see the module comment). Never throws: a failure of `root` itself is end "error". */
export async function measureUsage(
  root: string,
  opts: { cancelled: () => boolean; onUpdate: (s: UsageSnapshot) => void } & UsageLimits,
  deps: WalkDeps = defaultDeps,
): Promise<UsageSnapshot> {
  const maxEntries = opts.maxEntries ?? USAGE_MAX_ENTRIES;
  const maxMs = opts.maxMs ?? USAGE_MAX_MS;
  const maxFolders = opts.maxFolders ?? USAGE_MAX_FOLDERS;
  const started = deps.now();
  const here: UsageRow = { name: "", path: root, files: true, bytes: 0, entries: 0, state: "walking" };
  const subs: UsageRow[] = [];
  let entries = 0;
  const snap = (end: UsageEnd, error?: unknown): UsageSnapshot => {
    const s: UsageSnapshot = {
      root,
      rows: [here, ...subs].map((r) => ({ ...r })),
      measured: subs.filter((r) => r.state !== "waiting" && r.state !== "walking").length,
      folders: subs.length,
      entries,
      end,
    };
    if (error !== undefined) s.error = error;
    return s;
  };
  const finish = (end: Exclude<UsageEnd, null>, error?: unknown) => {
    for (const r of subs) if (r.state === "waiting" || r.state === "walking") r.state = "skipped";
    if (here.state === "walking") here.state = end === "done" ? "done" : "partial";
    const s = snap(end, error);
    opts.onUpdate(s);
    return s;
  };

  // 1. The folder itself: its files add up here, its subfolders become rows.
  let offset = 0;
  let extra = 0;
  for (;;) {
    if (opts.cancelled()) return finish("cancelled");
    if (deps.now() - started > maxMs) return finish("timeout");
    let page: ListPage;
    try {
      page = await deps.list(root, offset, LIST_PAGE);
    } catch (e) {
      return finish("error", e);
    }
    if (opts.cancelled()) return finish("cancelled");
    const list = Array.isArray(page?.entries) ? page.entries : [];
    for (const e of list) {
      entries++;
      if (e.kind === "dir") {
        if (subs.length < maxFolders) {
          subs.push({ name: e.name, path: joinPath(root, e.name), files: false, bytes: 0, entries: 0, state: "waiting" });
        } else extra++;
      } else if (e.kind === "file" && typeof e.size === "number" && e.size > 0) {
        here.bytes += e.size;
        here.entries++;
      } else here.entries++;
      if (entries >= maxEntries) return finish("capped");
    }
    const next = typeof page?.next === "number" ? page.next : offset + list.length;
    if (!page?.more || next <= offset) break;
    offset = next;
  }
  here.state = extra ? "partial" : "done";
  opts.onUpdate(snap(null));

  // 2. Each subfolder, one after another, out of what is left of the budget.
  for (const row of subs) {
    if (opts.cancelled()) return finish("cancelled");
    const left = maxEntries - entries;
    const msLeft = maxMs - (deps.now() - started);
    if (left <= 0) return finish("capped");
    if (msLeft <= 0) return finish("timeout");
    row.state = "walking";
    opts.onUpdate(snap(null));
    const r = await walkFolder(
      row.path,
      {
        cancelled: opts.cancelled,
        maxEntries: left,
        maxMs: msLeft,
        onProgress: (c) => {
          row.bytes = c.bytes;
          row.entries = c.entries;
          opts.onUpdate(snap(null));
        },
      },
      deps,
    );
    row.bytes = r.bytes;
    row.entries = r.entries;
    entries += r.entries;
    if (r.end === "error") {
      // The token is gone: stop; another failure of this one folder: it is unreadable, go on.
      if (r.error instanceof ApiError && r.error.status === 401) return finish("error", r.error);
      row.state = "unreadable";
      continue;
    }
    if (r.end === "cancelled") {
      row.state = "partial";
      return finish("cancelled");
    }
    if (r.end === "capped" || r.end === "timeout") {
      row.state = "partial";
      return finish(r.end);
    }
    row.state = r.unreadable ? "partial" : "done";
    opts.onUpdate(snap(null));
  }
  return finish(extra ? "capped" : "done");
}

/* ---- one at a time ---- */

let running: { cancel: () => void; done: Promise<UsageSnapshot> } | null = null;

/** A disk usage measurement runs now. */
export function usageRunning(): boolean {
  return running != null;
}

/**
 * Start the page's one measurement of `root`: null while another (or a folder
 * size walk) runs. `cancel()` stops it before its next page.
 */
export function startUsage(
  root: string,
  onUpdate: (s: UsageSnapshot) => void,
  deps?: WalkDeps,
  limits?: UsageLimits,
): { done: Promise<UsageSnapshot>; cancel: () => void } | null {
  if (running) return null;
  // One walk page-wide: Disk usage and "Calculate folder size" never run together.
  const release = holdWalk();
  if (!release) return null;
  let cancelled = false;
  const cancel = () => {
    cancelled = true;
  };
  const done = measureUsage(root, { cancelled: () => cancelled, onUpdate, ...limits }, deps).finally(() => {
    release();
    if (running?.done === done) running = null;
  });
  running = { cancel, done };
  return { done, cancel };
}

/** Stop the running measurement (if any) and wait until it has ended. */
export async function stopUsage(): Promise<void> {
  const r = running;
  if (!r) return;
  r.cancel();
  await r.done.catch(() => undefined);
}
