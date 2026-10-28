import { beforeEach, describe, expect, it, vi } from "vitest";
import { invoke } from "@tauri-apps/api/core";

vi.mock("@tauri-apps/api/core", () => ({ invoke: vi.fn() }));
vi.mock("../lib/tauriEnv", () => ({ isTauriEnv: () => true }));

import { onNotPaired } from "../lib/consoleSession";
import { jobStatus } from "./ps5";

const mockedInvoke = vi.mocked(invoke);

beforeEach(() => mockedInvoke.mockReset());

describe("a failed job opens the pairing dialog for the console that failed", () => {
  it("uses the console the engine names, not the one the job is polled against", async () => {
    // A PS5 to PS5 copy is polled with the destination, but the source is the unpaired one.
    mockedInvoke.mockResolvedValue({
      status: "failed",
      error: "not paired",
      error_reason: "ava1_not_paired",
      error_console: "10.0.0.2",
    });
    const heard: (string | undefined)[] = [];
    const off = onNotPaired((h) => heard.push(h));
    await jobStatus("j1", "10.0.0.3");
    off();
    expect(heard).toEqual(["10.0.0.2"]);
  });

  it("falls back to the polled console when the failure names none", async () => {
    mockedInvoke.mockResolvedValue({
      status: "failed",
      error: "not paired",
      error_reason: "ava1_not_paired",
    });
    const heard: (string | undefined)[] = [];
    const off = onNotPaired((h) => heard.push(h));
    await jobStatus("j1", "10.0.0.3");
    off();
    expect(heard).toEqual(["10.0.0.3"]);
  });
});
