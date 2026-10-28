import { beforeEach, describe, expect, it, vi } from "vitest";

vi.mock("../api", async (importOriginal) => {
  const real = await importOriginal<typeof import("../api")>();
  return { ...real, call: vi.fn() };
});

import { ApiError, call } from "../api";
import { baseName, copyItems, deleteItems, fixPermItems, IntoItselfError, isInside, joinPath, moveItems, parentPath, renameEntry } from "./move";

const mockCall = vi.mocked(call);

beforeEach(() => {
  mockCall.mockReset();
});

describe("paths", () => {
  it("join, parent and base name", () => {
    expect(joinPath("/", "data")).toBe("/data");
    expect(joinPath("/data/", "x")).toBe("/data/x");
    expect(joinPath("/mnt/usb0", "a b")).toBe("/mnt/usb0/a b");
    expect(parentPath("/data/a/b")).toBe("/data/a");
    expect(parentPath("/data")).toBe("/");
    expect(baseName("/data/a/完蛋.txt")).toBe("完蛋.txt");
  });
});

describe("moveItems", () => {
  it("renames each item to <destDir>/<name>", async () => {
    mockCall.mockResolvedValue({} as never);
    const r = await moveItems(["/data/a", "/data/b c"], "/data/dst");
    expect(mockCall.mock.calls).toEqual([
      ["/api/fs/rename", { from: "/data/a", to: "/data/dst/a" }],
      ["/api/fs/rename", { from: "/data/b c", to: "/data/dst/b c" }],
    ]);
    expect(r.started).toEqual([]);
    expect(r.moved).toEqual(["/data/a", "/data/b c"]);
  });

  it("move_falls_back_to_copy_on_cross_mount", async () => {
    mockCall.mockImplementation(async (route: string) => {
      if (route === "/api/fs/rename") throw new ApiError(409, "cross_mount");
      return { job: "ab".repeat(16) } as never;
    });
    const r = await moveItems(["/mnt/usb0/Game"], "/data/homebrew");
    expect(mockCall.mock.calls).toEqual([
      ["/api/fs/rename", { from: "/mnt/usb0/Game", to: "/data/homebrew/Game" }],
      // `dest` is the copied item's own new path, not the folder it goes into.
      ["/api/fs/copy", { src: "/mnt/usb0/Game", dest: "/data/homebrew/Game", move: true }],
    ]);
    expect(r.started).toEqual(["ab".repeat(16)]);
  });

  it("an existing target is a failure, never an overwrite; the rest still go", async () => {
    mockCall
      .mockRejectedValueOnce(new ApiError(409, "exists"))
      .mockResolvedValueOnce({} as never);
    const r = await moveItems(["/data/a", "/data/b"], "/data/dst");
    expect(r.failed).toHaveLength(1);
    expect(r.failed[0].item).toBe("/data/a");
    expect((r.failed[0].error as ApiError).code).toBe("exists");
    expect(r.moved).toEqual(["/data/b"]);
    expect(mockCall).toHaveBeenCalledTimes(2);
  });

  it("an item already in the target folder is left alone", async () => {
    const r = await moveItems(["/data/dst/a"], "/data/dst");
    expect(mockCall).not.toHaveBeenCalled();
    expect(r.failed).toEqual([]);
  });

  it("one request at a time, in order", async () => {
    let inFlight = 0;
    let most = 0;
    mockCall.mockImplementation(async () => {
      inFlight++;
      most = Math.max(most, inFlight);
      await new Promise((res) => setTimeout(res, 1));
      inFlight--;
      throw new ApiError(409, "cross_mount");
    });
    const progress = vi.fn();
    await moveItems(["/a/1", "/a/2", "/a/3"], "/b", { onProgress: progress });
    expect(most).toBe(1);
    expect(progress.mock.calls).toEqual([
      [1, 3],
      [2, 3],
      [3, 3],
    ]);
  });
});

describe("into itself", () => {
  it("copy_or_move_into_itself_is_refused_before_any_request", async () => {
    expect(isInside("/data/a", "/data/a")).toBe(true);
    expect(isInside("/data/a/b/c", "/data/a")).toBe(true);
    expect(isInside("/data/ab", "/data/a")).toBe(false);
    expect(isInside("/data", "/data/a")).toBe(false);
    mockCall.mockResolvedValue({ job: "ab".repeat(16) } as never);
    const m = await moveItems(["/data/a", "/data/x"], "/data/a/sub");
    const c = await copyItems(["/data/a"], "/data/a");
    expect(m.failed.map((f) => f.item)).toEqual(["/data/a"]);
    expect(m.failed[0].error).toBeInstanceOf(IntoItselfError);
    expect(c.failed[0].error).toBeInstanceOf(IntoItselfError);
    // Only the other item was sent; nothing for /data/a.
    expect(mockCall.mock.calls).toEqual([["/api/fs/rename", { from: "/data/x", to: "/data/a/sub/x" }]]);
  });
});

describe("copy, delete, rename", () => {
  it("copy sends each item's full destination path", async () => {
    mockCall.mockResolvedValue({ job: "cd".repeat(16) } as never);
    const r = await copyItems(["/data/x.png"], "/mnt/usb0");
    expect(mockCall).toHaveBeenCalledWith("/api/fs/copy", { src: "/data/x.png", dest: "/mnt/usb0/x.png", move: false });
    expect(r.started).toEqual(["cd".repeat(16)]);
  });

  it("delete starts one job per item", async () => {
    mockCall.mockResolvedValue({ job: "ef".repeat(16) } as never);
    const r = await deleteItems(["/data/a", "/data/b"]);
    expect(mockCall.mock.calls).toEqual([
      ["/api/fs/delete", { path: "/data/a" }],
      ["/api/fs/delete", { path: "/data/b" }],
    ]);
    expect(r.started).toHaveLength(2);
  });

  it("fix permissions sends only the path (the helper fixes the mode), one job at a time", async () => {
    const events: string[] = [];
    let n = 0;
    mockCall.mockImplementation(async (route: string, body?: object) => {
      events.push(`start ${route} ${JSON.stringify(body)}`);
      return { job: `j${++n}` } as never;
    });
    const waitJob = vi.fn(async (id: string) => {
      events.push(`wait ${id}`);
      return "done" as const;
    });
    const r = await fixPermItems(["/data/a", "/data/b"], { waitJob });
    expect(events).toEqual([
      'start /api/fs/chmod {"path":"/data/a"}',
      "wait j1",
      'start /api/fs/chmod {"path":"/data/b"}',
      "wait j2",
    ]);
    expect(r.started).toEqual(["j1", "j2"]);
  });

  it("rename keeps the folder and changes the name", async () => {
    mockCall.mockResolvedValue({} as never);
    await renameEntry("/data", "old", "new");
    expect(mockCall).toHaveBeenCalledWith("/api/fs/rename", { from: "/data/old", to: "/data/new" });
  });
});

describe("one job at a time", () => {
  it("batch_waits_for_each_job_before_the_next", async () => {
    const events: string[] = [];
    let n = 0;
    mockCall.mockImplementation(async (_route: string, body?: object) => {
      events.push(`start ${(body as { path: string }).path}`);
      return { job: `j${++n}` } as never;
    });
    const waitJob = vi.fn(async (id: string) => {
      events.push(`wait ${id}`);
      await new Promise((res) => setTimeout(res, 1));
      events.push(`ended ${id}`);
      return id === "j2" ? ("failed" as const) : ("done" as const);
    });
    const r = await deleteItems(["/d/a", "/d/b", "/d/c"], { waitJob });
    // Never two jobs at once: the next item is sent only after the previous job ended.
    expect(events).toEqual([
      "start /d/a",
      "wait j1",
      "ended j1",
      "start /d/b",
      "wait j2",
      "ended j2",
      "start /d/c",
      "wait j3",
      "ended j3",
    ]);
    expect(r.started).toEqual(["j1", "j2", "j3"]);
    expect(r.jobFailed).toEqual(["/d/b"]);
    expect(r.skipped).toEqual([]);
  });

  it("a rename needs no wait; a cross-drive move waits for its job", async () => {
    mockCall.mockImplementation(async (route: string, body?: object) => {
      if (route === "/api/fs/rename" && (body as { from: string }).from === "/mnt/usb0/x") throw new ApiError(409, "cross_mount");
      return (route === "/api/fs/copy" ? { job: "c1" } : {}) as never;
    });
    const waitJob = vi.fn(async () => "done" as const);
    const r = await moveItems(["/data/a", "/mnt/usb0/x"], "/data/dst", { waitJob });
    expect(waitJob.mock.calls).toEqual([["c1"]]);
    expect(r.moved).toEqual(["/data/a"]);
    expect(r.started).toEqual(["c1"]);
  });

  it("stop_sends_no_further_item", async () => {
    let stop = false;
    mockCall.mockResolvedValue({ job: "ab".repeat(16) } as never);
    const r = await copyItems(["/a/1", "/a/2", "/a/3"], "/b", {
      waitJob: async () => {
        stop = true; // Stop pressed while the first copy runs
        return "done";
      },
      stopped: () => stop,
    });
    expect(mockCall).toHaveBeenCalledTimes(1);
    expect(r.skipped).toEqual(["/a/2", "/a/3"]);
  });

  it("a 401 while waiting ends the batch", async () => {
    mockCall.mockResolvedValue({ job: "ab".repeat(16) } as never);
    const r = await copyItems(["/a/1", "/a/2"], "/b", {
      waitJob: async () => {
        throw new ApiError(401, "token");
      },
    });
    expect(mockCall).toHaveBeenCalledTimes(1);
    expect(r.failed.map((f) => f.item)).toEqual(["/a/1"]);
    expect(r.skipped).toEqual(["/a/2"]);
  });
});
