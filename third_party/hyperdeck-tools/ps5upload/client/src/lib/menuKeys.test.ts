import { describe, expect, it } from "vitest";

import { menuKeyAction, menuKeyTarget } from "./menuKeys";

describe("menuKeyTarget", () => {
  it("moves down and wraps past the last item", () => {
    expect(menuKeyTarget("ArrowDown", 0, 3)).toBe(1);
    expect(menuKeyTarget("ArrowDown", 2, 3)).toBe(0);
  });

  it("moves up and wraps past the first item", () => {
    expect(menuKeyTarget("ArrowUp", 1, 3)).toBe(0);
    expect(menuKeyTarget("ArrowUp", 0, 3)).toBe(2);
  });

  it("jumps to either end", () => {
    expect(menuKeyTarget("Home", 2, 3)).toBe(0);
    expect(menuKeyTarget("End", 0, 3)).toBe(2);
  });

  it("enters the list from outside it", () => {
    // Focus is on the trigger (or nowhere in the menu): -1.
    expect(menuKeyTarget("ArrowDown", -1, 3)).toBe(0);
    expect(menuKeyTarget("ArrowUp", -1, 3)).toBe(2);
  });

  it("leaves every other key alone", () => {
    expect(menuKeyTarget("Enter", 0, 3)).toBeNull();
    expect(menuKeyTarget("a", 0, 3)).toBeNull();
    expect(menuKeyTarget("Tab", 0, 3)).toBeNull();
  });

  it("has nowhere to go in an empty menu", () => {
    expect(menuKeyTarget("ArrowDown", -1, 0)).toBeNull();
  });
});

describe("menuKeyAction", () => {
  it("closes on Escape and Tab", () => {
    expect(menuKeyAction("Escape", 1, 3)).toBe("close");
    expect(menuKeyAction("Tab", 0, 3)).toBe("close");
    // Even with nothing to move between.
    expect(menuKeyAction("Escape", -1, 0)).toBe("close");
  });

  it("moves like menuKeyTarget for the movement keys", () => {
    expect(menuKeyAction("ArrowDown", 2, 3)).toBe(0);
    expect(menuKeyAction("ArrowUp", 0, 3)).toBe(2);
    expect(menuKeyAction("Home", 2, 3)).toBe(0);
    expect(menuKeyAction("End", 0, 3)).toBe(2);
  });

  it("leaves Enter and Space to the focused item", () => {
    expect(menuKeyAction("Enter", 0, 3)).toBeNull();
    expect(menuKeyAction(" ", 0, 3)).toBeNull();
  });
});
