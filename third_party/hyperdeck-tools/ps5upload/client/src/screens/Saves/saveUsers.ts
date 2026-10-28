// Which console user a save belongs to, by name, and where that user's USB backups live.
//
// Saves sit under /user/home/<uid hex>/…; the helper reports `user_id` as a signed 32-bit
// number and the profile list (`profileInfo`) names every local user. On a shared console the
// Save data screen shows one user's saves at a time and labels them by name.
//
// USB backups used to be `<base>/<title_id>/<timestamp>/<title_id>.zip` for every user, so
// "Restore all from USB" on a shared console could put one user's latest backup into another
// user's save of the same game. New backups go to `<base>/<title_id>/<name>-<uid hex>/<timestamp>/`.
import type { ProfileUser, SaveEntry } from "../../api/ps5";

/** The 8-digit lowercase hex of a user id, as in /user/home/<hex>. */
export function userHex(id: number): string {
  return (id >>> 0).toString(16).padStart(8, "0");
}

/** The local user's name for `id`, or null when the console didn't name it. */
export function userName(id: number, users: readonly ProfileUser[]): string | null {
  const hex = userHex(id);
  const u = users.find((x) => userHex(x.uid) === hex);
  const name = u?.username.trim();
  return name ? name : null;
}

/** The users that own at least one save, named where possible, in first-seen order. */
export function saveOwners(
  saves: readonly SaveEntry[],
  users: readonly ProfileUser[],
): Array<{ id: number; name: string | null }> {
  const seen = new Map<string, { id: number; name: string | null }>();
  for (const s of saves) {
    const hex = userHex(s.user_id);
    if (!seen.has(hex)) seen.set(hex, { id: s.user_id, name: userName(s.user_id, users) });
  }
  return [...seen.values()];
}

/** "all", or the hex of the user whose saves are shown. */
export type UserFilter = "all" | string;

/** The starting filter: the user at the console when they own saves, else every user. */
export function defaultUserFilter(saves: readonly SaveEntry[], foregroundUid: number): UserFilter {
  if (!foregroundUid) return "all";
  const hex = userHex(foregroundUid);
  return saves.some((s) => userHex(s.user_id) === hex) ? hex : "all";
}

export function filterSaves(saves: readonly SaveEntry[], filter: UserFilter): SaveEntry[] {
  return filter === "all" ? [...saves] : saves.filter((s) => userHex(s.user_id) === filter);
}

/** A name made safe for a file or folder name on exFAT/FAT32 and every host OS. */
export function safeName(name: string): string {
  const visible = Array.from(name, (c) => (c.charCodeAt(0) < 32 ? "_" : c)).join("");
  return visible
    .replace(/[\\/:*?"<>|]/g, "_")
    .replace(/^[.\s]+|[.\s]+$/g, "")
    .replace(/\s+/g, " ")
    .slice(0, 40)
    .trim();
}

/** This user's backup folder name under `<base>/<title_id>/`. */
export function usbUserDir(id: number, name: string | null): string {
  const safe = name ? safeName(name) : "";
  return safe ? `${safe}-${userHex(id)}` : userHex(id);
}

/** The default .zip name for a backup on this computer: `PPSA01234-Game Name-User[-stamp].zip`. */
export function backupZipName(
  titleId: string,
  name: string | null,
  stamp?: string,
  gameName?: string | null,
): string {
  const who = name ? safeName(name) : "";
  const game = gameName ? safeName(gameName.replace(/[™®©]/g, "")).slice(0, 60).trim() : "";
  return [titleId, game, who, stamp].filter(Boolean).join("-") + ".zip";
}

const TIMESTAMP = /^\d{4}-\d{2}-\d{2}_\d{6}$/;

/**
 * The newest USB backup of a title for one user, as a path relative to `<base>/<title_id>/`.
 *
 * `listDir(rel)` lists folder names under `<base>/<title_id>/<rel>` ("" = the title folder).
 * The user's own folder wins (matched by uid, so a renamed user still finds it). Backups from
 * before per-user folders (timestamps straight under the title) are used only when
 * `onlyUserOfTitle` — this user is the only one with a save of the title on the console — since
 * nothing says whose they are.
 */
export async function latestUsbBackup(
  listDir: (rel: string) => Promise<string[]>,
  id: number,
  onlyUserOfTitle: boolean,
): Promise<string | null> {
  const hex = userHex(id);
  const top = await listDir("");
  const mine = top.find((n) => n.toLowerCase() === hex || n.toLowerCase().endsWith(`-${hex}`));
  if (mine) {
    const stamps = (await listDir(mine)).filter((n) => TIMESTAMP.test(n)).sort();
    if (stamps.length) return `${mine}/${stamps[stamps.length - 1]}`;
  }
  if (!onlyUserOfTitle) return null;
  const legacy = top.filter((n) => TIMESTAMP.test(n)).sort();
  return legacy.length ? legacy[legacy.length - 1] : null;
}
