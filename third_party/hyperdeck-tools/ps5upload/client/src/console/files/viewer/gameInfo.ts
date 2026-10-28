/**
 * What a game folder's sce_sys says about it: PS5 param.json, or a PS4 (and
 * old homebrew) param.sfo. Pure parsers; gameFolder.ts reads the files.
 */
import { sdkHexToFw } from "../../../lib/sdkVersionHex";
import { t } from "../../strings";

export interface GameInfo {
  title: string | null;
  titleId: string | null;
  contentId: string | null;
  version: string | null;
  /** The system software the title requires ("9.60"). */
  firmware: string | null;
}

const str = (v: unknown): string | null => (typeof v === "string" && v.trim() ? v.trim() : null);

/** `path` is a game's sce_sys/param.json. */
export function isParamJsonPath(path: string): boolean {
  return /(^|\/)sce_sys\/param\.json$/i.test(path);
}

/**
 * A parsed param.json as GameInfo, or null when it is not one (no titleId and
 * no title). The title is the default language's titleName, else the first
 * language that has one.
 */
export function paramJsonInfo(json: unknown): GameInfo | null {
  if (!json || typeof json !== "object" || Array.isArray(json)) return null;
  const o = json as Record<string, unknown>;
  let title: string | null = null;
  const loc = o.localizedParameters;
  if (loc && typeof loc === "object" && !Array.isArray(loc)) {
    const l = loc as Record<string, unknown>;
    const def = str(l.defaultLanguage);
    const pick = (v: unknown) => (v && typeof v === "object" ? str((v as Record<string, unknown>).titleName) : null);
    if (def) title = pick(l[def]);
    if (!title) {
      for (const k of Object.keys(l)) {
        title = pick(l[k]);
        if (title) break;
      }
    }
  }
  const titleId = str(o.titleId);
  if (!title && !titleId) return null;
  const req = str(o.requiredSystemSoftwareVersion);
  return {
    title,
    titleId,
    contentId: str(o.contentId),
    version: str(o.contentVersion) ?? str(o.masterVersion),
    firmware: req ? sdkHexToFw(req) : null,
  };
}

/** param.json text as GameInfo (null: not JSON, or not a param.json). */
export function paramJsonText(text: string): GameInfo | null {
  try {
    return paramJsonInfo(JSON.parse(text.charCodeAt(0) === 0xfeff ? text.slice(1) : text));
  } catch {
    return null;
  }
}

/** The PSF magic "\0PSF". */
const PSF_MAGIC = [0x00, 0x50, 0x53, 0x46];
/** Entries read at most (a real param.sfo has ~20-60). */
const SFO_MAX_ENTRIES = 512;
/** A key name's longest (Sony's are under 20 bytes). */
const SFO_KEY_MAX = 64;

/**
 * A param.sfo's keys: strings (formats 0x0004 and 0x0204, UTF-8, NULs
 * trimmed) and 32-bit integers (0x0404). null when it is not a PSF. Every
 * offset is checked against the bytes: a cut or damaged file yields the keys
 * that are whole.
 */
export function parseSfo(bytes: Uint8Array): Record<string, string | number> | null {
  if (bytes.length < 20) return null;
  for (let i = 0; i < 4; i++) if (bytes[i] !== PSF_MAGIC[i]) return null;
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const keyTable = dv.getUint32(8, true);
  const dataTable = dv.getUint32(12, true);
  const count = Math.min(dv.getUint32(16, true), SFO_MAX_ENTRIES);
  const out: Record<string, string | number> = {};
  const dec = new TextDecoder("utf-8", { fatal: false });
  for (let i = 0; i < count; i++) {
    const e = 20 + i * 16;
    if (e + 16 > bytes.length) break;
    const keyOff = dv.getUint16(e, true);
    const fmt = dv.getUint16(e + 2, true);
    const len = dv.getUint32(e + 4, true);
    const dataOff = dv.getUint32(e + 12, true);
    const k = keyTable + keyOff;
    const d = dataTable + dataOff;
    if (k >= bytes.length || d > bytes.length || len > bytes.length - d) continue;
    // A key is a short name: look at most SFO_KEY_MAX bytes for its NUL (a damaged table never scans the file).
    const kmax = Math.min(bytes.length, k + SFO_KEY_MAX);
    let ke = k;
    while (ke < kmax && bytes[ke] !== 0) ke++;
    if (ke >= kmax) continue;
    const key = dec.decode(bytes.subarray(k, ke));
    if (!key || Object.prototype.hasOwnProperty.call(out, key)) continue;
    if (fmt === 0x0004 || fmt === 0x0204) {
      out[key] = dec.decode(bytes.subarray(d, d + len)).replace(/\0+$/, "");
    } else if (fmt === 0x0404 && len >= 4) {
      out[key] = dv.getUint32(d, true);
    }
  }
  return out;
}

/** "0x05050000" style for a SYSTEM_VER word. */
function verHex(v: number): string {
  return `0x${(v >>> 0).toString(16).padStart(8, "0")}`;
}

/** A parsed param.sfo as GameInfo (TITLE, TITLE_ID, CONTENT_ID, APP_VER / VERSION, SYSTEM_VER); null without TITLE and TITLE_ID. */
export function sfoInfo(keys: Record<string, string | number> | null): GameInfo | null {
  if (!keys) return null;
  const s = (k: string) => (typeof keys[k] === "string" ? str(keys[k]) : null);
  const title = s("TITLE");
  const titleId = s("TITLE_ID");
  if (!title && !titleId) return null;
  const sys = keys.SYSTEM_VER;
  return {
    title,
    titleId,
    contentId: s("CONTENT_ID"),
    version: s("APP_VER") ?? s("VERSION"),
    firmware: typeof sys === "number" && sys > 0 ? sdkHexToFw(verHex(sys)) : null,
  };
}

/** "PPSA01234 · 01.000.002 · Needs 9.60": what the banner and the param.json summary say under the title. */
export function gameFacts(info: GameInfo): string {
  return [
    info.titleId,
    info.version ? t("cw_files_game_version", { v: info.version }) : null,
    info.firmware ? t("cw_files_game_fw", { v: info.firmware }) : null,
  ]
    .filter(Boolean)
    .join(" · ");
}

