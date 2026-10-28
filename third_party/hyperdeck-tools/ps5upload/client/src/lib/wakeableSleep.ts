// A sleep the desktop/Android host can end (#457).
//
// A hidden window throttles setTimeout (Chromium clamps it to once a minute after
// a while; a hidden WKWebView can be suspended), so a queue waiting 500 ms between
// status polls could sit for a minute while the app is in the background. The host
// watcher (src-tauri/src/bg_activity.rs) sends `bg-tick` every second while work
// runs and `bg-job-finished` when an engine job ends; lib/bgWork.ts forwards those
// here. A tick ends every sleep whose time is up, so the host's clock stands in for
// the throttled timer; a finished job also ends the sleeps marked `poll`, so the
// next status poll sees the ending at once. Nothing ever ends early otherwise: a
// settle delay between jobs still waits its full time.

interface Sleeper {
  due: number;
  poll: boolean;
  done: () => void;
}

const sleepers = new Set<Sleeper>();

/** Resolve after `ms`, or sooner when the host says so (see the module comment).
 *  `poll`: this sleep only spaces out status polls, so a job ending may cut it short. */
export function sleep(ms: number, opts?: { poll?: boolean }): Promise<void> {
  return new Promise((resolve) => {
    const s: Sleeper = {
      due: Date.now() + ms,
      poll: opts?.poll ?? false,
      done: () => {
        sleepers.delete(s);
        clearTimeout(timer);
        resolve();
      },
    };
    const timer = setTimeout(s.done, ms);
    sleepers.add(s);
  });
}

/** End every sleep whose time is up (`bg-tick`), and with `polls` every poll sleep too
 *  (`bg-job-finished`). */
export function wakeSleepers(opts?: { polls?: boolean }, now = Date.now()): void {
  for (const s of [...sleepers]) {
    if (s.due <= now || (opts?.polls && s.poll)) s.done();
  }
}
