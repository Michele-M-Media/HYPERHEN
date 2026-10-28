import { afterEach, describe, expect, it, vi } from "vitest";
import { dispatchNav, NAV_ARROW_EVENT } from "./nav";

/* Node has no DOM: just what dispatchNav touches for ← / →. */
function fakeActive(marked: boolean) {
  const seen: string[] = [];
  const el = {
    hasAttribute: (a: string) => marked && a === "data-nav-arrows",
    dispatchEvent: (e: Event) => {
      seen.push(`${e.type}:${(e as CustomEvent).detail}`);
      return true;
    },
  };
  vi.stubGlobal("document", { activeElement: el, querySelectorAll: () => [] });
  return seen;
}

describe("arrows handed to the focused element", () => {
  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it("← / → on a [data-nav-arrows] element become its event, not a focus move", () => {
    const seen = fakeActive(true);
    dispatchNav("left");
    dispatchNav("right");
    expect(seen).toEqual([`${NAV_ARROW_EVENT}:left`, `${NAV_ARROW_EVENT}:right`]);
  });

  it("↑ / ↓, and any arrow on an unmarked element, move focus as before", () => {
    const seen = fakeActive(true);
    dispatchNav("up");
    expect(seen).toEqual([]);
    const plain = fakeActive(false);
    dispatchNav("left");
    expect(plain).toEqual([]);
  });
});
