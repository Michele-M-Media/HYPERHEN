// Export and import of payload playlists (Discord ask: "export & import the list, custom repos
// included, so I can give my friends a stable autoload configuration").
//
// One JSON document: the playlists, which of them the auto-loader runs, and the custom repos the
// catalogue knows. Repo steps name a payload by id; a custom repo's id is derived from its
// host/owner/name, so re-adding the repo on another computer gives the same id and the imported
// steps resolve with no remapping.

import type { PayloadInfo } from "../api/ps5";
import type { AutoLoaderConfig, Playlist, PlaylistStep } from "./playlistOps";

export const PLAYLIST_EXPORT_FORMAT = "ps5upload-playlists";

/** A custom repo, as `payloadsAddCustomRepo` takes it. */
export interface SharedRepo {
  id: string;
  host: string;
  owner: string;
  name: string;
  displayName: string;
}

export interface PlaylistExport {
  format: typeof PLAYLIST_EXPORT_FORMAT;
  version: 1;
  exportedAt: number;
  playlists: Playlist[];
  autoLoader?: AutoLoaderConfig;
  customRepos: SharedRepo[];
}

/** "https://git.example.org/o/n" → "git.example.org"; github.com for anything unparsable. */
export function repoHostOf(p: Pick<PayloadInfo, "homepage">): string {
  try {
    return new URL(p.homepage).host || "github.com";
  } catch {
    return "github.com";
  }
}

export function buildPlaylistExport(
  playlists: Playlist[],
  autoLoader: AutoLoaderConfig | undefined,
  catalog: PayloadInfo[],
  now: number,
): PlaylistExport {
  return {
    format: PLAYLIST_EXPORT_FORMAT,
    version: 1,
    exportedAt: now,
    // When it last ran here says nothing on another computer.
    playlists: playlists.map(({ lastRunAt: _lastRun, ...p }) => ({ ...p })),
    autoLoader,
    customRepos: catalog
      .filter((p) => p.is_custom)
      .map((p) => ({
        id: p.id,
        host: repoHostOf(p),
        owner: p.repo_owner,
        name: p.repo_name,
        displayName: p.display_name,
      })),
  };
}

const str = (v: unknown): string => (typeof v === "string" ? v : "");
const num = (v: unknown): number => (typeof v === "number" && Number.isFinite(v) ? v : 0);

function cleanStep(v: unknown): PlaylistStep | null {
  if (!v || typeof v !== "object") return null;
  const s = v as Record<string, unknown>;
  const step: PlaylistStep = { path: str(s.path), sleepMs: Math.max(0, num(s.sleepMs)) };
  if (str(s.payloadId)) step.payloadId = str(s.payloadId);
  if (str(s.payloadLabel)) step.payloadLabel = str(s.payloadLabel);
  if (str(s.ip)) step.ip = str(s.ip);
  if (num(s.port) > 0) step.port = num(s.port);
  return step.path || step.payloadId ? step : null;
}

/** The file's document, every field checked: throws on anything that is not an export. */
export function parsePlaylistExport(text: string): PlaylistExport {
  let v: unknown;
  try {
    v = JSON.parse(text);
  } catch {
    throw new Error("not a ps5upload playlists file (not JSON)");
  }
  const d = v as Record<string, unknown> | null;
  if (!d || d.format !== PLAYLIST_EXPORT_FORMAT || !Array.isArray(d.playlists)) {
    throw new Error("not a ps5upload playlists file");
  }
  const playlists: Playlist[] = [];
  for (const raw of d.playlists) {
    if (!raw || typeof raw !== "object") continue;
    const p = raw as Record<string, unknown>;
    const steps = Array.isArray(p.steps) ? p.steps.map(cleanStep).filter((s): s is PlaylistStep => !!s) : [];
    if (!str(p.id) || !str(p.name)) continue;
    playlists.push({
      id: str(p.id),
      name: str(p.name),
      steps,
      continueOnFailure: p.continueOnFailure === true,
      createdAt: num(p.createdAt),
      updatedAt: num(p.updatedAt),
    });
  }
  const a = d.autoLoader as Record<string, unknown> | undefined;
  const repos = Array.isArray(d.customRepos) ? d.customRepos : [];
  return {
    format: PLAYLIST_EXPORT_FORMAT,
    version: 1,
    exportedAt: num(d.exportedAt),
    playlists,
    autoLoader:
      a && typeof a === "object"
        ? {
            enabled: a.enabled === true,
            playlistId: str(a.playlistId) || null,
            bringUpPlaylistId: str(a.bringUpPlaylistId) || null,
          }
        : undefined,
    customRepos: repos
      .map((r) => r as Record<string, unknown>)
      .filter((r) => r && str(r.owner) && str(r.name))
      .map((r) => ({
        id: str(r.id),
        host: str(r.host) || "github.com",
        owner: str(r.owner),
        name: str(r.name),
        displayName: str(r.displayName),
      })),
  };
}

/** The imported playlists with fresh ids (so nothing here is replaced) and names that do not
 *  repeat one already here; `idMap` takes an imported id to its new one. */
export function importedPlaylists(
  existing: Playlist[],
  incoming: Playlist[],
  newId: () => string,
  now: number,
): { playlists: Playlist[]; idMap: Map<string, string> } {
  const names = new Set(existing.map((p) => p.name));
  const idMap = new Map<string, string>();
  const playlists = incoming.map((p) => {
    let name = p.name;
    for (let n = 2; names.has(name); n++) name = `${p.name} (${n})`;
    names.add(name);
    const id = newId();
    idMap.set(p.id, id);
    return { ...p, id, name, createdAt: now, updatedAt: now };
  });
  return { playlists, idMap };
}
