import { describe, expect, it } from "vitest";
import { isImageFolder } from "./imageDirNote";

describe("isImageFolder (a game folder sent here is still a folder)", () => {
  it("matches the exfat preset and paths ending in it", () => {
    expect(isImageFolder("exfat")).toBe(true);
    expect(isImageFolder("/exfat/")).toBe(true);
    expect(isImageFolder("games/EXFAT")).toBe(true);
  });

  it("does not match other folders", () => {
    expect(isImageFolder("homebrew")).toBe(false);
    expect(isImageFolder("exfat/games")).toBe(false);
    expect(isImageFolder("myexfat")).toBe(false);
    expect(isImageFolder("")).toBe(false);
  });
});
