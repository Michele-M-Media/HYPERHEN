import { describe, expect, it } from "vitest";

import { edgeFade, MAX_FADE_PX } from "./edgeFade";

describe("edgeFade", () => {
  it("fades nothing when everything fits", () => {
    expect(edgeFade({ scrollLeft: 0, scrollWidth: 300, clientWidth: 300 })).toEqual({ start: 0, end: 0 });
    // Sub-pixel rounding must not count as overflow.
    expect(edgeFade({ scrollLeft: 0, scrollWidth: 301, clientWidth: 300 })).toEqual({ start: 0, end: 0 });
  });

  it("fades the end while more is hidden past it", () => {
    expect(edgeFade({ scrollLeft: 0, scrollWidth: 500, clientWidth: 300 })).toEqual({
      start: 0,
      end: MAX_FADE_PX,
    });
  });

  it("fades both edges in the middle of the strip", () => {
    expect(edgeFade({ scrollLeft: 80, scrollWidth: 500, clientWidth: 300 })).toEqual({
      start: MAX_FADE_PX,
      end: MAX_FADE_PX,
    });
  });

  it("fades only the start once scrolled to the end", () => {
    expect(edgeFade({ scrollLeft: 200, scrollWidth: 500, clientWidth: 300 })).toEqual({
      start: MAX_FADE_PX,
      end: 0,
    });
  });

  it("fades no wider than what is hidden", () => {
    // 6px of the last button clipped: a full-width fade would wipe out the
    // whole button for the sake of those 6px.
    expect(edgeFade({ scrollLeft: 0, scrollWidth: 306, clientWidth: 300 })).toEqual({ start: 0, end: 6 });
    expect(edgeFade({ scrollLeft: 4, scrollWidth: 500, clientWidth: 300 }).start).toBe(4);
  });

  it("reads right-to-left scroll offsets, which run negative", () => {
    expect(edgeFade({ scrollLeft: -0, scrollWidth: 500, clientWidth: 300 })).toEqual({
      start: 0,
      end: MAX_FADE_PX,
    });
    expect(edgeFade({ scrollLeft: -200, scrollWidth: 500, clientWidth: 300 })).toEqual({
      start: MAX_FADE_PX,
      end: 0,
    });
  });
});
