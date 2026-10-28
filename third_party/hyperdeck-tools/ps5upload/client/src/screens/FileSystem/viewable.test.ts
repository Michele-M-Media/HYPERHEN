import { describe, expect, it, vi } from "vitest";

vi.mock("@tauri-apps/api/core", () => ({ invoke: vi.fn() }));

import { viewableEntry } from "./index";

describe("File System rows the viewer can open", () => {
  it("offers packages, game images and folders named like a game", () => {
    expect(viewableEntry("a.pkg", false)).toBe(true);
    expect(viewableEntry("PPSA01234.exfat", false)).toBe(true);
    expect(viewableEntry("x.ffpfsc", false)).toBe(true);
    expect(viewableEntry("PPSA01234-app", true)).toBe(true);
    expect(viewableEntry("notes.txt", false)).toBe(false);
    expect(viewableEntry("savedata", true)).toBe(false);
  });
});
