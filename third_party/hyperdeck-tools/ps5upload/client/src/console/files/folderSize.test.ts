import { describe, expect, it } from "vitest";
import { ApiError } from "../api";
import { folderSizeRunning, startFolderSize, walkFolder, type ListPage, type WalkDeps } from "./folderSize";
import type { Entry } from "./useDir";

const file = (name: string, size: number): Entry => ({ name, kind: "file", size, mtime: 0 });
const dir = (name: string): Entry => ({ name, kind: "dir", size: 0, mtime: 0 });

/** A fake tree: path → entries, paged like the helper (offset/limit, next). */
function tree(map: Record<string, Entry[]>, fail: string[] = []) {
  const calls: string[] = [];
  let clock = 0;
  const deps: WalkDeps = {
    list: async (path, offset, limit): Promise<ListPage> => {
      calls.push(`${path}@${offset}`);
      clock += 10;
      if (fail.includes(path)) throw new ApiError(403, "path_not_allowed");
      const all = map[path];
      if (!all) throw new ApiError(404, "missing");
      const entries = all.slice(offset, offset + limit);
      return { entries, more: offset + limit < all.length, next: offset + entries.length };
    },
    now: () => clock,
  };
  return { deps, calls, tick: (ms: number) => (clock += ms) };
}

describe("folder size walk", () => {
  it("counts files, folders and bytes through every level and page", async () => {
    const big = Array.from({ length: 600 }, (_, i) => file(`f${i}`, 2));
    const { deps, calls } = tree({
      "/data/g": [dir("a"), dir("b"), file("x.bin", 100), { name: "lnk", kind: "link", size: 999, mtime: 0 }],
      "/data/g/a": big,
      "/data/g/b": [dir("c")],
      "/data/g/b/c": [file("y", 5)],
    });
    const r = await walkFolder("/data/g", { cancelled: () => false }, deps);
    expect(r).toMatchObject({ end: "done", files: 603, folders: 3, bytes: 100 + 1200 + 5, unreadable: 0 });
    // 600 entries = 3 pages of 256
    expect(calls.filter((c) => c.startsWith("/data/g/a@"))).toEqual(["/data/g/a@0", "/data/g/a@256", "/data/g/a@512"]);
  });

  it("stops at the entry cap", async () => {
    const { deps } = tree({ "/r": Array.from({ length: 50 }, (_, i) => file(`f${i}`, 1)) });
    const r = await walkFolder("/r", { cancelled: () => false, maxEntries: 20 }, deps);
    expect(r).toMatchObject({ end: "capped", entries: 20, files: 20, bytes: 20 });
  });

  it("stops at the time cap", async () => {
    const map: Record<string, Entry[]> = { "/r": [dir("a"), dir("b"), dir("c")], "/r/a": [file("1", 1)], "/r/b": [file("2", 1)], "/r/c": [] };
    const { deps, calls } = tree(map);
    // each list costs 10 ms of the fake clock: the 25 ms cap ends it after three reads
    const r = await walkFolder("/r", { cancelled: () => false, maxMs: 25 }, deps);
    expect(r.end).toBe("timeout");
    expect(r.files).toBe(2);
    expect(calls).toEqual(["/r@0", "/r/a@0", "/r/b@0"]);
  });

  it("cancels before the next page", async () => {
    const map: Record<string, Entry[]> = { "/r": [dir("a"), dir("b")], "/r/a": [file("1", 1)], "/r/b": [file("2", 1)] };
    const { deps, calls } = tree(map);
    let stop = false;
    const r = await walkFolder(
      "/r",
      {
        cancelled: () => stop,
        onProgress: (c) => {
          if (c.entries >= 2) stop = true;
        },
      },
      deps,
    );
    expect(r.end).toBe("cancelled");
    expect(calls).toEqual(["/r@0"]);
  });

  it("skips an unreadable subfolder; the folder itself failing is an error", async () => {
    const { deps } = tree({ "/r": [dir("locked"), file("a", 3)], "/r/locked": [] }, ["/r/locked"]);
    const r = await walkFolder("/r", { cancelled: () => false }, deps);
    expect(r).toMatchObject({ end: "done", files: 1, folders: 1, bytes: 3, unreadable: 1 });
    const bad = await walkFolder("/nope", { cancelled: () => false }, tree({}).deps);
    expect(bad.end).toBe("error");
    expect(bad.error).toBeInstanceOf(ApiError);
  });

  it("never loops on a page that does not move on", async () => {
    let n = 0;
    const deps: WalkDeps = {
      list: async () => {
        n++;
        return { entries: [], more: true, next: 0 };
      },
      now: () => 0,
    };
    const r = await walkFolder("/r", { cancelled: () => false }, deps);
    expect(r.end).toBe("done");
    expect(n).toBe(1);
  });

  it("runs one walk at a time", async () => {
    let release: () => void = () => undefined;
    const gate = new Promise<void>((res) => (release = res));
    const deps: WalkDeps = {
      list: async () => {
        await gate;
        return { entries: [file("a", 1)], more: false, next: 1 };
      },
      now: () => 0,
    };
    const first = startFolderSize("/a", () => undefined, deps);
    expect(first).not.toBeNull();
    expect(folderSizeRunning()).toBe(true);
    expect(startFolderSize("/b", () => undefined, deps)).toBeNull();
    release();
    const r = await first!.done;
    expect(r.end).toBe("done");
    expect(folderSizeRunning()).toBe(false);
    const again = startFolderSize("/b", () => undefined, deps);
    expect(again).not.toBeNull();
    again!.cancel();
    expect((await again!.done).end).toBe("cancelled");
  });
});
