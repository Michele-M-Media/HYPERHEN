/**
 * The helper's jobs, every owner's (the PC app's uploads and copies as well as
 * the page's own), read from /api/jobs: about once a second while one runs,
 * every five seconds otherwise. The poller stops when the section unmounts and
 * for good on a 401 (the token is gone; the reload banner takes over).
 */
import { useCallback, useEffect, useRef, useState } from "react";
import { ApiError, call } from "../api";

/** One /api/jobs entry (payload/src/webui_codec.c webui_jobs_to_json). */
export interface Job {
  /** 32 lowercase hex chars. */
  job: string;
  kind: "upload" | "download" | "copy" | "op" | "other";
  state: "running" | "done" | "failed";
  files_done: number;
  files_total: number;
  bytes_done: number;
  bytes_total: number;
  /** Started from this page (false: a paired PC's job). */
  mine: boolean;
}

export const JOBS_FAST_MS = 1000;
export const JOBS_SLOW_MS = 5000;

/** How long until the next read after `jobs`. */
export function nextJobsDelay(jobs: Job[]): number {
  return jobs.some((j) => j.state === "running") ? JOBS_FAST_MS : JOBS_SLOW_MS;
}

/**
 * The Tasks list: every running job whoever started it, but a finished,
 * failed or cancelled one only when this page started it (the PC app reports
 * its own outcomes). Running ones first. Polling still watches all jobs.
 */
export function visibleJobs(jobs: Job[]): Job[] {
  const running = jobs.filter((j) => j.state === "running");
  return running.concat(jobs.filter((j) => j.state !== "running" && j.mine));
}

/** One read of /api/jobs: the jobs, and where running uploads (a PC's) are writing. */
export async function fetchJobsAll(): Promise<{ jobs: Job[]; uploading: string[] }> {
  const r = await call<{ jobs?: unknown; uploading?: unknown }>("/api/jobs");
  const uploading = Array.isArray(r?.uploading)
    ? r.uploading.filter((x): x is string => typeof x === "string" && x.startsWith("/"))
    : [];
  return { jobs: Array.isArray(r?.jobs) ? (r.jobs as Job[]) : [], uploading };
}

/** One read of /api/jobs. */
export async function fetchJobs(): Promise<Job[]> {
  return (await fetchJobsAll()).jobs;
}

/** Ask the helper to stop job `id` (any owner's). */
export async function cancelJob(id: string): Promise<void> {
  await call("/api/jobs/cancel", { job: id });
}

export interface JobsPoller {
  start(): void;
  stop(): void;
  /** Read now (after a cancel), never two reads at once. */
  refresh(): void;
}

/** The polling loop behind useJobs, with no React in it. */
export function createJobsPoller(opts: {
  onJobs: (jobs: Job[], uploading: string[]) => void;
  onError: (err: unknown) => void;
}): JobsPoller {
  let timer: ReturnType<typeof setTimeout> | undefined;
  let stopped = false;
  let inFlight = false;
  let again = false;

  const schedule = (ms: number) => {
    if (stopped) return;
    if (timer !== undefined) clearTimeout(timer);
    timer = setTimeout(tick, ms);
  };

  async function tick(): Promise<void> {
    timer = undefined;
    if (stopped) return;
    if (inFlight) {
      again = true;
      return;
    }
    inFlight = true;
    let delay = JOBS_SLOW_MS;
    try {
      const { jobs, uploading } = await fetchJobsAll();
      if (stopped) return;
      opts.onJobs(jobs, uploading);
      delay = nextJobsDelay(jobs);
    } catch (e) {
      if (stopped) return;
      opts.onError(e);
      if (e instanceof ApiError && e.status === 401) {
        stopped = true;
        return;
      }
    } finally {
      inFlight = false;
    }
    if (again) {
      again = false;
      delay = 0;
    }
    schedule(delay);
  }

  return {
    start() {
      schedule(0);
    },
    stop() {
      stopped = true;
      if (timer !== undefined) clearTimeout(timer);
      timer = undefined;
    },
    refresh() {
      if (stopped) return;
      if (inFlight) again = true;
      else schedule(0);
    },
  };
}

export interface JobsState {
  jobs: Job[];
  /** Where running uploads (a PC's) are writing: Files badges those folders. */
  uploading: string[];
  /** The first answer has come in. */
  loaded: boolean;
  /** The last read failed (null after a good one). */
  error: unknown;
  /** Stop job `id`; throws the ApiError when the helper refuses. Re-reads either way. */
  cancel(id: string): Promise<void>;
  /** Read the jobs again now (Create = refresh). */
  refresh(): void;
}

export function useJobs(): JobsState {
  const [jobs, setJobs] = useState<Job[]>([]);
  const [uploading, setUploading] = useState<string[]>([]);
  const [loaded, setLoaded] = useState(false);
  const [error, setError] = useState<unknown>(null);
  const poller = useRef<JobsPoller | null>(null);

  useEffect(() => {
    const p = createJobsPoller({
      onJobs: (j, u) => {
        setJobs(j);
        setUploading((prev) => (prev.length === u.length && prev.every((x, i) => x === u[i]) ? prev : u));
        setLoaded(true);
        setError(null);
      },
      onError: setError,
    });
    poller.current = p;
    p.start();
    return () => {
      p.stop();
      if (poller.current === p) poller.current = null;
    };
  }, []);

  const cancel = useCallback(async (id: string) => {
    try {
      await cancelJob(id);
    } finally {
      poller.current?.refresh();
    }
  }, []);

  const refresh = useCallback(() => {
    poller.current?.refresh();
  }, []);

  return { jobs, uploading, loaded, error, cancel, refresh };
}
