/**
 * Tied into the PC app: "Show on TV" (the PC's File System left a folder for
 * this page: App polls for it and opens Files there), "Show on PC" (the
 * actions sheet leaves a folder for the PC app, which polls the helper through
 * its engine), and the "Uploading" badge on folders a PC is uploading into.
 * Handoffs live in helper memory for two minutes (payload/src/files_handoff.c).
 */
import { useEffect } from "react";
import { MonitorUp } from "lucide-react";
import { ApiError, call } from "../api";
import type { Message } from "../home/Home";
import type { SectionParams } from "../sections";
import { t } from "../strings";
import { pollWhileVisible } from "../ui/visiblePoll";

/** How often the page asks for a handoff from the PC (while it is visible). */
export const HANDOFF_POLL_MS = 3000;

/** The actions sheet's "Show on PC" item (ActionsSheet's ITEMS entry). */
export const SHOW_ON_PC_ITEM = { icon: MonitorUp, key: "cw_files_show_pc" };

/**
 * Whether the helper has handoffs: unknown until the first poll answers, then
 * true, or false for good on a 404 (a helper without the routes). "Show on PC"
 * is offered only when true.
 */
let supported: boolean | null = null;

export function canShowOnPc(): boolean {
  return supported === true;
}

/** Tests: forget what the last helper said. */
export function resetHandoffSupport(value: boolean | null = null): void {
  supported = value;
}

/** Takes the folder the PC left for this page: its path, or null when none is waiting. */
export async function takeHandoff(): Promise<string | null> {
  try {
    const r = await call<{ path?: unknown }>("/api/files/handoff/take", { for: "console" });
    supported = true;
    return typeof r?.path === "string" && r.path.startsWith("/") ? r.path : null;
  } catch (e) {
    if (e instanceof ApiError && e.status === 404) supported = false;
    throw e;
  }
}

const parentOf = (p: string) => {
  const i = p.lastIndexOf("/");
  return i <= 0 ? "/" : p.slice(0, i);
};

/**
 * Where Files opens for a handoff: the folder itself, or a file's folder with
 * the file focused. A path that is gone opens no folder but says so.
 */
export async function handoffTarget(path: string): Promise<SectionParams> {
  try {
    const st = await call<{ kind?: unknown }>("/api/fs/stat", { path });
    if (st?.kind === "dir") return { path, handoff: "pc" };
    return { path: parentOf(path), focus: path.slice(path.lastIndexOf("/") + 1), handoff: "pc" };
  } catch (e) {
    if (e instanceof ApiError && e.status === 404) return { handoff_gone: path };
    // Any other refusal: open it, and Files says what is wrong with the folder.
    return { path, handoff: "pc" };
  }
}

/** The line Files shows for the handoff it was opened with, or null. */
export function handoffMessage(params: SectionParams | null): Message | null {
  if (!params) return null;
  if (params.handoff_gone) return { tone: "info", text: t("cw_files_from_pc_gone", { path: params.handoff_gone }) };
  if (params.handoff === "pc") return { tone: "info", text: t("cw_files_from_pc") };
  return null;
}

/** Files: show the handoff's line once per opening. */
export function useHandoffNotice(params: SectionParams | null, show: (m: Message) => void): void {
  useEffect(() => {
    const m = handoffMessage(params);
    if (m) show(m);
  }, [params, show]);
}

/**
 * One check: take the PC's handoff and, when there is one, open Files at its
 * target. True when something was opened.
 */
export async function checkHandoff(open: (params: SectionParams) => void, live: () => boolean = () => true): Promise<boolean> {
  const path = await takeHandoff();
  if (!path || !live()) return false;
  const target = await handoffTarget(path);
  if (!live()) return false;
  open(target);
  return true;
}

/**
 * App: while the page is visible, ask every few seconds whether the PC left a
 * folder, and open Files there. Stops on a helper without handoffs.
 */
export function useHandoffPoll(open: (params: SectionParams) => void, enabled = true): void {
  useEffect(() => {
    if (!enabled) return;
    let live = true;
    let busy = false;
    let stop = () => {};
    const tick = () => {
      if (busy || supported === false) return;
      busy = true;
      checkHandoff(open, () => live)
        .catch(() => {
          if (supported === false) stop();
        })
        .finally(() => {
          busy = false;
        });
    };
    stop = pollWhileVisible(tick, HANDOFF_POLL_MS);
    return () => {
      live = false;
      stop();
    };
  }, [open, enabled]);
}

/** "Show on PC": leave `path` for the PC app. The line to show either way. */
export async function showOnPc(path: string): Promise<Message> {
  try {
    await call("/api/files/handoff/put", { to: "pc", path });
    return { tone: "info", text: t("cw_files_show_pc_done") };
  } catch {
    return { tone: "error", text: t("cw_files_show_pc_failed") };
  }
}

/**
 * The entries of folder `dir` that a running upload is writing into: an upload's
 * root is the entry itself or lies inside it.
 */
export function uploadingNames(dir: string | null, roots: readonly string[]): Set<string> {
  const out = new Set<string>();
  if (dir == null || !roots.length) return out;
  const base = dir === "/" ? "/" : `${dir.replace(/\/+$/, "")}/`;
  for (const r of roots) {
    if (!r.startsWith(base)) continue;
    const rest = r.slice(base.length);
    const name = rest.split("/")[0];
    if (name) out.add(name);
  }
  return out;
}
