import type { JobSnapshot } from "../api/ps5";
import { LOST_POLL_INTERVAL_MS, MAX_POLL_FAILURES } from "./transfer";

const POLL_MS = 500;

export interface FsDownloadPollHooks {
  /** False once the user stopped this download or started another one. */
  isLive: () => boolean;
  onProgress: (p: { bytesReceived: number; totalBytes: number }) => void;
  /** The engine stopped answering (an error message), or answers again (null). */
  onLostContact: (error: string | null) => void;
  onDone: () => void;
  onFailed: (error: string) => void;
}

export interface FsDownloadPollDeps {
  jobStatus: (jobId: string) => Promise<JobSnapshot>;
  sleep?: (ms: number) => Promise<void>;
}

/**
 * Follow a Files download's engine job until it ends.
 *
 * A failed status read is not a failed download: the engine keeps pulling the
 * file whether or not this tab can see it. Like the upload poller
 * (state/transfer.ts) it counts failures in a row, reports "lost contact"
 * after MAX_POLL_FAILURES and keeps checking at a slower pace, so the banner
 * (and its Cancel) stays up for as long as the job may be running. Only an
 * engine that says it has no such job (it restarted) ends the download here.
 */
export async function pollFsDownload(
  jobId: string,
  hooks: FsDownloadPollHooks,
  deps: FsDownloadPollDeps,
): Promise<void> {
  const sleep = deps.sleep ?? ((ms: number) => new Promise<void>((r) => setTimeout(r, ms)));
  let failures = 0;
  while (true) {
    if (!hooks.isLive()) return;
    let snap: JobSnapshot;
    try {
      snap = await deps.jobStatus(jobId);
    } catch (e) {
      if (!hooks.isLive()) return;
      const msg = e instanceof Error ? e.message : String(e);
      if (/job not found/i.test(msg)) {
        hooks.onFailed(msg);
        return;
      }
      failures++;
      if (failures >= MAX_POLL_FAILURES) hooks.onLostContact(msg);
      await sleep(failures >= MAX_POLL_FAILURES ? LOST_POLL_INTERVAL_MS : POLL_MS);
      continue;
    }
    if (!hooks.isLive()) return;
    if (failures >= MAX_POLL_FAILURES) hooks.onLostContact(null);
    failures = 0;
    if (snap.status === "done") {
      hooks.onDone();
      return;
    }
    if (snap.status === "failed") {
      hooks.onFailed(snap.error ?? "download failed");
      return;
    }
    hooks.onProgress({ bytesReceived: snap.bytes_sent ?? 0, totalBytes: snap.total_bytes ?? 0 });
    await sleep(POLL_MS);
  }
}
