import { beforeEach, describe, expect, it } from "vitest";

import { noteJobWatched, resetWatchedJobIds, watchedJobIds, WATCHED_FRESH_MS } from "./watchedJobIds";
import { unclaimedJobs, type RunningEngineJob } from "./engineJobs";

const job = (jobId: string): RunningEngineJob => ({
  jobId,
  bytesSent: 0,
  totalBytes: 0,
  filesFinalized: 0,
  filesFinalizingTotal: 0,
  startedAtMs: 0,
});

describe("watchedJobIds", () => {
  beforeEach(() => resetWatchedJobIds());

  it("claims a job this tab polled recently, so it is not shown as an orphan", () => {
    noteJobWatched("download-1", 1_000);
    const claimed = watchedJobIds(2_000);
    expect(unclaimedJobs([job("download-1"), job("closed-tab")], claimed).map((j) => j.jobId)).toEqual([
      "closed-tab",
    ]);
  });

  it("lets go of a job nothing has polled for a while", () => {
    noteJobWatched("old", 1_000);
    expect(watchedJobIds(1_000 + WATCHED_FRESH_MS + 1).has("old")).toBe(false);
  });
});
