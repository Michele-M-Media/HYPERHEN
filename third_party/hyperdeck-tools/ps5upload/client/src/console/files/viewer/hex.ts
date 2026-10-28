/**
 * The hex view's model: the first 64 KiB of a file as rows of 16 bytes
 * (offset | hex | ASCII), shown a page at a time.
 */

/** Bytes read for the hex view. */
export const HEX_MAX = 64 * 1024;
export const HEX_ROW = 16;
/** Rows per page (1 KiB): one page fits the TV without scrolling much. */
export const HEX_PAGE_ROWS = 64;
export const HEX_PAGE = HEX_ROW * HEX_PAGE_ROWS;

export interface HexRow {
  /** Offset as 8 hex digits. */
  offset: string;
  /** "00 01 02 03 04 05 06 07  08 09 …" (blank-padded when the row is short). */
  hex: string;
  /** Printable ASCII, "." for anything else. */
  ascii: string;
}

const HEX = "0123456789abcdef";

function hex2(b: number): string {
  return HEX[b >> 4] + HEX[b & 15];
}

/** The row of `bytes` that starts at `offset` (which must be < bytes.length). */
export function hexRow(bytes: Uint8Array, offset: number): HexRow {
  const cells: string[] = [];
  let ascii = "";
  for (let i = 0; i < HEX_ROW; i++) {
    const at = offset + i;
    if (at < bytes.length) {
      const b = bytes[at];
      cells.push(hex2(b));
      ascii += b >= 0x20 && b < 0x7f ? String.fromCharCode(b) : ".";
    } else {
      cells.push("  ");
    }
  }
  const hex = `${cells.slice(0, 8).join(" ")}  ${cells.slice(8).join(" ")}`.replace(/\s+$/, "");
  return { offset: offset.toString(16).padStart(8, "0"), hex, ascii };
}

/** How many pages `length` bytes make (at least 1). */
export function hexPages(length: number): number {
  return Math.max(1, Math.ceil(length / HEX_PAGE));
}

/** Page `page` (0-based, clamped) of `bytes` as rows. */
export function hexPage(bytes: Uint8Array, page: number): HexRow[] {
  const p = Math.min(Math.max(0, page), hexPages(bytes.length) - 1);
  const rows: HexRow[] = [];
  for (let at = p * HEX_PAGE; at < Math.min(bytes.length, (p + 1) * HEX_PAGE); at += HEX_ROW) rows.push(hexRow(bytes, at));
  return rows;
}
