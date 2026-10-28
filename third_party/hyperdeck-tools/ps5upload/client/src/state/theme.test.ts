import { describe, expect, it } from "vitest";

import { isDarkTheme, nextTheme, normalizeTheme, parsePair } from "./theme";

describe("nextTheme (the sun/moon toggle)", () => {
  it("flips between plain light and dark by default", () => {
    expect(nextTheme("dark", { light: "light", dark: "dark" })).toBe("light");
    expect(nextTheme("light", { light: "light", dark: "dark" })).toBe("dark");
  });

  it("goes back to the dark and light the user picked", () => {
    // An OLED user flipping to light and back must land on OLED, not Dark.
    const pair = { light: "peach", dark: "oled" } as const;
    expect(nextTheme("oled", pair)).toBe("peach");
    expect(nextTheme("peach", pair)).toBe("oled");
    expect(nextTheme("light", pair)).toBe("oled");
    expect(nextTheme("dark", pair)).toBe("peach");
  });
});

describe("isDarkTheme", () => {
  it("knows which side each theme is on", () => {
    expect(isDarkTheme("dark")).toBe(true);
    expect(isDarkTheme("oled")).toBe(true);
    expect(isDarkTheme("light")).toBe(false);
    expect(isDarkTheme("peach")).toBe(false);
  });
});

describe("normalizeTheme (stored values, including older versions)", () => {
  it("keeps the four themes", () => {
    for (const t of ["dark", "light", "oled", "peach"]) expect(normalizeTheme(t)).toBe(t);
  });

  it("reads 6.x's Rose as Peach, the warm light theme", () => {
    expect(normalizeTheme("rose")).toBe("peach");
  });

  it("returns null for anything else", () => {
    expect(normalizeTheme(null)).toBeNull();
    expect(normalizeTheme("garbage")).toBeNull();
    expect(normalizeTheme(3)).toBeNull();
  });
});

describe("parsePair (the remembered light and dark)", () => {
  it("reads a stored pair and falls back per side", () => {
    expect(parsePair("peach,oled")).toEqual({ light: "peach", dark: "oled" });
    expect(parsePair("oled,peach")).toEqual({ light: "light", dark: "dark" });
    expect(parsePair(null)).toEqual({ light: "light", dark: "dark" });
    expect(parsePair("peach")).toEqual({ light: "peach", dark: "dark" });
  });
});
