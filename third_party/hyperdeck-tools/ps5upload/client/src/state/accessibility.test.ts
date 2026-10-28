import { describe, expect, it } from "vitest";
import { parseAccessibility } from "./accessibility";

describe("stored accessibility settings", () => {
  it("keeps the settings that still exist when older keys are present", () => {
    const got = parseAccessibility(
      JSON.stringify({
        motion: "none",
        contrast: "high",
        dyslexia: true,
        hapticsEnabled: false,
        density: "spacious",
        screenReaderHints: true,
        colorBlindPalette: "tritanopia",
      }),
    );
    expect(got).toEqual({
      motion: "none",
      transparency: "auto",
      contrast: "high",
      dyslexia: true,
      hapticsEnabled: false,
    });
  });

  it("ignores old keys even when their values are ones no version accepted", () => {
    const got = parseAccessibility(
      JSON.stringify({ motion: "reduced", density: "huge", colorBlindPalette: 7 }),
    );
    expect(got.motion).toBe("reduced");
    expect(got).not.toHaveProperty("density");
    expect(got).not.toHaveProperty("colorBlindPalette");
    expect(got).not.toHaveProperty("screenReaderHints");
  });

  it("falls back to defaults for nothing stored or junk", () => {
    expect(parseAccessibility(null).motion).toBe("auto");
    expect(parseAccessibility("{not json").motion).toBe("auto");
  });
});

describe("transparency setting", () => {
  it("defaults to auto, keeps a stored value, and rejects garbage", () => {
    expect(parseAccessibility(null).transparency).toBe("auto");
    expect(parseAccessibility(JSON.stringify({ transparency: "reduced" })).transparency).toBe("reduced");
    expect(parseAccessibility(JSON.stringify({ transparency: "glassy" })).transparency).toBe("auto");
  });
});
