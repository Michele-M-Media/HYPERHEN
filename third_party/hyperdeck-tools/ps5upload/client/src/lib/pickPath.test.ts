import { beforeEach, describe, expect, it, vi } from "vitest";

// Mock the three boundaries pickPath dispatches across: platform
// detection, the in-app picker (Android), and the native dialog (desktop).
const { mockIsAndroid, mockIsTauri, mockPickLocal, mockPickLocalMany, mockOpenDialog } = vi.hoisted(() => ({
  mockIsAndroid: vi.fn(),
  mockIsTauri: vi.fn(),
  mockPickLocal: vi.fn(),
  mockPickLocalMany: vi.fn(),
  mockOpenDialog: vi.fn(),
}));

vi.mock("./platform", () => ({ isAndroid: () => mockIsAndroid() }));
vi.mock("./tauriEnv", () => ({ isTauriEnv: () => mockIsTauri() }));
vi.mock("../state/localPicker", () => ({
  pickLocalPath: (...a: unknown[]) => mockPickLocal(...a),
  pickLocalPaths: (...a: unknown[]) => mockPickLocalMany(...a),
}));
vi.mock("@tauri-apps/plugin-dialog", () => ({
  open: (...a: unknown[]) => mockOpenDialog(...a),
}));

import { pickPath, pickPaths } from "./pickPath";

beforeEach(() => {
  mockIsAndroid.mockReset();
  mockIsTauri.mockReset().mockReturnValue(true);
  mockPickLocal.mockReset();
  mockPickLocalMany.mockReset();
  mockOpenDialog.mockReset();
});

describe("pickPath on Android", () => {
  beforeEach(() => mockIsAndroid.mockReturnValue(true));

  it("routes a folder pick to the in-app browser, not the native dialog", async () => {
    mockPickLocal.mockResolvedValue("/storage/emulated/0/Download/Game");
    const p = await pickPath({ mode: "folder", title: "Pick" });
    expect(p).toBe("/storage/emulated/0/Download/Game");
    expect(mockPickLocal).toHaveBeenCalledWith({ mode: "folder", title: "Pick" });
    expect(mockOpenDialog).not.toHaveBeenCalled();
  });

  it("routes a file pick to the in-app browser", async () => {
    mockPickLocal.mockResolvedValue("/storage/emulated/0/x.pkg");
    const p = await pickPath({ mode: "file" });
    expect(p).toBe("/storage/emulated/0/x.pkg");
    expect(mockPickLocal).toHaveBeenCalledWith({ mode: "file", title: undefined });
    expect(mockOpenDialog).not.toHaveBeenCalled();
  });

  it("propagates a cancel (null)", async () => {
    mockPickLocal.mockResolvedValue(null);
    expect(await pickPath({ mode: "folder" })).toBeNull();
  });
});

describe("pickPath on desktop", () => {
  beforeEach(() => mockIsAndroid.mockReturnValue(false));

  it("uses the native dialog with directory:true for folders", async () => {
    mockOpenDialog.mockResolvedValue("/Users/me/games");
    const p = await pickPath({ mode: "folder", title: "Dest" });
    expect(p).toBe("/Users/me/games");
    expect(mockOpenDialog).toHaveBeenCalledWith({
      directory: true,
      multiple: false,
      title: "Dest",
      filters: undefined,
    });
    expect(mockPickLocal).not.toHaveBeenCalled();
  });

  it("uses directory:false + filters for files", async () => {
    mockOpenDialog.mockResolvedValue("/Users/me/p.pkg");
    const filters = [{ name: "PS5 Package", extensions: ["pkg"] }];
    await pickPath({ mode: "file", filters });
    expect(mockOpenDialog).toHaveBeenCalledWith({
      directory: false,
      multiple: false,
      title: undefined,
      filters,
    });
  });

  it("returns null when the native dialog is cancelled (non-string)", async () => {
    mockOpenDialog.mockResolvedValue(null);
    expect(await pickPath({ mode: "file" })).toBeNull();
  });
});

describe("pickPath beyond the desktop dialog", () => {
  beforeEach(() => mockIsAndroid.mockReturnValue(false));

  it("browses in-app in the web build, which has no system dialog", async () => {
    mockIsTauri.mockReturnValue(false);
    mockPickLocal.mockResolvedValue("/data/game");
    expect(await pickPath({ mode: "folder" })).toBe("/data/game");
    expect(mockOpenDialog).not.toHaveBeenCalled();
  });

  it("browses a saved server in-app and hands back its remote path", async () => {
    mockPickLocal.mockResolvedValue("remote://nas-1/games/a.pkg");
    const filters = [{ name: "PKG", extensions: ["pkg"] }];
    expect(await pickPath({ mode: "file", filters, source: { connectionId: "nas-1" } })).toBe(
      "remote://nas-1/games/a.pkg",
    );
    expect(mockPickLocal).toHaveBeenCalledWith(
      expect.objectContaining({ mode: "file", filters, source: { connectionId: "nas-1" } }),
    );
    expect(mockOpenDialog).not.toHaveBeenCalled();
  });
});

describe("pickPath with a remote engine", () => {
  beforeEach(() => mockIsAndroid.mockReturnValue(false));

  it("browses the engine's disk instead of this computer's", async () => {
    // A desktop app driving a homelab engine used to open the native dialog
    // and hand the engine `C:\...` paths it could never open.
    const { useEngineStore } = await import("../state/engine");
    useEngineStore.getState().setEngineUrl("http://192.168.1.10:19113");
    try {
      mockPickLocal.mockResolvedValue("/pkgs/game.pkg");
      const p = await pickPath({ mode: "file", title: "Pick" });
      expect(p).toBe("/pkgs/game.pkg");
      expect(mockPickLocal).toHaveBeenCalled();
      expect(mockOpenDialog).not.toHaveBeenCalled();
    } finally {
      useEngineStore.getState().setEngineUrl("http://127.0.0.1:19113");
    }
  });
});

describe("pickPaths: several at once", () => {
  it("multi-selects files in the system dialog by default", async () => {
    mockIsAndroid.mockReturnValue(false);
    mockOpenDialog.mockResolvedValue(["/g/a.exfat", "/g/b.7z"]);
    expect(await pickPaths({ title: "Pick" })).toEqual(["/g/a.exfat", "/g/b.7z"]);
    expect(mockOpenDialog).toHaveBeenCalledWith(expect.objectContaining({ directory: false, multiple: true }));
  });

  it("multi-selects folders in the system dialog", async () => {
    mockIsAndroid.mockReturnValue(false);
    mockOpenDialog.mockResolvedValue(["/g/Game A", "/g/Game B"]);
    expect(await pickPaths({ mode: "folder" })).toEqual(["/g/Game A", "/g/Game B"]);
    expect(mockOpenDialog).toHaveBeenCalledWith(expect.objectContaining({ directory: true, multiple: true }));
    expect(mockPickLocalMany).not.toHaveBeenCalled();
  });

  it("ticks folders in the in-app browser on Android and in the web build", async () => {
    mockIsAndroid.mockReturnValue(true);
    mockPickLocalMany.mockResolvedValue(["/storage/emulated/0/G"]);
    expect(await pickPaths({ mode: "folder", title: "Pick" })).toEqual(["/storage/emulated/0/G"]);
    expect(mockPickLocalMany).toHaveBeenCalledWith(expect.objectContaining({ mode: "folder", title: "Pick" }));
    mockIsAndroid.mockReturnValue(false);
    mockIsTauri.mockReturnValue(false);
    mockPickLocalMany.mockResolvedValue([]);
    expect(await pickPaths({ mode: "folder" })).toEqual([]);
    expect(mockOpenDialog).not.toHaveBeenCalled();
  });
});
