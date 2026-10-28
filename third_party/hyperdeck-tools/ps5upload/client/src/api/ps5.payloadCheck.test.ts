import { beforeEach, describe, expect, it, vi } from "vitest";
import { invoke } from "@tauri-apps/api/core";
import { payloadCheck } from "./ps5";

vi.mock("@tauri-apps/api/core", () => ({
  invoke: vi.fn(),
}));
vi.mock("../lib/tauriEnv", () => ({ isTauriEnv: () => true }));

const mockedInvoke = vi.mocked(invoke);

/** The status probe's failure carries the engine's `reason` and `power` (rest mode design). */
describe("payloadCheck reason and power", () => {
  beforeEach(() => mockedInvoke.mockReset());

  it("passes a rest-mode verdict through", async () => {
    mockedInvoke.mockResolvedValue({
      ok: false,
      reachable: false,
      engine: true,
      error: '{"error":"x","reason":"console_rest_mode","power":"rest"}',
      reason: "console_rest_mode",
      power: "rest",
    });
    const r = await payloadCheck("10.0.0.2");
    expect(r.reachable).toBe(false);
    expect(r.session).toBe("down");
    expect(r.reason).toBe("console_rest_mode");
    expect(r.power).toBe("rest");
  });

  it("drops an unknown power value and leaves both out when absent", async () => {
    mockedInvoke.mockResolvedValue({
      ok: false,
      reachable: false,
      engine: true,
      error: "x",
      reason: "ava1_unreachable",
      power: "asleep",
    });
    const a = await payloadCheck("10.0.0.2");
    expect(a.reason).toBe("ava1_unreachable");
    expect("power" in a).toBe(false);

    mockedInvoke.mockResolvedValue({ ok: false, reachable: false, engine: true, error: "x" });
    const b = await payloadCheck("10.0.0.2");
    expect("reason" in b || "power" in b).toBe(false);
  });

  it("has no reason or power when the console answered", async () => {
    mockedInvoke.mockResolvedValue({
      ok: true,
      reachable: true,
      engine: true,
      status: { version: "7.0.30" },
    });
    const r = await payloadCheck("10.0.0.2");
    expect(r.reachable).toBe(true);
    expect("reason" in r || "power" in r).toBe(false);
  });
});
