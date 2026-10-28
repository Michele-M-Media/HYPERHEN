/**
 * The permissions editor's model: owner / group / other × read / write / run
 * as bits of a mode, its octal text, and whether a drive keeps them at all.
 * Pure: PermissionsDialog only renders what these return.
 */

export type PermWho = "owner" | "group" | "other";
export type PermBit = "read" | "write" | "exec";

export const PERM_WHO: PermWho[] = ["owner", "group", "other"];
export const PERM_BITS: PermBit[] = ["read", "write", "exec"];

const SHIFT: Record<PermWho, number> = { owner: 6, group: 3, other: 0 };
const BIT: Record<PermBit, number> = { read: 4, write: 2, exec: 1 };

/** The mask of one toggle. */
export function permMask(who: PermWho, bit: PermBit): number {
  return BIT[bit] << SHIFT[who];
}

export function hasPerm(mode: number, who: PermWho, bit: PermBit): boolean {
  return (mode & permMask(who, bit)) !== 0;
}

/** `mode` with one toggle flipped (only the 0777 bits are ever kept). */
export function togglePerm(mode: number, who: PermWho, bit: PermBit): number {
  return (mode ^ permMask(who, bit)) & 0o777;
}

/** "0755". */
export function modeText(mode: number): string {
  return (mode & 0o777).toString(8).padStart(4, "0");
}

/** "rwxr-xr-x". */
export function modeLetters(mode: number): string {
  let s = "";
  for (const who of PERM_WHO) for (const bit of PERM_BITS) s += hasPerm(mode, who, bit) ? "rwx"[PERM_BITS.indexOf(bit)] : "-";
  return s;
}

/** Typed octal ("755", "0644") as a mode, or null. */
export function parseMode(text: string): number | null {
  const v = text.trim();
  if (!/^[0-7]{1,4}$/.test(v)) return null;
  const n = parseInt(v, 8);
  return n > 0o777 ? null : n;
}

/**
 * Does a drive of this file system keep permissions? exFAT and FAT (USB
 * drives, exFAT images) have none: every file shows the same mode and chmod
 * changes nothing that lasts.
 */
export function keepsPermissions(fsType: string | null | undefined): boolean {
  if (!fsType) return true;
  return !/^(exfat|msdos|msdosfs|vfat|fat|fat16|fat32)$/i.test(fsType);
}
