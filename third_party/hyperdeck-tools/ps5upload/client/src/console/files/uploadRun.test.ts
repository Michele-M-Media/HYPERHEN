import { describe, expect, it, vi } from "vitest";
import { ApiError } from "../api";
import type { UploadItem } from "./transfer";
import { runUploads, type UploadDeps } from "./uploadRun";

const item = (rel: string, size = 4): UploadItem => ({ rel, file: new Blob(["x".repeat(size)]) as File });

function deps(over: Partial<UploadDeps> = {}, taken: Record<string, "file" | "dir"> = {}) {
  const sent: { dest: string; overwrite: boolean }[] = [];
  const d: UploadDeps = {
    mkdirs: vi.fn(async () => undefined),
    kindAt: async (p) => taken[p] ?? null,
    upload: async (dest, _f, overwrite, onProgress) => {
      onProgress(2);
      sent.push({ dest, overwrite });
    },
    decide: vi.fn(async () => ({ choice: "skip" as const, all: false })),
    ...over,
  };
  return { d, sent };
}

describe("runUploads", () => {
  it("makes the folders first, then sends each file under its relative path", async () => {
    const { d, sent } = deps();
    const r = await runUploads([item("A/b/1.txt"), item("top.txt")], "/data/x", d);
    expect(d.mkdirs).toHaveBeenCalledWith("/data/x", ["A", "A/b"]);
    expect(sent).toEqual([
      { dest: "/data/x/A/b/1.txt", overwrite: false },
      { dest: "/data/x/top.txt", overwrite: false },
    ]);
    expect(r.uploaded).toEqual(["A/b/1.txt", "top.txt"]);
  });
  it("asks about a name that is taken: skip, keep both, replace", async () => {
    const taken = { "/data/x/a.txt": "file" as const, "/data/x/b.txt": "file" as const, "/data/x/c.txt": "file" as const };
    const answers = [
      { choice: "skip" as const, all: false },
      { choice: "keep" as const, all: false },
      { choice: "replace" as const, all: false },
    ];
    const { d, sent } = deps({ decide: vi.fn(async () => answers.shift()!) }, taken);
    const r = await runUploads([item("a.txt"), item("b.txt"), item("c.txt")], "/data/x", d);
    expect(r.skipped).toEqual(["a.txt"]);
    expect(sent).toEqual([
      { dest: "/data/x/b (2).txt", overwrite: false },
      { dest: "/data/x/c.txt", overwrite: true },
    ]);
  });
  it("answers the rest the same way with all, but never replaces a folder", async () => {
    const taken = { "/data/x/a.txt": "file" as const, "/data/x/b.txt": "file" as const, "/data/x/d": "dir" as const };
    const decide = vi.fn(async (_n: string, canReplace: boolean) => ({ choice: canReplace ? ("replace" as const) : ("skip" as const), all: true }));
    const { d, sent } = deps({ decide }, taken);
    const r = await runUploads([item("a.txt"), item("b.txt"), item("d")], "/data/x", d);
    expect(decide).toHaveBeenCalledTimes(2);
    expect(decide.mock.calls[1]).toEqual(["d", false, 0]);
    expect(sent.map((s) => s.overwrite)).toEqual([true, true]);
    expect(r.skipped).toEqual(["d"]);
  });
  it("stops between files, counts an aborted file as not sent and a refusal as failed", async () => {
    let n = 0;
    let stop = false;
    const { d } = deps({
      upload: async () => {
        n++;
        if (n === 1) throw new ApiError(0, "aborted");
        if (n === 2) throw new ApiError(507, "no_space");
        stop = true;
      },
      stopped: () => stop,
    });
    const r = await runUploads([item("1"), item("2"), item("3"), item("4")], "/d", d);
    expect(r.stopped).toEqual(["1", "4"]);
    expect(r.failed.map((f) => f.name)).toEqual(["2"]);
    expect(r.uploaded).toEqual(["3"]);
  });
  it("reports progress with the bytes done before", async () => {
    const seen: number[] = [];
    const { d } = deps({ onProgress: (p) => seen.push(p.doneBytes + p.sent) });
    await runUploads([item("a", 10), item("b", 10)], "/d", d);
    expect(seen).toEqual([0, 2, 10, 12]);
  });
});
