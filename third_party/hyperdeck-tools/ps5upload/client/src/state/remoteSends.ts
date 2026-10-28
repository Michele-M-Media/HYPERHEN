import { create } from "zustand";

import { fetchRemoteSends, type RemoteSend } from "../api/ava1";
import { hostOf } from "../lib/addr";
import type { AddQueueItem } from "./uploadQueue";

/**
 * Console-to-console sends other devices started (#444). The device that starts a send keeps a
 * small record of it on the receiving console; while Tasks is open this polls every console in
 * the roster for those records, so a send started from a phone can be watched, taken over or
 * resumed from here.
 */
export const REMOTE_POLL_MS = 15_000;

export interface ConsoleSends {
  sends: RemoteSend[];
  /** The console's helper shares transfers with every paired device; null = not known. */
  sharedJobs: boolean | null;
  error: string | null;
}

export type RemoteSendAction = "watch" | "take" | "resume";

/** A send as the Tasks section shows it: with the console it was found on (this app's address). */
export type ShownSend = RemoteSend & { console: string };

interface RemoteSendsState {
  byConsole: Record<string, ConsoleSends>;
  refresh: (hosts: string[]) => Promise<void>;
  /** Polls `hosts()` now and every REMOTE_POLL_MS until the returned stop is called. Callers
   *  share one poll. */
  watch: (hosts: () => string[]) => () => void;
}

let watchers = 0;
let timer: ReturnType<typeof setInterval> | null = null;
let hostsOf: () => string[] = () => [];

export const useRemoteSends = create<RemoteSendsState>((set, get) => ({
  byConsole: {},
  async refresh(hosts) {
    const unique = [...new Set(hosts.map(hostOf).filter(Boolean))];
    const results = await Promise.all(
      unique.map(async (h): Promise<[string, ConsoleSends]> => {
        try {
          const r = await fetchRemoteSends(h);
          return [h, { sends: r.sends, sharedJobs: r.sharedJobs, error: null }];
        } catch (e) {
          // An offline console (or an older engine) lists nothing; keep what it last said.
          const prev = get().byConsole[h];
          return [
            h,
            {
              sends: prev?.sends ?? [],
              sharedJobs: prev?.sharedJobs ?? null,
              error: e instanceof Error ? e.message : String(e),
            },
          ];
        }
      }),
    );
    set({ byConsole: Object.fromEntries(results) });
  },
  watch(hosts) {
    hostsOf = hosts;
    watchers += 1;
    if (watchers === 1) {
      void get().refresh(hostsOf());
      timer = setInterval(() => void get().refresh(hostsOf()), REMOTE_POLL_MS);
    }
    let stopped = false;
    return () => {
      if (stopped) return;
      stopped = true;
      watchers -= 1;
      if (watchers === 0 && timer) {
        clearInterval(timer);
        timer = null;
      }
    };
  },
}));

/** Other devices' sends: none this app runs or has in its queue (by job id), newest first. */
export function visibleRemoteSends(
  byConsole: Record<string, ConsoleSends>,
  queueTxIds: ReadonlySet<string>,
): ShownSend[] {
  const seen = new Set<string>();
  const out: ShownSend[] = [];
  for (const [console, c] of Object.entries(byConsole)) {
    for (const s of c.sends) {
      if (s.cls === "mine" || queueTxIds.has(s.txId) || seen.has(s.txId)) continue;
      seen.add(s.txId);
      out.push({ ...s, console });
    }
  }
  return out.sort((a, b) => b.updatedMs - a.updatedMs);
}

/** What the row offers. A relay's bytes run through the other device: it cannot be watched. */
export function actionsFor(s: RemoteSend): RemoteSendAction[] {
  switch (s.cls) {
    case "running_elsewhere":
      return [
        ...(s.canWatch ? (["watch"] as const) : []),
        ...(s.canTakeOver ? (["take"] as const) : []),
      ];
    case "paused":
    case "interrupted":
      return ["resume"];
    default:
      return [];
  }
}

function baseName(p: string): string {
  const t = p.replace(/\/+$/, "");
  return t.slice(t.lastIndexOf("/") + 1) || t;
}

/** The queue item that adopts `s` under its own job id: on the console it was found on, from
 *  the sending console as the starting device named it (or this app's roster entry for it). */
export function adoptItem(
  s: ShownSend,
  action: RemoteSendAction,
  rosterHosts: readonly string[] = [],
): AddQueueItem {
  const from = hostOf(s.from);
  return {
    sourceKind: "ps5",
    fromConsole: rosterHosts.map(hostOf).find((h) => h === from) ?? from,
    route: s.route,
    directAddr: s.directAddr,
    sourcePath: s.src,
    displayName: baseName(s.src),
    resolvedDest: s.dest,
    addr: s.console,
    strategy: "overwrite",
    reconcileMode: "fast",
    excludes: [],
    mountAfterUpload: false,
    mountReadOnly: true,
    registerAfterUpload: false,
    estimatedBytes: s.bytesTotal,
    txIdHex: s.txId,
    adopt: action === "watch" ? "watch" : "take",
  };
}
