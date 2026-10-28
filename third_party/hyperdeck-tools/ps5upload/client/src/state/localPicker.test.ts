import { beforeEach, describe, expect, it } from "vitest";

import { pickLocalPath, pickLocalPaths, useLocalPickerStore } from "./localPicker";

beforeEach(() => {
  useLocalPickerStore.setState({ pending: null });
});

describe("localPicker store", () => {
  it("opens a request and resolves it with the settled path", async () => {
    const p = useLocalPickerStore.getState().open({ mode: "folder" });
    const pending = useLocalPickerStore.getState().pending;
    expect(pending?.mode).toBe("folder");

    useLocalPickerStore.getState().settle("/storage/emulated/0/Download");
    await expect(p).resolves.toBe("/storage/emulated/0/Download");
    // Cleared after settling.
    expect(useLocalPickerStore.getState().pending).toBeNull();
  });

  it("resolves null when cancelled", async () => {
    const p = useLocalPickerStore.getState().open({ mode: "file" });
    useLocalPickerStore.getState().settle(null);
    await expect(p).resolves.toBeNull();
  });

  it("cancels a prior request when a new one opens (only one at a time)", async () => {
    const first = useLocalPickerStore.getState().open({ mode: "folder" });
    const second = useLocalPickerStore.getState().open({ mode: "file" });
    // The first promise resolves null immediately; the second is pending.
    await expect(first).resolves.toBeNull();
    expect(useLocalPickerStore.getState().pending?.mode).toBe("file");

    useLocalPickerStore.getState().settle("/storage/emulated/0/x.pkg");
    await expect(second).resolves.toBe("/storage/emulated/0/x.pkg");
  });

  it("pickLocalPath() is a thin wrapper over open()", async () => {
    const p = pickLocalPath({ mode: "file", title: "Pick a .pkg" });
    expect(useLocalPickerStore.getState().pending?.title).toBe("Pick a .pkg");
    useLocalPickerStore.getState().settle("/storage/emulated/0/game.pkg");
    await expect(p).resolves.toBe("/storage/emulated/0/game.pkg");
  });

  it("pickLocalPaths() asks for several and resolves every one picked", async () => {
    const p = pickLocalPaths({ mode: "file" });
    expect(useLocalPickerStore.getState().pending?.multiple).toBe(true);
    useLocalPickerStore.getState().settleMany(["/g/a.pkg", "/g/b.pkg"]);
    await expect(p).resolves.toEqual(["/g/a.pkg", "/g/b.pkg"]);
  });

  it("pickLocalPaths() resolves [] when cancelled, and one path from a single pick", async () => {
    const cancelled = pickLocalPaths({ mode: "file" });
    useLocalPickerStore.getState().settle(null);
    await expect(cancelled).resolves.toEqual([]);
    const one = pickLocalPaths({ mode: "folder" });
    useLocalPickerStore.getState().settle("/g/Game");
    await expect(one).resolves.toEqual(["/g/Game"]);
  });
});
