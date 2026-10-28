// The console file manager, the PC side (engine /api/ps5/files/*): the folders pinned on the
// console's own Files page, and the "Show on console" / "Show on PC" handoffs between the TV
// page and this app. An older helper answers {supported:false}: callers then hide the features.
// A plain fetch against the engine origin, so the desktop app and the web UI take the same path.

import { getEngineUrl } from "../state/engine";
import { consoleAddr } from "../lib/addr";

export interface ConsoleFilesPrefs {
  favorites: string[];
  recent: string[];
  last: string | null;
}

/** null: the helper is older and has no shared prefs. */
export type ConsolePrefsAnswer = ConsoleFilesPrefs | null;

export type PrefsOp = "pin" | "unpin" | "visit";
export type HandoffSide = "console" | "pc";

const paths = (v: unknown): string[] =>
  Array.isArray(v) ? v.filter((x): x is string => typeof x === "string" && x.startsWith("/")) : [];

async function engineJson(route: string, init?: RequestInit): Promise<Record<string, unknown>> {
  const res = await fetch(`${getEngineUrl()}${route}`, {
    ...init,
    signal: AbortSignal.timeout(15_000),
  });
  const body = (await res.json().catch(() => ({}))) as Record<string, unknown>;
  if (!res.ok) {
    throw new Error(typeof body.error === "string" && body.error ? body.error : `request failed (${res.status})`);
  }
  return body;
}

const post = (route: string, body: object) =>
  engineJson(route, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });

function toPrefs(b: Record<string, unknown>): ConsolePrefsAnswer {
  if (b.supported !== true) return null;
  return {
    favorites: paths(b.favorites),
    recent: paths(b.recent),
    last: typeof b.last === "string" && b.last.startsWith("/") ? b.last : null,
  };
}

export async function fetchConsolePrefs(host: string): Promise<ConsolePrefsAnswer> {
  const addr = encodeURIComponent(consoleAddr(host));
  return toPrefs(await engineJson(`/api/ps5/files/prefs?addr=${addr}`));
}

export async function changeConsolePrefs(host: string, op: PrefsOp, path: string): Promise<ConsolePrefsAnswer> {
  return toPrefs(await post("/api/ps5/files/prefs/set", { addr: consoleAddr(host), op, path }));
}

/** Leave `path` for the console page to open. False: the helper is older (no handoffs). */
export async function handoffToConsole(host: string, path: string): Promise<boolean> {
  const b = await post("/api/ps5/files/handoff/put", { addr: consoleAddr(host), to: "console", path });
  return b.supported === true;
}

export type HandoffTake =
  | { supported: false }
  | { supported: true; path: string | null; ageMs: number };

/** Take the handoff the console page left for this app (`path` null: none waiting). */
export async function takeHandoffForPc(host: string): Promise<HandoffTake> {
  const b = await post("/api/ps5/files/handoff/take", { addr: consoleAddr(host), for: "pc" });
  if (b.supported !== true) return { supported: false };
  const path = typeof b.path === "string" && b.path.startsWith("/") ? b.path : null;
  return { supported: true, path, ageMs: typeof b.age_ms === "number" ? b.age_ms : 0 };
}

/** A path as the helper stores it: '//' collapsed, no trailing '/' (except "/"). */
export function cleanConsolePath(p: string): string {
  const c = p.replace(/\/{2,}/g, "/");
  return c.length > 1 ? c.replace(/\/$/, "") : c;
}
