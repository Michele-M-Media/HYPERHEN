import { describe, expect, it } from "vitest";

import { isNotifRead, markAllUpTo, setOneRead } from "./ps5NotifRead";

describe("PS5 notification read state", () => {
  it("starts with everything unread", () => {
    expect(isNotifRead(undefined, 1)).toBe(false);
  });

  it("marks one read, and back", () => {
    const s = setOneRead(undefined, 5, true);
    expect(isNotifRead(s, 5)).toBe(true);
    expect(isNotifRead(s, 4)).toBe(false);
    expect(isNotifRead(setOneRead(s, 5, false), 5)).toBe(false);
  });

  it("mark all moves a watermark; later entries arrive unread", () => {
    const s = markAllUpTo(setOneRead(undefined, 2, true), 10);
    expect(isNotifRead(s, 1)).toBe(true);
    expect(isNotifRead(s, 10)).toBe(true);
    expect(isNotifRead(s, 11)).toBe(false);
    // exceptions below the watermark are folded in, so it stays small
    expect(s.read).toEqual([]);
  });

  it("can mark an old one unread again", () => {
    const s = setOneRead(markAllUpTo(undefined, 10), 3, false);
    expect(isNotifRead(s, 3)).toBe(false);
    expect(isNotifRead(s, 4)).toBe(true);
    expect(isNotifRead(setOneRead(s, 3, true), 3)).toBe(true);
  });

  it("never moves the watermark backwards", () => {
    expect(markAllUpTo(markAllUpTo(undefined, 10), 4).upTo).toBe(10);
  });
});
