/**
 * "PS5Upload Minion was updated": the page shown is older than the helper's.
 * That happens when the browser served its offline copy (the appcache serves
 * its copy first and fetches the new page behind it: "updateready"), or when
 * the helper was updated under an open page. Either way the page offers a
 * one-press Reload (UpdateBanner) instead of reloading by itself mid-task.
 */
import { useSyncExternalStore } from "react";

/** The version this page was built from ("" under tests: never compared). */
export const PAGE_VERSION: string = typeof __CW_APP_V__ === "string" ? __CW_APP_V__ : "";

let ready = false;
const listeners = new Set<() => void>();

/** A newer page is waiting: show the banner (once). */
export function markUpdateReady(): void {
  if (ready) return;
  ready = true;
  listeners.forEach((fn) => fn());
}

/** The helper's version (/api/settings) against this page's; a difference means a newer page. */
export function isNewerHelper(helperVersion: unknown, page: string = PAGE_VERSION): boolean {
  return !!page && typeof helperVersion === "string" && !!helperVersion && helperVersion !== page;
}

/** Later: hide the banner for this page's life. */
export function dismissUpdate(): void {
  if (!ready) return;
  ready = false;
  listeners.forEach((fn) => fn());
}

function subscribe(fn: () => void): () => void {
  listeners.add(fn);
  return () => {
    listeners.delete(fn);
  };
}

export const useUpdateReady = (): boolean => useSyncExternalStore(subscribe, () => ready, () => ready);

/** Tests. */
export function _resetUpdateForTests(): void {
  ready = false;
}
