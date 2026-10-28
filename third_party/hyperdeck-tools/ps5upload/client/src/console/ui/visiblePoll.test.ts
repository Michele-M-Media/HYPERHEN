import { afterEach, describe, expect, it, vi } from "vitest";
import { pollWhileVisible, type VisibilityDoc } from "./visiblePoll";

function fakeDoc(hidden: boolean) {
  const fns = new Set<() => void>();
  const doc = {
    hidden,
    addEventListener: (_: string, fn: () => void) => fns.add(fn),
    removeEventListener: (_: string, fn: () => void) => fns.delete(fn),
  };
  return {
    doc: doc as VisibilityDoc,
    set(h: boolean) {
      doc.hidden = h;
      fns.forEach((fn) => fn());
    },
    listeners: () => fns.size,
  };
}

afterEach(() => {
  vi.useRealTimers();
});

describe("pollWhileVisible", () => {
  it("status_poll_pauses_while_hidden", () => {
    vi.useFakeTimers();
    const d = fakeDoc(false);
    const fn = vi.fn();
    const stop = pollWhileVisible(fn, 10_000, d.doc);
    expect(fn).toHaveBeenCalledTimes(1);
    vi.advanceTimersByTime(20_000);
    expect(fn).toHaveBeenCalledTimes(3);
    d.set(true);
    vi.advanceTimersByTime(60_000);
    expect(fn).toHaveBeenCalledTimes(3); // nothing while hidden
    d.set(false);
    expect(fn).toHaveBeenCalledTimes(4); // at once when shown
    vi.advanceTimersByTime(10_000);
    expect(fn).toHaveBeenCalledTimes(5);
    stop();
    vi.advanceTimersByTime(60_000);
    expect(fn).toHaveBeenCalledTimes(5);
    expect(d.listeners()).toBe(0);
  });

  it("a page opened hidden waits until it shows", () => {
    vi.useFakeTimers();
    const d = fakeDoc(true);
    const fn = vi.fn();
    pollWhileVisible(fn, 1000, d.doc);
    vi.advanceTimersByTime(5000);
    expect(fn).not.toHaveBeenCalled();
    d.set(false);
    expect(fn).toHaveBeenCalledTimes(1);
  });
});
