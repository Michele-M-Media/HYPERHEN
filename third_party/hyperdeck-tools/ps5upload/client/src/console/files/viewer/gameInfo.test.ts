import { describe, expect, it } from "vitest";
import { gameFacts, isParamJsonPath, paramJsonInfo, paramJsonText, parseSfo, sfoInfo } from "./gameInfo";

type SfoValue = { s: string; max?: number } | { u32: number };

/** A param.sfo as Sony lays it out: header, index, key table (4-aligned), data table. */
function sfo(entries: [string, SfoValue][]): Uint8Array {
  const enc = new TextEncoder();
  const keys: number[] = [];
  const keyOffs: number[] = [];
  for (const [k] of entries) {
    keyOffs.push(keys.length);
    keys.push(...enc.encode(k), 0);
  }
  while (keys.length % 4) keys.push(0);
  const data: number[] = [];
  const meta: { fmt: number; len: number; max: number; off: number }[] = [];
  for (const [, v] of entries) {
    const off = data.length;
    if ("u32" in v) {
      data.push(v.u32 & 255, (v.u32 >>> 8) & 255, (v.u32 >>> 16) & 255, (v.u32 >>> 24) & 255);
      meta.push({ fmt: 0x0404, len: 4, max: 4, off });
    } else {
      const b = [...enc.encode(v.s), 0];
      const max = v.max ?? Math.ceil(b.length / 4) * 4;
      data.push(...b);
      while (data.length - off < max) data.push(0);
      meta.push({ fmt: 0x0204, len: b.length, max, off });
    }
  }
  const keyTable = 20 + entries.length * 16;
  const dataTable = keyTable + keys.length;
  const out = new Uint8Array(dataTable + data.length);
  const dv = new DataView(out.buffer);
  out.set([0x00, 0x50, 0x53, 0x46], 0);
  dv.setUint32(4, 0x0101, true);
  dv.setUint32(8, keyTable, true);
  dv.setUint32(12, dataTable, true);
  dv.setUint32(16, entries.length, true);
  meta.forEach((m, i) => {
    const e = 20 + i * 16;
    dv.setUint16(e, keyOffs[i], true);
    dv.setUint16(e + 2, m.fmt, true);
    dv.setUint32(e + 4, m.len, true);
    dv.setUint32(e + 8, m.max, true);
    dv.setUint32(e + 12, m.off, true);
  });
  out.set(keys, keyTable);
  out.set(data, dataTable);
  return out;
}

const PS4 = sfo([
  ["APP_VER", { s: "01.05", max: 8 }],
  ["CATEGORY", { s: "gd", max: 4 }],
  ["CONTENT_ID", { s: "UP9000-CUSA07842_00-JAKXCOMBATRACING", max: 48 }],
  ["PARENTAL_LEVEL", { u32: 5 }],
  ["SYSTEM_VER", { u32: 0x05050000 }],
  ["TITLE", { s: "Jak X: Combat Racing™", max: 128 }],
  ["TITLE_ID", { s: "CUSA07842", max: 12 }],
]);

describe("param.sfo", () => {
  it("reads string and integer keys of a PS4 param.sfo", () => {
    const keys = parseSfo(PS4);
    expect(keys).toMatchObject({
      APP_VER: "01.05",
      CATEGORY: "gd",
      TITLE: "Jak X: Combat Racing™",
      TITLE_ID: "CUSA07842",
      PARENTAL_LEVEL: 5,
      SYSTEM_VER: 0x05050000,
    });
  });

  it("as the banner's facts: title, id, content id, version, firmware", () => {
    expect(sfoInfo(parseSfo(PS4))).toEqual({
      title: "Jak X: Combat Racing™",
      titleId: "CUSA07842",
      contentId: "UP9000-CUSA07842_00-JAKXCOMBATRACING",
      version: "01.05",
      firmware: "5.05",
    });
  });

  it("is not a PSF: null; too short: null", () => {
    expect(parseSfo(new TextEncoder().encode('{"titleId":"PPSA01234"}'))).toBeNull();
    expect(parseSfo(PS4.subarray(0, 19))).toBeNull();
    expect(sfoInfo(null)).toBeNull();
  });

  it("a cut file keeps the keys that are whole and never reads past the end", () => {
    const cut = PS4.subarray(0, PS4.length - 20); // TITLE_ID's data is gone
    const keys = parseSfo(cut);
    expect(keys).not.toBeNull();
    expect(keys?.TITLE_ID).toBeUndefined();
    expect(keys?.APP_VER).toBe("01.05");
  });

  it("damaged offsets and a huge entry count are skipped, not trusted", () => {
    const bad = PS4.slice();
    const dv = new DataView(bad.buffer);
    dv.setUint32(16, 0xffffffff, true); // count
    dv.setUint32(20 + 12, 0x7fffffff, true); // APP_VER's data offset
    const keys = parseSfo(bad);
    expect(keys?.APP_VER).toBeUndefined();
    expect(keys?.TITLE_ID).toBe("CUSA07842");
  });

  it("a key without its NUL within 64 bytes is skipped", () => {
    const long = sfo([["X".repeat(70), { s: "v" }], ["TITLE_ID", { s: "CUSA00001" }]]);
    const keys = parseSfo(long);
    expect(Object.keys(keys ?? {})).toEqual(["TITLE_ID"]);
  });

  it("without TITLE and TITLE_ID it names no game", () => {
    expect(sfoInfo(parseSfo(sfo([["CATEGORY", { s: "gd" }]])))).toBeNull();
  });
});

const PARAM_JSON = {
  applicationCategoryType: 0,
  contentId: "EP9000-PPSA01650_00-ASTROBOT00000000",
  contentVersion: "01.002.000",
  masterVersion: "01.00",
  requiredSystemSoftwareVersion: "0x0960000000000000",
  titleId: "PPSA01650",
  localizedParameters: {
    defaultLanguage: "en-US",
    "de-DE": { titleName: "ASTRO BOT (DE)" },
    "en-US": { titleName: "ASTRO BOT" },
  },
};

describe("param.json", () => {
  it("summarizes title (default language), id, content id, version and firmware", () => {
    expect(paramJsonInfo(PARAM_JSON)).toEqual({
      title: "ASTRO BOT",
      titleId: "PPSA01650",
      contentId: "EP9000-PPSA01650_00-ASTROBOT00000000",
      version: "01.002.000",
      firmware: "9.60",
    });
  });

  it("falls back to any language with a title, and to masterVersion", () => {
    const j = { titleId: "PPSA00001", masterVersion: "01.00", localizedParameters: { "ja-JP": { titleName: "テスト" } } };
    expect(paramJsonInfo(j)).toMatchObject({ title: "テスト", version: "01.00", firmware: null, contentId: null });
  });

  it("text with a BOM parses; not JSON, or not a param.json, is null", () => {
    expect(paramJsonText(`\ufeff${JSON.stringify(PARAM_JSON)}`)?.titleId).toBe("PPSA01650");
    expect(paramJsonText("{oops")).toBeNull();
    expect(paramJsonText('{"name":"x"}')).toBeNull();
    expect(paramJsonText("[1,2]")).toBeNull();
  });

  it("is told by its path", () => {
    expect(isParamJsonPath("/data/game/sce_sys/param.json")).toBe(true);
    expect(isParamJsonPath("/data/game/SCE_SYS/PARAM.JSON")).toBe(true);
    expect(isParamJsonPath("/data/game/param.json")).toBe(false);
  });

  it("facts line: id, version, firmware (missing parts left out)", () => {
    const info = paramJsonInfo(PARAM_JSON);
    expect(info && gameFacts(info)).toBe("PPSA01650 · version 01.002.000 · needs system software 9.60");
    expect(gameFacts({ title: "x", titleId: "CUSA1", contentId: null, version: null, firmware: null })).toBe("CUSA1");
  });
});
