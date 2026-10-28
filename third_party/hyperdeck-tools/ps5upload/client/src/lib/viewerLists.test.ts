import { describe, expect, it } from "vitest";

import { filterFiles, relatedPackages, visibleWindow } from "./viewerLists";

describe("the Files list", () => {
  const files = [
    { path: "eboot.bin", size: 10, encrypted: false },
    { path: "sce_sys/param.json", size: 2, encrypted: false },
    { path: "sce_sys/icon0.png", size: 5, encrypted: true },
  ];

  it("filters by any part of the path, ignoring case", () => {
    expect(filterFiles(files, "SCE_SYS").map((f) => f.path)).toEqual(["sce_sys/param.json", "sce_sys/icon0.png"]);
    expect(filterFiles(files, "  ")).toHaveLength(3);
  });

  it("renders only the rows in view, plus a margin", () => {
    expect(visibleWindow(0, 280, 28, 10_000, 5)).toEqual({ start: 0, end: 15 });
    expect(visibleWindow(28 * 1000, 280, 28, 10_000, 5)).toEqual({ start: 995, end: 1015 });
    expect(visibleWindow(28 * 9_999, 280, 28, 10_000, 5)).toEqual({ start: 9_994, end: 10_000 });
    expect(visibleWindow(0, 280, 28, 0, 5)).toEqual({ start: 0, end: 0 });
  });
});

describe("related packages", () => {
  const lib = [
    { path: "/data/pkg/base.pkg", contentId: "UP9000-PPSA01234_00-GAME000000000000", category: "gd", appVer: "01.000", title: "Game" },
    { path: "/data/pkg/upd.pkg", contentId: "UP9000-PPSA01234_00-GAME000000000000", category: "gp", appVer: "01.010", title: "Game" },
    { path: "/data/pkg/dlc.pkg", contentId: "UP9000-PPSA01234_00-DLC0000000000001", category: "ac", title: "Skin pack" },
    { path: "/data/pkg/other.pkg", contentId: "UP9000-PPSA09999_00-OTHER00000000000", category: "gd", title: "Other" },
  ];

  it("groups the title's other packages base → update → DLC, leaving out this one", () => {
    const groups = relatedPackages("PPSA01234", "/data/pkg/upd.pkg", lib, []);
    expect(groups.map((g) => g.kind)).toEqual(["game", "dlc"]);
    expect(groups[0].items.map((i) => i.path)).toEqual(["/data/pkg/base.pkg"]);
    expect(groups[1].items[0]).toMatchObject({ name: "Skin pack", where: "library" });
  });

  it("leaves out the row being viewed when it was opened from its original file", () => {
    const rows = [{ ...lib[0], sourcePath: "/Users/me/base.pkg" }, lib[1]];
    const groups = relatedPackages("PPSA01234", "/Users/me/base.pkg", rows, []);
    expect(groups.flatMap((g) => g.items.map((i) => i.path))).toEqual(["/data/pkg/upd.pkg"]);
  });

  it("includes queued installs of the same title", () => {
    const groups = relatedPackages("PPSA01234", null, [], [
      { id: "q1", displayName: "Game v1.01", contentId: "UP9000-PPSA01234_00-GAME000000000000", category: "gp", status: "pending" },
    ]);
    expect(groups).toEqual([
      { kind: "update", items: [{ name: "Game v1.01", version: null, where: "queue", path: null, status: "pending" }] },
    ]);
  });

  it("says nothing without a title id", () => {
    expect(relatedPackages("", null, lib, [])).toEqual([]);
  });
});
