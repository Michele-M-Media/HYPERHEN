import { describe, expect, it, vi, beforeEach } from "vitest";

vi.mock("@tauri-apps/api/core", () => ({
  convertFileSrc: vi.fn((p: string) => `asset://${p}`),
}));
vi.mock("./tauriEnv", () => ({ isTauriEnv: vi.fn() }));
vi.mock("../state/engine", () => ({ getEngineUrl: () => "http://engine:19113" }));

import { localFileSrc } from "./fileSrc";
import { isTauriEnv } from "./tauriEnv";
import { convertFileSrc } from "@tauri-apps/api/core";

describe("localFileSrc", () => {
  beforeEach(() => vi.mocked(convertFileSrc).mockClear());

  it("converts a path inside Tauri", () => {
    vi.mocked(isTauriEnv).mockReturnValue(true);
    expect(localFileSrc("/tmp/icon0.png")).toBe("asset:///tmp/icon0.png");
  });

  it("serves a game cover from the engine in the browser", () => {
    vi.mocked(isTauriEnv).mockReturnValue(false);
    expect(
      localFileSrc("/data/games/G/sce_sys/icon0.png"),
    ).toBe(
      "http://engine:19113/api/local/game-icon?path=%2Fdata%2Fgames%2FG%2Fsce_sys%2Ficon0.png",
    );
    expect(convertFileSrc).not.toHaveBeenCalled();
  });

  it("returns null in the browser for any other path instead of throwing", () => {
    // The #271 regression: convertFileSrc reads a Tauri global that does
    // not exist in the web UI, so calling it there threw and took the
    // upload screen down as soon as a folder was picked.
    vi.mocked(isTauriEnv).mockReturnValue(false);
    expect(localFileSrc("/tmp/icon0.png")).toBeNull();
    expect(localFileSrc("/etc/passwd")).toBeNull();
    expect(convertFileSrc).not.toHaveBeenCalled();
  });

  it("returns null for an absent path without calling into Tauri", () => {
    vi.mocked(isTauriEnv).mockReturnValue(true);
    expect(localFileSrc(null)).toBeNull();
    expect(localFileSrc(undefined)).toBeNull();
    expect(localFileSrc("")).toBeNull();
    expect(convertFileSrc).not.toHaveBeenCalled();
  });
});
