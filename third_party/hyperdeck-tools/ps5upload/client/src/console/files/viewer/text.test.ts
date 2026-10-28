import { describe, expect, it } from "vitest";
import {
  findMatches,
  firstMatchFrom,
  highlight,
  isJsonName,
  prettyJson,
  ROW_WINDOW,
  stepMatch,
  toRows,
  windowAround,
} from "./text";

describe("text rows", () => {
  it("numbers lines, drops \\r and the empty line after a final newline", () => {
    const rows = toRows("one\r\ntwo\n\nfour\n");
    expect(rows.map((r) => [r.line, r.text, r.cont])).toEqual([
      [1, "one", false],
      [2, "two", false],
      [3, "", false],
      [4, "four", false],
    ]);
  });

  it("cuts a long line into rows numbered once", () => {
    const rows = toRows("abcdefghij\nx", 4);
    expect(rows.map((r) => [r.line, r.cont, r.text])).toEqual([
      [1, false, "abcd"],
      [1, true, "efgh"],
      [1, true, "ij"],
      [2, false, "x"],
    ]);
  });

  it("an empty file is one empty line", () => {
    expect(toRows("")).toEqual([{ line: 1, cont: false, text: "" }]);
  });
});

describe("find", () => {
  const rows = toRows("Alpha beta\nBETA gamma beta\nnothing");

  it("finds every place, case-insensitively, in order", () => {
    const { matches, capped } = findMatches(rows, "beta");
    expect(capped).toBe(false);
    expect(matches).toEqual([
      { row: 0, start: 6, end: 10 },
      { row: 1, start: 0, end: 4 },
      { row: 1, start: 11, end: 15 },
    ]);
  });

  it("an empty query finds nothing; a missing word finds nothing", () => {
    expect(findMatches(rows, "").matches).toEqual([]);
    expect(findMatches(rows, "delta").matches).toEqual([]);
  });

  it("stops counting at the cap", () => {
    const many = toRows("a".repeat(50));
    const r = findMatches(many, "a", 10);
    expect(r.matches).toHaveLength(10);
    expect(r.capped).toBe(true);
  });

  it("matches do not overlap", () => {
    expect(findMatches(toRows("aaaa"), "aa").matches.map((m) => m.start)).toEqual([0, 2]);
  });

  it("a row whose lower case changes length is searched as written (offsets stay in the row)", () => {
    // "İ" lower-cases to two code units: the row is searched as it is.
    const r = findMatches(toRows("İstanbul Ankara"), "Ankara");
    expect(r.matches).toEqual([{ row: 0, start: 9, end: 15 }]);
  });

  it("next / previous wrap around; none is -1", () => {
    expect(stepMatch(-1, 3, 1)).toBe(0);
    expect(stepMatch(-1, 3, -1)).toBe(2);
    expect(stepMatch(2, 3, 1)).toBe(0);
    expect(stepMatch(0, 3, -1)).toBe(2);
    expect(stepMatch(1, 3, 1)).toBe(2);
    expect(stepMatch(0, 0, 1)).toBe(-1);
  });

  it("starts at the first match at or after the reader's row", () => {
    const m = findMatches(rows, "beta").matches;
    expect(firstMatchFrom(m, 1)).toBe(1);
    expect(firstMatchFrom(m, 5)).toBe(0);
    expect(firstMatchFrom([], 0)).toBe(-1);
  });

  it("a find jump draws a bounded window around the match, never everything above it", () => {
    const w0 = { start: 0, end: ROW_WINDOW };
    expect(windowAround(10, w0, 50_000)).toBe(w0); // already drawn: unchanged
    const far = windowAround(40_000, w0, 50_000);
    expect(far.end - far.start).toBe(ROW_WINDOW);
    expect(far.start).toBeLessThanOrEqual(40_000);
    expect(far.end).toBeGreaterThan(40_000);
    expect(far.start % 30).toBe(0); // starts on a block
    const near0 = windowAround(100, { start: 30_000, end: 31_500 }, 50_000);
    expect(near0.start).toBe(0);
    const end = windowAround(49_999, w0, 50_000);
    expect(end.end).toBe(50_000);
    expect(end.start).toBeLessThanOrEqual(49_999);
  });

  it("cuts a row around its matches", () => {
    expect(
      highlight("Alpha beta beta", [
        { start: 6, end: 10, index: 0 },
        { start: 11, end: 15, index: 1 },
      ]),
    ).toEqual([
      ["Alpha ", -1],
      ["beta", 0],
      [" ", -1],
      ["beta", 1],
    ]);
    expect(highlight("", [])).toEqual([["", -1]]);
  });
});

describe("JSON", () => {
  it("is pretty printed; a BOM is ignored", () => {
    expect(prettyJson('\ufeff{"a":[1,2],"b":{"c":"d"}}')).toEqual({ text: '{\n  "a": [\n    1,\n    2\n  ],\n  "b": {\n    "c": "d"\n  }\n}', ok: true });
  });

  it("a parse error (or a file cut at 1 MiB) shows as written", () => {
    expect(prettyJson('{"a": 1,')).toEqual({ text: '{"a": 1,', ok: false });
  });

  it("is told by its name", () => {
    expect(isJsonName("param.JSON")).toBe(true);
    expect(isJsonName("notes.txt")).toBe(false);
  });
});
