import { describe, expect, it, vi } from "vitest";

import type { JobSnapshot } from "../api/ps5";
import { pollFsDownload, type FsDownloadPollHooks } from "./fsDownloadPoll";
import { MAX_POLL_FAILURES } from "./transfer";

const running = (bytes: number) =>
  ({ status: "running", bytes_sent: bytes, total_bytes: 100 }) as unknown as JobSnapshot;
const done = { status: "done" } as unknown as JobSnapshot;

function hooks(): FsDownloadPollHooks & Record<string, ReturnType<typeof vi.fn>> {
  return {
    isLive: vi.fn(() => true),
    onProgress: vi.fn(),
    onLostContact: vi.fn(),
    onDone: vi.fn(),
    onFailed: vi.fn(),
  } as unknown as FsDownloadPollHooks & Record<string, ReturnType<typeof vi.fn>>;
}

describe("pollFsDownload", () => {
  it("rides out failed reads, reports lost contact, and finishes when the engine answers again", async () => {
    const answers: Array<JobSnapshot | Error> = [
      running(10),
      ...Array.from({ length: MAX_POLL_FAILURES + 2 }, () => new Error("fetch failed")),
      running(60),
      done,
    ];
    const h = hooks();
    await pollFsDownload(
      "j1",
      h,
      {
        jobStatus: async () => {
          const a = answers.shift()!;
          if (a instanceof Error) throw a;
          return a;
        },
        sleep: async () => {},
      },
    );
    expect(h.onFailed).not.toHaveBeenCalled();
    expect(h.onLostContact).toHaveBeenCalledWith("fetch failed");
    expect(h.onLostContact).toHaveBeenLastCalledWith(null);
    expect(h.onProgress).toHaveBeenLastCalledWith({ bytesReceived: 60, totalBytes: 100 });
    expect(h.onDone).toHaveBeenCalledTimes(1);
  });

  it("does not report lost contact for a single dropped read", async () => {
    const answers: Array<JobSnapshot | Error> = [new Error("blip"), done];
    const h = hooks();
    await pollFsDownload("j1", h, {
      jobStatus: async () => {
        const a = answers.shift()!;
        if (a instanceof Error) throw a;
        return a;
      },
      sleep: async () => {},
    });
    expect(h.onLostContact).not.toHaveBeenCalled();
    expect(h.onDone).toHaveBeenCalled();
  });

  it("ends the download when the engine no longer knows the job", async () => {
    const h = hooks();
    await pollFsDownload("j1", h, {
      jobStatus: async () => {
        throw new Error("job not found");
      },
      sleep: async () => {},
    });
    expect(h.onFailed).toHaveBeenCalledWith("job not found");
  });

  it("stops quietly once the user cancelled", async () => {
    const h = hooks();
    h.isLive = vi.fn(() => false);
    const jobStatus = vi.fn();
    await pollFsDownload("j1", h, { jobStatus, sleep: async () => {} });
    expect(jobStatus).not.toHaveBeenCalled();
  });
});
