import { appListRunning, type AppDbEntry, type AppLifecycleAck } from "../../api/ps5";
import { fetchRunningGames, type RunningGame } from "../../lib/runningGames";
import type { HostProbe } from "../../lib/staleHostGuard";
import { useRunningAppsStore } from "../../state/runningApps";

export interface RunningJoined {
  app_id: number;
  /** Resolved from app.db (or the process list) when found; null when the
   *  running app isn't known there (rare — system processes, installers). */
  title_id: string | null;
  name: string | null;
}

export interface RunningAppsDeps {
  appListRunning: (addr: string) => Promise<AppLifecycleAck>;
  fetchRunningGames: (addr: string) => Promise<Map<string, RunningGame>>;
}

const realDeps: RunningAppsDeps = { appListRunning, fetchRunningGames };

/**
 * One read of the running apps, joined to their titles.
 *
 * Titles come from app.db when `appdb` is given and answers (desktop), else
 * from the process list (the browser build has no app.db read). `titleIds` is
 * null when neither answered: "we don't know what is running" must not be
 * published as "nothing is running", or every Playing badge and Close game
 * button disappears on one failed read.
 */
export async function readRunningApps(
  addr: string,
  appdb: (() => Promise<AppDbEntry[]>) | null,
  deps: RunningAppsDeps = realDeps,
): Promise<{ rows: RunningJoined[]; titleIds: string[] | null }> {
  const [running, db] = await Promise.all([
    deps.appListRunning(addr),
    appdb ? appdb().catch(() => null) : Promise.resolve(null),
  ]);
  const byAppId = new Map<number, { title_id: string; name: string | null }>();
  let titleIds: string[] | null = null;
  if (db) {
    for (const e of db) byAppId.set(e.app_id, { title_id: e.title_id, name: e.name });
  } else {
    const games = await deps.fetchRunningGames(addr).catch(() => null);
    if (games) {
      for (const g of games.values()) {
        if (g.appId) byAppId.set(g.appId, { title_id: g.titleId, name: null });
      }
      titleIds = Array.from(games.keys());
    }
  }
  const rows: RunningJoined[] = (running.apps ?? []).map((r) => {
    const hit = byAppId.get(r.app_id);
    return { app_id: r.app_id, title_id: hit?.title_id ?? null, name: hit?.name ?? null };
  });
  if (db) titleIds = rows.map((j) => j.title_id).filter((t): t is string => !!t);
  return { rows, titleIds };
}

/**
 * The panel's refresh: read, then show and publish only while the console the
 * read was for is still the selected one. A slow reply for console A landing
 * after a switch to B would otherwise show A's rows, whose Kill/Suspend
 * buttons act on B (app ids are small and collide across consoles).
 */
export async function refreshRunningApps(opts: {
  addr: string;
  probe: HostProbe;
  appdb: (() => Promise<AppDbEntry[]>) | null;
  onRows: (rows: RunningJoined[]) => void;
  deps?: RunningAppsDeps;
}): Promise<void> {
  const r = await readRunningApps(opts.addr, opts.appdb, opts.deps);
  if (opts.probe.isStale()) return;
  opts.onRows(r.rows);
  // Tag with the address so a console switch can clear data tied to another PS5.
  if (r.titleIds) useRunningAppsStore.getState().setRunning(r.titleIds, opts.addr);
}
