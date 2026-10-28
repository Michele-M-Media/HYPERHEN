// Keeping what ps5upload stored in Payload Manager up to date (Reddit/Discord ask: "sync with
// the payloads on the device"). Payload Manager 0.5 reports no versions and many assets keep one
// file name across releases, so the app remembers, per console, which catalogue payload and
// release it stored under each file name. The Payload Manager tab compares that with the
// catalogue's current release and offers the update. Payloads Payload Manager installed from
// its own repository are its to update and are never touched.

import type { PayloadReleaseInfo } from "../api/ps5";

export interface PmStoreRecord {
  /** Catalogue payload id. */
  id: string;
  /** The release tag that was stored. */
  tag: string;
}

type Records = Record<string, Record<string, PmStoreRecord>>;

const KEY = "ps5upload.pmStored.v1";

function load(): Records {
  try {
    const v = JSON.parse(globalThis.localStorage?.getItem(KEY) ?? "{}");
    return v && typeof v === "object" ? (v as Records) : {};
  } catch {
    return {};
  }
}

function save(r: Records): void {
  try {
    globalThis.localStorage?.setItem(KEY, JSON.stringify(r));
  } catch {
    /* private window or blocked storage: updates are simply not offered */
  }
}

/** What this app stored in `host`'s Payload Manager, by file name. */
export function pmRecords(host: string): Record<string, PmStoreRecord> {
  return load()[host] ?? {};
}

/** Remember that `filename` on `host` is catalogue payload `id` at release `tag`. */
export function recordPmStore(host: string, filename: string, id: string, tag: string): void {
  const all = load();
  all[host] = { ...(all[host] ?? {}), [filename]: { id, tag } };
  save(all);
}

export function forgetPmStore(host: string, filename: string): void {
  const all = load();
  if (!all[host]?.[filename]) return;
  const { [filename]: _gone, ...rest } = all[host];
  void _gone;
  all[host] = rest;
  save(all);
}

/** Stored from Payload Manager's own repository: it updates those itself. */
export function isRepoManaged(p: { path: string; slot: string }): boolean {
  return p.slot.startsWith("repo/") || p.path.includes("/payloads/repo/");
}

/** The release to update to, or null when the stored one is current (or unknown). */
export function pmUpdateFor(
  rec: PmStoreRecord | undefined,
  latest: PayloadReleaseInfo | null | undefined,
): PayloadReleaseInfo | null {
  if (!rec || !latest || !latest.picked_asset_url) return null;
  return latest.tag !== rec.tag ? latest : null;
}

/** `list` with `oldKey` replaced by `newKey` in the same place (pauses kept). When `newKey` is
 *  already listed elsewhere, `oldKey` just goes. */
export function swapAutoloadKey(list: string[], oldKey: string, newKey: string): string[] {
  if (oldKey === newKey || !list.includes(oldKey)) return list;
  if (list.includes(newKey)) return list.filter((e) => e !== oldKey);
  return list.map((e) => (e === oldKey ? newKey : e));
}
