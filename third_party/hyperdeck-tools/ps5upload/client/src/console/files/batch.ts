/**
 * The page's copy/move/delete batch: one at a time, page-wide.
 *
 * A batch sends one item, waits until that item's job has left "running"
 * (read from /api/jobs, the list Home and Files poll), then sends the next:
 * the helper's job table holds 32 jobs for every owner (paired PCs too), and a
 * 40-item copy must not take it over. Stop ends the batch before its next item
 * (the job already running carries on; Home can cancel it).
 *
 * The batch lives outside any component, so it keeps going while another
 * section is open and Files shows its progress again when it comes back. Only
 * one batch runs at a time.
 */
import { useEffect, useState } from "react";
import { ApiError } from "../api";
import { fetchJobs, type Job } from "../home/useJobs";
import type { BatchResult, JobEnd } from "./move";

export type BatchOp = "copy" | "move" | "delete" | "chmod" | "rename" | "unpack";

/** The batch running now: item `at` (1-based) of `total` is being sent or waited on. */
export interface BatchProgress {
  op: BatchOp;
  at: number;
  total: number;
  /** Stop was asked: no further item is sent. */
  stopping: boolean;
  /** What it reads and writes: its items and (copy/move) the destination folder. */
  paths?: string[];
  /** Bytes written so far of `bytesTotal` (a one-job batch such as unpack reports them). */
  bytesDone?: number;
  bytesTotal?: number;
}

/** How often a running job is checked. */
export const BATCH_POLL_MS = 1000;
/** A job missing from this many reads in a row has ended (released from the list). */
export const BATCH_ABSENT_READS = 2;

export interface WaiterDeps {
  fetchJobs: () => Promise<Job[]>;
  sleep: (ms: number) => Promise<void>;
  pollMs?: number;
}

/**
 * Wait for job `id` to leave "running": "done" / "failed" from the list, "gone"
 * when it is no longer listed, "left" when `stopped()` turns true first. A read
 * that fails is retried; a 401 (the token is gone) is thrown.
 */
export function createJobWaiter(deps: WaiterDeps) {
  const pollMs = deps.pollMs ?? BATCH_POLL_MS;
  return async (id: string, stopped?: () => boolean): Promise<JobEnd> => {
    let absent = 0;
    for (;;) {
      let jobs: Job[] | null = null;
      try {
        jobs = await deps.fetchJobs();
      } catch (e) {
        if (e instanceof ApiError && e.status === 401) throw e;
      }
      if (jobs) {
        const j = jobs.find((x) => x.job === id);
        if (j) {
          absent = 0;
          if (j.state !== "running") return j.state;
        } else if (++absent >= BATCH_ABSENT_READS) {
          return "gone";
        }
      }
      if (stopped?.()) return "left";
      await deps.sleep(pollMs);
      if (stopped?.()) return "left";
    }
  };
}

export const waitJob = createJobWaiter({
  fetchJobs,
  sleep: (ms) => new Promise((res) => setTimeout(res, ms)),
});

/* ---- the one batch ---- */

let progress: BatchProgress | null = null;
let stopAsked = false;
const listeners = new Set<() => void>();

function emit(): void {
  listeners.forEach((fn) => fn());
}

/** The running batch, or null. */
export function currentBatch(): BatchProgress | null {
  return progress;
}

/** Hear every change of currentBatch(). Returns the unsubscribe function. */
export function onBatchChange(fn: () => void): () => void {
  listeners.add(fn);
  return () => {
    listeners.delete(fn);
  };
}

/** Stop the running batch before its next item. */
export function stopBatch(): void {
  if (!progress || progress.stopping) return;
  stopAsked = true;
  progress = { ...progress, stopping: true };
  emit();
}

export interface BatchControl {
  onProgress: (at: number, total: number) => void;
  /** Bytes done of total for the running item (shown with a percentage). */
  onBytes: (done: number, total: number) => void;
  stopped: () => boolean;
}

/**
 * Run `work` as the page's batch. Null (nothing started) while another batch
 * runs. The batch is over when the promise settles.
 */
export function startBatch<R extends BatchResult = BatchResult>(
  op: BatchOp,
  total: number,
  work: (ctl: BatchControl) => Promise<R>,
  paths?: string[],
): Promise<R> | null {
  if (progress) return null;
  stopAsked = false;
  progress = { op, at: 0, total, stopping: false, ...(paths ? { paths } : {}) };
  emit();
  const ctl: BatchControl = {
    onProgress: (at, n) => {
      if (!progress) return;
      progress = { ...progress, at, total: n };
      emit();
    },
    onBytes: (done, total) => {
      if (!progress) return;
      if (progress.bytesDone === done && progress.bytesTotal === total) return;
      progress = { ...progress, bytesDone: done, bytesTotal: total };
      emit();
    },
    stopped: () => stopAsked,
  };
  let run: Promise<R>;
  try {
    run = work(ctl);
  } catch (e) {
    run = Promise.reject(e);
  }
  return run.finally(() => {
    progress = null;
    stopAsked = false;
    emit();
  });
}

/** currentBatch() as React state. */
export function useBatch(): BatchProgress | null {
  const [b, setB] = useState(currentBatch);
  useEffect(() => {
    setB(currentBatch());
    return onBatchChange(() => setB(currentBatch()));
  }, []);
  return b;
}
