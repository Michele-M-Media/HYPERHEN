import { describe, expect, it, vi } from "vitest";

const invoke = vi.fn(async () => {
  throw new Error("'keep_awake_state' requires the Tauri desktop client");
});
vi.mock("../lib/invokeLogged", () => ({ invoke: (...a: unknown[]) => invoke(...(a as [])) }));
vi.mock("../lib/tauriEnv", () => ({ isTauriEnv: () => false }));
vi.mock("../lib/platform", () => ({ isAndroid: () => false }));

import { useKeepAwakeStore } from "./keepAwake";

describe("keep awake in the browser build", () => {
  it("says it isn't available there, instead of surfacing the native call's error", async () => {
    await useKeepAwakeStore.getState().syncFromBackend();
    expect(useKeepAwakeStore.getState()).toMatchObject({ supported: false, lastError: null });
    await useKeepAwakeStore.getState().setEnabled(true);
    expect(useKeepAwakeStore.getState().lastError).toBeNull();
    expect(invoke).not.toHaveBeenCalled();
  });
});
