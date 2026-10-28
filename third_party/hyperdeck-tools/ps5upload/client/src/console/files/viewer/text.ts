/**
 * The text viewer's model: the file's first 1 MiB as numbered rows, find
 * (case-insensitive, next / previous, wrapping), and JSON pretty printing.
 * Pure: the component (TextView.tsx) only renders what these return.
 */

/** A line longer than this is shown as several rows (numbered once): a 1 MiB minified file stays drawable. */
export const ROW_CHARS = 2000;
/** Rows drawn at first; "Show more lines" adds this many again (find extends it to reach a match). */
export const ROW_WINDOW = 1500;
/** Rows per focusable block (↑/↓ steps block by block). */
export const BLOCK_ROWS = 30;
/** Matches counted at most (the rest are not found). */
export const FIND_MAX = 10_000;

export interface Row {
  /** 1-based line number. */
  line: number;
  /** The row continues the line above (no number shown). */
  cont: boolean;
  text: string;
}

export interface Match {
  /** Index into the rows. */
  row: number;
  start: number;
  end: number;
}

/** `text` as rows: lines split at \n (a \r before it dropped), long lines cut every ROW_CHARS. */
export function toRows(text: string, rowChars: number = ROW_CHARS): Row[] {
  const rows: Row[] = [];
  const lines = text.split("\n");
  // A file ending in a newline has no empty last line.
  if (lines.length > 1 && lines[lines.length - 1] === "") lines.pop();
  for (let i = 0; i < lines.length; i++) {
    let line = lines[i];
    if (line.endsWith("\r")) line = line.slice(0, -1);
    if (line.length <= rowChars) {
      rows.push({ line: i + 1, cont: false, text: line });
      continue;
    }
    for (let at = 0; at < line.length; at += rowChars) {
      rows.push({ line: i + 1, cont: at > 0, text: line.slice(at, at + rowChars) });
    }
  }
  return rows;
}

/**
 * Every place `query` occurs in the rows, case-insensitively, in order (at most
 * `max`; `capped` then). A row whose lower-case form changes length (a few
 * Unicode letters) is searched as written, so offsets always point into it.
 */
export function findMatches(rows: Row[], query: string, max: number = FIND_MAX): { matches: Match[]; capped: boolean } {
  const matches: Match[] = [];
  if (!query) return { matches, capped: false };
  const lq = query.toLowerCase();
  for (let r = 0; r < rows.length; r++) {
    const raw = rows[r].text;
    const low = raw.toLowerCase();
    const hay = low.length === raw.length ? low : raw;
    const needle = low.length === raw.length ? lq : query;
    if (!needle.length) continue;
    let from = 0;
    for (;;) {
      const at = hay.indexOf(needle, from);
      if (at < 0) break;
      if (matches.length >= max) return { matches, capped: true };
      matches.push({ row: r, start: at, end: at + needle.length });
      from = at + needle.length;
    }
  }
  return { matches, capped: false };
}

/** The match after (dir 1) or before (dir -1) `current`, wrapping around; -1 when there are none. */
export function stepMatch(current: number, count: number, dir: 1 | -1): number {
  if (count <= 0) return -1;
  if (current < 0 || current >= count) return dir === 1 ? 0 : count - 1;
  return (current + dir + count) % count;
}

/** The first match at or after row `row` (where the reader is), else the first one; -1 when none. */
export function firstMatchFrom(matches: Match[], row: number): number {
  if (!matches.length) return -1;
  const i = matches.findIndex((m) => m.row >= row);
  return i < 0 ? 0 : i;
}

/** The rows drawn: [start, end). */
export interface RowWindow {
  start: number;
  end: number;
}

/**
 * The rows to draw so row `row` is among them: the window as it is when it
 * already holds it, else a fresh ROW_WINDOW-sized one around it (starting on a
 * block boundary), so a find jump far down never draws everything above it.
 */
export function windowAround(row: number, win: RowWindow, total: number, size: number = ROW_WINDOW): RowWindow {
  if (row >= win.start && row < win.end) return win;
  const start = Math.max(0, Math.floor((row - size / 2) / BLOCK_ROWS) * BLOCK_ROWS);
  return { start, end: Math.min(total, start + size) };
}

/** A row's text cut around its matches: [text, matchIndex | -1] pieces, in order. */
export function highlight(text: string, rowMatches: { start: number; end: number; index: number }[]): [string, number][] {
  const out: [string, number][] = [];
  let at = 0;
  for (const m of rowMatches) {
    if (m.start < at) continue;
    if (m.start > at) out.push([text.slice(at, m.start), -1]);
    out.push([text.slice(m.start, m.end), m.index]);
    at = m.end;
  }
  if (at < text.length || !out.length) out.push([text.slice(at), -1]);
  return out;
}

/** A .json file by its name. */
export function isJsonName(name: string): boolean {
  return /\.json$/i.test(name);
}

/**
 * `text` pretty printed (two spaces) when it is JSON, else as it is (`ok`
 * false: a parse error, or the file was cut at 1 MiB). A leading BOM is ignored.
 */
export function prettyJson(text: string): { text: string; ok: boolean } {
  const body = text.charCodeAt(0) === 0xfeff ? text.slice(1) : text;
  try {
    return { text: JSON.stringify(JSON.parse(body), null, 2), ok: true };
  } catch {
    return { text, ok: false };
  }
}
