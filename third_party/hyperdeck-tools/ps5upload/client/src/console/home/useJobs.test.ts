import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

vi.mock("../api", async (importOriginal) => {
  const real = await importOriginal<typeof import("../api")>();
  return { ...real, call: vi.fn() };
});

import { ApiError, call } from "../api";
import { cancelJob, createJobsPoller, JOBS_FAST_MS, JOBS_SLOW_MS, nextJobsDelay, type Job } from "./useJobs";

const mockCall = vi.mocked(call);

const job = (state: Job["state"], over: Partial<Job> = {}): Job => ({
  job: "0123456789abcdef0123456789abcdef",
  kind: "copy",
  state,
  files_done: 1,
  files_total: 4,
  bytes_done: 100,
  bytes_total: 400,
  mine: true,
  ...over,
});

const jobsCalls = () => mockCall.mock.calls.filter((c) => c[0] === "/api/jobs").length;

describe("useJobs poller", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    mockCall.mockReset();
  });
  afterEach(() => {
    vi.useRealTimers();
  });

  it("polls_fast_while_running_then_slow", async () => {
    const replies = [[job("running")], [job("done")], [job("done")]];
    mockCall.mockImplementation(async () => ({ jobs: replies.shift() ?? [] }));
    const seen: Job[][] = [];
    const p = createJobsPoller({
      onJobs: (j) => seen.push(j),
      onError: () => {},
    });
    p.start();
    await vi.advanceTimersByTimeAsync(0);
    expect(jobsCalls()).toBe(1);
    expect(seen[0][0].state).toBe("running");

    // something runs: the next read comes after JOBS_FAST_MS
    await vi.advanceTimersByTimeAsync(JOBS_FAST_MS - 1);
    expect(jobsCalls()).toBe(1);
    await vi.advanceTimersByTimeAsync(1);
    expect(jobsCalls()).toBe(2);

    // nothing runs any more: the poller backs off to JOBS_SLOW_MS
    await vi.advanceTimersByTimeAsync(JOBS_FAST_MS);
    expect(jobsCalls()).toBe(2);
    await vi.advanceTimersByTimeAsync(JOBS_SLOW_MS - JOBS_FAST_MS);
    expect(jobsCalls()).toBe(3);
    p.stop();
  });

  it("stops polling once stopped (the section unmounted)", async () => {
    mockCall.mockResolvedValue({ jobs: [job("running")] });
    const p = createJobsPoller({ onJobs: () => {}, onError: () => {} });
    p.start();
    await vi.advanceTimersByTimeAsync(0);
    p.stop();
    await vi.advanceTimersByTimeAsync(JOBS_SLOW_MS * 3);
    expect(jobsCalls()).toBe(1);
  });

  it("reports errors and backs off; stops for good on 401", async () => {
    mockCall.mockRejectedValueOnce(new ApiError(0, "offline")).mockRejectedValueOnce(new ApiError(401, "token"));
    const errors: unknown[] = [];
    const p = createJobsPoller({
      onJobs: () => {},
      onError: (e) => errors.push(e),
    });
    p.start();
    await vi.advanceTimersByTimeAsync(0);
    expect(errors).toHaveLength(1);
    await vi.advanceTimersByTimeAsync(JOBS_FAST_MS);
    expect(jobsCalls()).toBe(1); // an error backs off, it does not hammer
    await vi.advanceTimersByTimeAsync(JOBS_SLOW_MS);
    expect(jobsCalls()).toBe(2);
    expect(errors).toHaveLength(2);
    await vi.advanceTimersByTimeAsync(JOBS_SLOW_MS * 3);
    expect(jobsCalls()).toBe(2); // the token is gone: every read would 401
  });

  it("refresh reads at once without overlapping a read in flight", async () => {
    let release: (v: unknown) => void = () => {};
    mockCall.mockImplementationOnce(() => new Promise((r) => (release = r)));
    mockCall.mockResolvedValue({ jobs: [] });
    const p = createJobsPoller({ onJobs: () => {}, onError: () => {} });
    p.start();
    await vi.advanceTimersByTimeAsync(0);
    p.refresh();
    await vi.advanceTimersByTimeAsync(0);
    expect(jobsCalls()).toBe(1); // still waiting for the first answer
    release({ jobs: [] });
    await vi.advanceTimersByTimeAsync(0);
    expect(jobsCalls()).toBe(2); // the refresh ran right after it
    p.stop();
  });

  it("delay: fast only while some job is running", () => {
    expect(nextJobsDelay([])).toBe(JOBS_SLOW_MS);
    expect(nextJobsDelay([job("done"), job("failed")])).toBe(JOBS_SLOW_MS);
    expect(nextJobsDelay([job("done"), job("running")])).toBe(JOBS_FAST_MS);
    expect(JOBS_FAST_MS).toBe(1000);
    expect(JOBS_SLOW_MS).toBe(5000);
  });

  it("cancelJob posts the job id", async () => {
    mockCall.mockResolvedValue({});
    await cancelJob("abc");
    expect(mockCall).toHaveBeenCalledWith("/api/jobs/cancel", { job: "abc" });
  });
});
