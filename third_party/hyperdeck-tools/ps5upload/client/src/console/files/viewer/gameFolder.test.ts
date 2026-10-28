import { describe, expect, it, vi } from "vitest";
import { ApiError } from "../../api";
import { readGameFolder, sceSysName, GAME_META_MAX } from "./gameFolder";

const json = (o: unknown) => new TextEncoder().encode(JSON.stringify(o));

describe("game folder", () => {
  it("finds sce_sys as listed (any case), folders only", () => {
    expect(sceSysName([{ name: "SCE_SYS", kind: "dir", size: 0, mtime: null }])).toBe("SCE_SYS");
    expect(sceSysName([{ name: "sce_sys", kind: "file", size: 1, mtime: null }])).toBeNull();
    expect(sceSysName([])).toBeNull();
  });

  it("reads param.json first, at most 64 KiB, and names the icon", async () => {
    const read = vi.fn(async () => json({ titleId: "PPSA01650", localizedParameters: { "en-US": { titleName: "ASTRO BOT" } } }));
    const g = await readGameFolder("/data/games/Astro/", "sce_sys", undefined, { read });
    expect(g).toEqual({
      info: expect.objectContaining({ title: "ASTRO BOT", titleId: "PPSA01650" }),
      source: "param.json",
      iconPath: "/data/games/Astro/sce_sys/icon0.png",
    });
    expect(read).toHaveBeenCalledTimes(1);
    expect(read.mock.calls[0]).toEqual(["/data/games/Astro/sce_sys/param.json", GAME_META_MAX, undefined]);
  });

  it("no param.json (404): param.sfo", async () => {
    const read = vi.fn(async (path: string) => {
      if (path.endsWith("param.json")) throw new ApiError(404, "not_found");
      throw new ApiError(404, "not_found");
    });
    expect(await readGameFolder("/mnt/usb0/Jak", "sce_sys", undefined, { read })).toBeNull();
    expect(read.mock.calls.map((c) => c[0])).toEqual(["/mnt/usb0/Jak/sce_sys/param.json", "/mnt/usb0/Jak/sce_sys/param.sfo"]);
  });

  it("a param.json that names no game falls through to param.sfo", async () => {
    const read = vi.fn(async (path: string) => (path.endsWith("param.json") ? json({ x: 1 }) : new Uint8Array([1, 2, 3])));
    expect(await readGameFolder("/data/x", "sce_sys", undefined, { read })).toBeNull();
    expect(read).toHaveBeenCalledTimes(2);
  });

  it("a failure other than missing (the helper gone, a 401) rejects", async () => {
    const read = vi.fn(async () => {
      throw new ApiError(0, "offline");
    });
    await expect(readGameFolder("/data/x", "sce_sys", undefined, { read })).rejects.toBeInstanceOf(ApiError);
  });
});
