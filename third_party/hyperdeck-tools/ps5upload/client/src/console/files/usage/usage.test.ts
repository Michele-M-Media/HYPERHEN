import { afterEach, describe, expect, it, vi } from "vitest";
import { ApiError } from "../../api";
import { startFolderSize, type ListPage, type WalkDeps } from "../folderSize";
import type { Entry } from "../useDir";
import { barPercent, measureUsage, sortRows, startUsage, stopUsage, usageRunning, type UsageSnapshot } from "./usage";

const f = (name: string, size: number): Entry => ({ name, kind: "file", size, mtime: 0 });
const d = (name: string): Entry => ({ name, kind: "dir", size: 0, mtime: 0 });

/** A fake tree: path -> entries; pages of `page` entries. */
function tree(t: Record<string, Entry[]>, opts: { page?: number; fail?: Record<string, unknown>; clock?: { t: number; per: number } } = {}) {
  const page = opts.page ?? 2;
  const calls: string[] = [];
  const deps: WalkDeps = {
    list: vi.fn(async (path: string, offset: number): Promise<ListPage> => {
      calls.push(`${path}@${offset}`);
      if (opts.clock) opts.clock.t += opts.clock.per;
      if (opts.fail && path in opts.fail) throw opts.fail[path];
      const all = t[path];
      if (!all) throw new ApiError(404, "not_found");
      const entries = all.slice(offset, offset + page);
      return { entries, more: offset + page < all.length, next: offset + entries.length };
    }),
    now: () => opts.clock?.t ?? 0,
  };
  return { deps, calls };
}

const TREE: Record<string, Entry[]> = {
  "/data": [d("games"), d("music"), f("a.bin", 100), f("b.bin", 50), d("empty")],
  "/data/games": [d("A"), f("g.pkg", 5000)],
  "/data/games/A": [f("eboot.bin", 3000), f("x", 1)],
  "/data/music": [f("m.mp3", 700)],
  "/data/empty": [],
};

describe("disk usage", () => {
  afterEach(async () => {
    await stopUsage();
  });

  it("each subfolder's size (all of its tree) and the files here, with progress", async () => {
    const { deps } = tree(TREE);
    const updates: UsageSnapshot[] = [];
    const s = await measureUsage("/data", { cancelled: () => false, onUpdate: (u) => updates.push(u) }, deps);
    expect(s.end).toBe("done");
    const by = Object.fromEntries(s.rows.map((r) => [r.files ? "(files)" : r.name, [r.bytes, r.state]]));
    expect(by).toEqual({
      "(files)": [150, "done"],
      games: [8001, "done"],
      music: [700, "done"],
      empty: [0, "done"],
    });
    expect(s.folders).toBe(3);
    expect(s.measured).toBe(3);
    expect(updates.length).toBeGreaterThan(3);
    // a row is "walking" while its walk runs
    expect(updates.some((u) => u.rows.some((r) => r.name === "games" && r.state === "walking"))).toBe(true);
  });

  it("biggest first; waiting rows after the measured ones; bars against the biggest", () => {
    const rows = sortRows([
      { name: "a", path: "/a", files: false, bytes: 5, entries: 1, state: "done" },
      { name: "b", path: "/b", files: false, bytes: 0, entries: 0, state: "waiting" },
      { name: "c", path: "/c", files: false, bytes: 50, entries: 1, state: "done" },
    ]);
    expect(rows.map((r) => r.name)).toEqual(["c", "a", "b"]);
    expect(barPercent(50, 50)).toBe(100);
    expect(barPercent(1, 1000)).toBe(1); // tiny but not invisible
    expect(barPercent(0, 1000)).toBe(0);
    expect(barPercent(5, 0)).toBe(0);
  });

  it("is bounded for the whole measurement: an entry cap stops it, the rest is not measured", async () => {
    const { deps } = tree(TREE);
    const s = await measureUsage("/data", { cancelled: () => false, onUpdate: () => undefined, maxEntries: 8 }, deps);
    expect(s.end).toBe("capped");
    expect(s.entries).toBeLessThanOrEqual(8);
    expect(s.rows.find((r) => r.name === "games")?.state).toBe("partial");
    expect(s.rows.find((r) => r.name === "music")?.state).toBe("skipped");
  });

  it("and by time", async () => {
    const clock = { t: 0, per: 1000 };
    const { deps } = tree(TREE, { clock });
    const s = await measureUsage("/data", { cancelled: () => false, onUpdate: () => undefined, maxMs: 4500 }, deps);
    expect(s.end).toBe("timeout");
    expect(s.rows.some((r) => r.state === "skipped")).toBe(true);
  });

  it("subfolders past the folder cap are not listed, and the end says capped", async () => {
    const big: Record<string, Entry[]> = { "/x": [d("1"), d("2"), d("3")], "/x/1": [], "/x/2": [], "/x/3": [] };
    const { deps } = tree(big);
    const s = await measureUsage("/x", { cancelled: () => false, onUpdate: () => undefined, maxFolders: 2 }, deps);
    expect(s.folders).toBe(2);
    expect(s.end).toBe("capped");
  });

  it("Cancel stops before the next page", async () => {
    let n = 0;
    const { deps, calls } = tree(TREE);
    const s = await measureUsage("/data", { cancelled: () => ++n > 4, onUpdate: () => undefined }, deps);
    expect(s.end).toBe("cancelled");
    const after = calls.length;
    expect(after).toBeLessThan(8);
  });

  it("an unreadable subfolder is marked and skipped; the folder itself failing is an error", async () => {
    const { deps } = tree(TREE, { fail: { "/data/music": new ApiError(403, "path_not_allowed") } });
    const s = await measureUsage("/data", { cancelled: () => false, onUpdate: () => undefined }, deps);
    expect(s.end).toBe("done");
    expect(s.rows.find((r) => r.name === "music")?.state).toBe("unreadable");
    const bad = tree(TREE, { fail: { "/data": new ApiError(404, "not_found") } });
    const e = await measureUsage("/data", { cancelled: () => false, onUpdate: () => undefined }, bad.deps);
    expect(e.end).toBe("error");
    expect(e.error).toBeInstanceOf(ApiError);
  });

  it("a 401 inside stops it (the page must reload)", async () => {
    const { deps } = tree(TREE, { fail: { "/data/games": new ApiError(401, "token") } });
    const s = await measureUsage("/data", { cancelled: () => false, onUpdate: () => undefined }, deps);
    expect(s.end).toBe("error");
  });

  it("one at a time: a second start, or a folder-size walk, is refused; stopUsage waits for the end", async () => {
    let release: () => void = () => undefined;
    const gate = new Promise<void>((r) => {
      release = r;
    });
    const slow: WalkDeps = {
      list: async () => {
        await gate;
        return { entries: [], more: false, next: 0 };
      },
      now: () => 0,
    };
    const run = startUsage("/data", () => undefined, slow);
    expect(run).not.toBeNull();
    expect(usageRunning()).toBe(true);
    expect(startUsage("/mnt/usb0", () => undefined, slow)).toBeNull();
    const stopping = stopUsage();
    release();
    await stopping;
    expect(usageRunning()).toBe(false);
    expect((await run?.done)?.end).toBe("cancelled");

    // a "Calculate folder size" walk running blocks a measurement too
    let release2: () => void = () => undefined;
    const gate2 = new Promise<void>((r) => {
      release2 = r;
    });
    const walk = startFolderSize("/data", () => undefined, {
      list: async () => {
        await gate2;
        return { entries: [], more: false };
      },
      now: () => 0,
    });
    expect(startUsage("/data", () => undefined, slow)).toBeNull();
    walk?.cancel();
    release2();
    await walk?.done;
    const again = startUsage("/data", () => undefined, slow);
    expect(again).not.toBeNull();
    // and the other way round: a folder size walk waits while Disk usage runs
    expect(startFolderSize("/data", () => undefined)).toBeNull();
    await again?.done;
    expect(usageRunning()).toBe(false);
  });
});
