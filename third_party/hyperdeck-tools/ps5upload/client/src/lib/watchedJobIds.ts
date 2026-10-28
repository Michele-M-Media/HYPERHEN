/**
 * Engine jobs this tab is following, by when it last read each one's status.
 *
 * Every screen that follows a job polls it through `jobStatus` (Files
 * downloads and uploads, Convert and image builds, RAR, link downloads, the
 * queues), so recording the read there claims the job for the whole tab
 * without each of those places registering itself. The running-jobs banner
 * uses it to tell a job nobody is watching (a closed tab's) from one this tab
 * is already showing elsewhere.
 */

/** A job read within this window counts as watched. The slowest follower
 *  (a lost-contact poll) reads every 5 s. */
export const WATCHED_FRESH_MS = 15_000;

const lastRead = new Map<string, number>();

export function noteJobWatched(jobId: string, now: number = Date.now()): void {
  if (jobId) lastRead.set(jobId, now);
}

/** The ids read within WATCHED_FRESH_MS of `now`; older entries are forgotten. */
export function watchedJobIds(now: number = Date.now()): Set<string> {
  const out = new Set<string>();
  for (const [id, at] of lastRead) {
    if (now - at <= WATCHED_FRESH_MS) out.add(id);
    else lastRead.delete(id);
  }
  return out;
}

/** Tests only. */
export function resetWatchedJobIds(): void {
  lastRead.clear();
}
