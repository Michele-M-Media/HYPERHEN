import { describe, expect, it } from "vitest";
import type { LibraryEntry } from "../../api/ps5";
import { shownGamePath } from "./gamePath";

const entry = (kind: "game" | "image", path: string): LibraryEntry =>
  ({ kind, name: path.split("/").pop() ?? "", path, volume: "", scope: "", size: 0 }) as LibraryEntry;

describe("shownGamePath", () => {
  // A FW 13.60 Phat: Wolverine registered by ShadowMount+ from an image, with an old folder copy.
  const entries = [
    entry("game", "/data/homebrew/PPSA03671-app"),
    entry("image", "/mnt/ext0/homebrew/PPSA03671-app.ffpfsc"),
    entry("image", "/data/homebrew/PPSA11386.exfat"),
  ];

  it("shows the image a ShadowMount+ title runs from, not a folder copy elsewhere (#452)", () => {
    const app = { source: "/mnt/shadowmnt/PPSA03671-app_36697d91", imageBacked: true };
    expect(shownGamePath("/data/homebrew/PPSA03671-app", app, entries)).toBe(
      "/mnt/ext0/homebrew/PPSA03671-app.ffpfsc",
    );
  });

  it("uses the mount map for ps5upload's own mounts", () => {
    const app = { source: "/mnt/ps5upload/PPSA11386", imageBacked: true };
    const mounts = new Map([["/mnt/usb0/games/Overdrive.exfat", "/mnt/ps5upload/PPSA11386"]]);
    expect(shownGamePath(undefined, app, entries, mounts)).toBe("/mnt/usb0/games/Overdrive.exfat");
  });

  it("falls back to the mount point, the registered folder, then the library row", () => {
    expect(shownGamePath(undefined, { source: "/mnt/shadowmnt/PPSA99058_2b22537e", imageBacked: true }, entries)).toBe(
      "/mnt/shadowmnt/PPSA99058_2b22537e",
    );
    expect(shownGamePath("/data/homebrew/X", { source: "/mnt/ext0/homebrew/X/", imageBacked: false }, entries)).toBe(
      "/mnt/ext0/homebrew/X",
    );
    expect(shownGamePath("/data/homebrew/X", { source: "", imageBacked: false }, entries)).toBe("/data/homebrew/X");
    expect(shownGamePath(undefined, undefined, null)).toBe("");
  });
});
