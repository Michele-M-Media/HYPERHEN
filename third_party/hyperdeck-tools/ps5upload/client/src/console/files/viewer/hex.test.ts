import { describe, expect, it } from "vitest";
import { HEX_MAX, HEX_PAGE, hexPage, hexPages, hexRow } from "./hex";

describe("hex view", () => {
  it("a full row: offset, 16 bytes in two groups, ASCII with dots", () => {
    const b = new Uint8Array([0x00, 0x50, 0x53, 0x46, 0x41, 0x7f, 0x20, 0x7e, 0x80, 0xff, 0x0a, 0x30, 0x31, 0x32, 0x33, 0x34]);
    expect(hexRow(b, 0)).toEqual({
      offset: "00000000",
      hex: "00 50 53 46 41 7f 20 7e  80 ff 0a 30 31 32 33 34",
      ascii: ".PSFA. ~...01234",
    });
  });

  it("a short last row has no trailing blanks and a short ASCII column", () => {
    const b = new Uint8Array(20).fill(0x41);
    expect(hexRow(b, 16)).toEqual({ offset: "00000010", hex: "41 41 41 41", ascii: "AAAA" });
  });

  it("offsets past 64 KiB keep 8 digits", () => {
    const b = new Uint8Array(0x10010);
    expect(hexRow(b, 0x10000).offset).toBe("00010000");
  });

  it("pages are 1 KiB (64 rows); a page out of range is clamped", () => {
    const b = new Uint8Array(HEX_MAX);
    expect(HEX_PAGE).toBe(1024);
    expect(hexPages(b.length)).toBe(64);
    expect(hexPages(0)).toBe(1);
    expect(hexPage(b, 0)).toHaveLength(64);
    expect(hexPage(b, 63)[0].offset).toBe("0000fc00");
    expect(hexPage(b, 99)[0].offset).toBe("0000fc00");
    expect(hexPage(b, -1)[0].offset).toBe("00000000");
    expect(hexPage(new Uint8Array(1030), 1)).toHaveLength(1);
    expect(hexPage(new Uint8Array(0), 0)).toEqual([]);
  });
});
