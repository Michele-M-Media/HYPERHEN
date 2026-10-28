import { describe, expect, it } from "vitest";

import { scanView, elapsedLabel } from "./scanProgress";

const base = {
  running: true,
  deep: false,
  found: 0,
  done: 0,
  started_ms: 0,
  finished_ms: 0,
  error: null,
  games: 0,
  locations: 0,
};

describe("scanView", () => {
  it("has no bar while it is still finding items, and says which folder", () => {
    const v = scanView({ ...base, phase: "finding", current: "/Volumes/S/games", roots_total: 2, roots_done: 1 });
    expect(v.phase).toBe("finding");
    expect(v.value).toBeNull();
    expect(v.folder).toEqual({ n: 2, total: 2 });
    expect(v.current).toBe("/Volumes/S/games");
  });

  it("fills the bar by items read while reading", () => {
    const v = scanView({ ...base, phase: "reading", found: 80, done: 20, current: "Game.pkg" });
    expect(v.value).toBeCloseTo(0.25);
    expect(v.counts).toEqual({ done: 20, total: 80 });
  });

  it("fills the bar by games named while naming", () => {
    const v = scanView({ ...base, phase: "naming", games_total: 10, games_done: 9 });
    expect(v.value).toBeCloseTo(0.9);
    expect(v.counts).toEqual({ done: 9, total: 10 });
  });

  it("is full while saving", () => {
    expect(scanView({ ...base, phase: "saving" }).value).toBe(1);
  });

  it("works with an engine that only reports found and done", () => {
    expect(scanView({ ...base, found: 0 }).phase).toBe("finding");
    const v = scanView({ ...base, found: 4, done: 1 });
    expect(v.phase).toBe("reading");
    expect(v.value).toBeCloseTo(0.25);
  });
});

describe("elapsedLabel", () => {
  it("reads as m:ss, and h:mm:ss past an hour", () => {
    expect(elapsedLabel(0, 65_000)).toBe("1:05");
    expect(elapsedLabel(0, 3_725_000)).toBe("1:02:05");
    expect(elapsedLabel(0, 0)).toBe("0:00");
  });
});
