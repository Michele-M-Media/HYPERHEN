import { describe, expect, it, vi } from "vitest";
import { ApiError } from "../api";
import type { Job } from "../home/useJobs";
import { BATCH_ABSENT_READS, createJobWaiter, currentBatch, onBatchChange, startBatch, stopBatch } from "./batch";
import type { BatchResult } from "./move";

const job = (id: string, state: Job["state"]): Job => ({
  job: id,
  kind: "copy",
  state,
  files_done: 0,
  files_total: 0,
  bytes_done: 0,
  bytes_total: 0,
  mine: true,
});

const empty = (): BatchResult => ({ started: [], moved: [], failed: [], jobFailed: [], skipped: [] });

describe("createJobWaiter", () => {
  it("returns the end state once the job leaves running", async () => {
    const reads = [[job("a", "running")], [job("a", "running")], [job("a", "failed")]];
    const fetchJobs = vi.fn(async () => reads.shift() ?? []);
    const sleep = vi.fn(async () => undefined);
    const wait = createJobWaiter({ fetchJobs, sleep });
    await expect(wait("a")).resolves.toBe("failed");
    expect(fetchJobs).toHaveBeenCalledTimes(3);
  });

  it("a job gone from the list for several reads has ended", async () => {
    const fetchJobs = vi.fn(async () => [job("other", "running")]);
    const wait = createJobWaiter({ fetchJobs, sleep: async () => undefined });
    await expect(wait("a")).resolves.toBe("gone");
    expect(fetchJobs).toHaveBeenCalledTimes(BATCH_ABSENT_READS);
  });

  it("retries a failed read, throws a 401", async () => {
    const fetchJobs = vi
      .fn<() => Promise<Job[]>>()
      .mockRejectedValueOnce(new ApiError(0, "offline"))
      .mockResolvedValueOnce([job("a", "done")]);
    const wait = createJobWaiter({ fetchJobs, sleep: async () => undefined });
    await expect(wait("a")).resolves.toBe("done");
    const gone = createJobWaiter({
      fetchJobs: async () => {
        throw new ApiError(401, "token");
      },
      sleep: async () => undefined,
    });
    await expect(gone("a")).rejects.toBeInstanceOf(ApiError);
  });

  it("stop leaves the running job and returns", async () => {
    let stop = false;
    const wait = createJobWaiter({
      fetchJobs: async () => [job("a", "running")],
      sleep: async () => {
        stop = true;
      },
    });
    await expect(wait("a", () => stop)).resolves.toBe("left");
  });
});

describe("the page's one batch", () => {
  it("one at a time, with progress and Stop", async () => {
    const seen: (string | null)[] = [];
    const off = onBatchChange(() => {
      const b = currentBatch();
      seen.push(b ? `${b.op} ${b.at}/${b.total}${b.stopping ? " stopping" : ""}` : null);
    });
    let release: () => void = () => undefined;
    const gate = new Promise<void>((res) => (release = res));
    const run = startBatch("copy", 40, async (ctl) => {
      ctl.onProgress(3, 40);
      await gate;
      expect(ctl.stopped()).toBe(true);
      return { ...empty(), skipped: ["x"] };
    });
    expect(run).not.toBeNull();
    // A second batch is refused while one runs.
    expect(startBatch("delete", 1, async () => empty())).toBeNull();
    stopBatch();
    release();
    await expect(run).resolves.toMatchObject({ skipped: ["x"] });
    off();
    expect(seen).toEqual(["copy 0/40", "copy 3/40", "copy 3/40 stopping", null]);
    expect(currentBatch()).toBeNull();
  });

  it("carries the running item's bytes (unpack's percent)", async () => {
    let release: () => void = () => undefined;
    const gate = new Promise<void>((res) => (release = res));
    const run = startBatch("unpack", 1, async (ctl) => {
      ctl.onBytes(300, 1200);
      await gate;
      return empty();
    });
    expect(currentBatch()).toMatchObject({ bytesDone: 300, bytesTotal: 1200 });
    release();
    await run;
    expect(currentBatch()).toBeNull();
  });

  it("keeps what it touches while it runs (eject reads it)", async () => {
    let release: () => void = () => undefined;
    const gate = new Promise<void>((res) => (release = res));
    const run = startBatch("copy", 1, async () => {
      await gate;
      return empty();
    }, ["/data/a", "/mnt/usb0"]);
    expect(currentBatch()?.paths).toEqual(["/data/a", "/mnt/usb0"]);
    release();
    await run;
    expect(currentBatch()).toBeNull();
  });

  it("ends even when the work throws", async () => {
    const run = startBatch("move", 2, async () => {
      throw new Error("boom");
    });
    await expect(run).rejects.toThrow("boom");
    expect(currentBatch()).toBeNull();
  });
});
