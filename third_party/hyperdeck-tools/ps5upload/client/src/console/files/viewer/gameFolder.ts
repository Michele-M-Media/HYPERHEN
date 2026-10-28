/**
 * The game-folder banner's data: when a folder holds sce_sys/param.json (PS5)
 * or sce_sys/param.sfo (PS4), its title and id, and the icon (sce_sys/icon0.png).
 * Each file is read with one bounded /api/fs/read; nothing is written.
 */
import { ApiError, callBlob } from "../../api";
import { joinPath } from "../move";
import type { Entry } from "../useDir";
import { paramJsonText, parseSfo, sfoInfo, type GameInfo } from "./gameInfo";

/** param.json / param.sfo are read up to this much (a real one is a few KiB). */
export const GAME_META_MAX = 64 * 1024;
/** The icon is shown only when it is at most this big (a PS5 icon0.png is 512x512, well under it). */
export const GAME_ICON_MAX = 4 * 1024 * 1024;

export interface GameFolder {
  info: GameInfo;
  source: "param.json" | "param.sfo";
  /** sce_sys/icon0.png's path (it may not exist). */
  iconPath: string;
}

/** The folder's sce_sys entry (its name as listed: exFAT may spell it SCE_SYS), or null. */
export function sceSysName(entries: Entry[]): string | null {
  const e = entries.find((x) => x.kind === "dir" && x.name.toLowerCase() === "sce_sys");
  return e ? e.name : null;
}

export interface GameFolderDeps {
  /** At most `len` bytes from the start; rejects with ApiError (404 missing). */
  read: (path: string, len: number, signal?: AbortSignal) => Promise<Uint8Array>;
}

const defaultDeps: GameFolderDeps = {
  read: async (path, len, signal) => {
    const blob = await callBlob("/api/fs/read", { path, offset: 0, len }, signal);
    return new Uint8Array(await new Response(blob).arrayBuffer());
  },
};

const missing = (e: unknown) => e instanceof ApiError && (e.status === 404 || e.status === 400);

/**
 * Read `folder`/`sceSys`/param.json, else param.sfo. null: neither is there or
 * neither names a game. Other failures (the helper gone, a 401) reject.
 */
export async function readGameFolder(
  folder: string,
  sceSys: string,
  signal?: AbortSignal,
  deps: GameFolderDeps = defaultDeps,
): Promise<GameFolder | null> {
  const dir = joinPath(folder, sceSys);
  const iconPath = joinPath(dir, "icon0.png");
  try {
    const bytes = await deps.read(joinPath(dir, "param.json"), GAME_META_MAX, signal);
    const info = paramJsonText(new TextDecoder("utf-8", { fatal: false }).decode(bytes));
    if (info) return { info, source: "param.json", iconPath };
  } catch (e) {
    if (!missing(e)) throw e;
  }
  try {
    const bytes = await deps.read(joinPath(dir, "param.sfo"), GAME_META_MAX, signal);
    const info = sfoInfo(parseSfo(bytes));
    if (info) return { info, source: "param.sfo", iconPath };
  } catch (e) {
    if (!missing(e)) throw e;
  }
  return null;
}
