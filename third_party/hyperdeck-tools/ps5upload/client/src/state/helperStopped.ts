import { create } from "zustand";

import { hostOf } from "../lib/addr";
import { safeGetItem, safeSetItem } from "../lib/safeStorage";

/**
 * Consoles whose helper the user stopped ("stopped by you", #471).
 *
 * Every unattended path that would send the helper (the reconnect loop, an upload queue's
 * mid-run recovery, a scheduled or wake-resumed run) leaves a marked console alone: a stop
 * that the next 15-second tick undoes is no stop. The mark goes when the user sends the helper
 * or connects again (Send helper, Check, the setup wizard, Quick bring-up, starting an upload),
 * or when a helper is seen answering well after the stop — someone started one another way,
 * and the console is no longer "stopped".
 *
 * Persisted in localStorage (the desktop WebView keeps it across restarts; the browser build
 * has the same storage), keyed by host without port.
 */

const STORAGE_KEY = "ps5upload.helperStopped";

/** A helper seen answering this long after the stop was started by someone, not left over
 *  from before it: the mark then goes. Longer than any status poll in flight at the stop. */
export const HELPER_STOP_GRACE_MS = 60_000;

const keyOf = (host: string) => (hostOf(host.trim()) || host.trim()).toLowerCase();

function load(): Record<string, number> {
  try {
    const raw = safeGetItem(STORAGE_KEY);
    if (!raw) return {};
    const v = JSON.parse(raw) as unknown;
    if (!v || typeof v !== "object") return {};
    const out: Record<string, number> = {};
    for (const [k, at] of Object.entries(v as Record<string, unknown>)) {
      if (typeof at === "number" && Number.isFinite(at)) out[k] = at;
    }
    return out;
  } catch {
    return {};
  }
}

interface HelperStoppedState {
  /** When each console's helper was stopped (ms since epoch), by host. */
  stoppedAt: Record<string, number>;
}

export const useHelperStoppedStore = create<HelperStoppedState>(() => ({
  stoppedAt: load(),
}));

function write(next: Record<string, number>) {
  useHelperStoppedStore.setState({ stoppedAt: next });
  safeSetItem(STORAGE_KEY, JSON.stringify(next));
}

/** Mark `host` stopped by the user. */
export function markHelperStopped(host: string, now = Date.now()): void {
  const k = keyOf(host);
  if (!k) return;
  write({ ...useHelperStoppedStore.getState().stoppedAt, [k]: now });
}

/** Clear the mark: the user sent the helper or connected again. */
export function clearHelperStopped(host: string): void {
  const k = keyOf(host);
  const cur = useHelperStoppedStore.getState().stoppedAt;
  if (!(k in cur)) return;
  const next = { ...cur };
  delete next[k];
  write(next);
}

/** Whether the user stopped this console's helper (and has not sent it since). */
export function isHelperStopped(host: string): boolean {
  return keyOf(host) in useHelperStoppedStore.getState().stoppedAt;
}

/** The mark, for a component. */
export function useHelperStopped(host: string): boolean {
  const k = keyOf(host);
  return useHelperStoppedStore((s) => k in s.stoppedAt);
}

/** The status poller saw the helper answering: a mark older than the grace period goes (a
 *  helper someone started another way). Returns whether it cleared one. */
export function noteHelperAnswering(host: string, now = Date.now()): boolean {
  const at = useHelperStoppedStore.getState().stoppedAt[keyOf(host)];
  if (at === undefined || now - at < HELPER_STOP_GRACE_MS) return false;
  clearHelperStopped(host);
  return true;
}

/** Test seam. */
export function resetHelperStopped(): void {
  write({});
}
