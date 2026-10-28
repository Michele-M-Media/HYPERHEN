import { describe, expect, it, vi } from "vitest";

import {
  checkSaysRest,
  consoleLight,
  consoleLightColor,
  isRestModeReason,
  keepAwakeTargets,
  powerFromCheck,
} from "./consolePower";
import { autoRedeployDecision } from "./autoRedeploy";
import { isAutoRecoverable, PostUploadStepError, shouldParkForWake } from "./uploadRecovery";
import { consoleLightLabel } from "./consoleLightLabel";

const down = { reachable: false, engineReachable: true, session: "down" };

describe("power derives from the status reply", () => {
  it("reads the engine's power field", () => {
    expect(powerFromCheck({ ...down, power: "rest" } as never)).toBe("rest");
    expect(powerFromCheck({ ...down, power: "unreachable" } as never)).toBe("unreachable");
    expect(powerFromCheck({ ...down, power: "awake" } as never)).toBe("awake");
  });

  it("reads rest mode from the reason token when the field is missing", () => {
    expect(powerFromCheck({ ...down, reason: "console_rest_mode" } as never)).toBe("rest");
  });

  it("a helper that answers (or wants pairing) means awake, whatever else is said", () => {
    expect(powerFromCheck({ reachable: true, engineReachable: true, session: "connected" })).toBe(
      "awake",
    );
    expect(
      powerFromCheck({ ...down, session: "needs_pairing", power: "rest" } as never),
    ).toBe("awake");
  });

  it("a refusal without a verdict is unknown, not rest", () => {
    expect(powerFromCheck({ ...down, reason: "helper_not_ava1" } as never)).toBe("unknown");
    expect(powerFromCheck({ ...down, reason: "ava1_unreachable" } as never)).toBe("unknown");
    expect(powerFromCheck(down)).toBe("unknown");
    // A value the client does not know is ignored, not trusted.
    expect(powerFromCheck({ ...down, power: "sleepy" } as never)).toBe("unknown");
  });

  it("says nothing when the engine itself did not answer", () => {
    expect(powerFromCheck({ ...down, engineReachable: false, power: "rest" } as never)).toBeNull();
    expect(powerFromCheck(null)).toBeNull();
  });

  it("matches the job failure token", () => {
    expect(isRestModeReason("console_rest_mode")).toBe(true);
    expect(isRestModeReason("ava1_unreachable")).toBe(false);
    expect(isRestModeReason(null)).toBe(false);
  });
});

describe("status light", () => {
  it("shows rest mode calmly, apart from a plain down", () => {
    expect(consoleLight({ payloadStatus: "down", power: "rest" })).toBe("rest");
    expect(consoleLight({ payloadStatus: "down", power: "unreachable" })).toBe("unreachable");
    expect(consoleLight({ payloadStatus: "down", power: "unknown" })).toBe("down");
    expect(consoleLight({ payloadStatus: "up", power: "rest" })).toBe("up");
    expect(consoleLight({ payloadStatus: "unknown", power: "rest" })).toBe("unknown");
    expect(consoleLight({ payloadStatus: "up", session: "needs_pairing" })).toBe("pairing");
    expect(consoleLightColor("rest")).toBe("var(--color-accent)");
    expect(consoleLightColor("down")).toBe("var(--color-bad)");
  });

  it("names each state in the tooltip", () => {
    const tr = ((_k: string, _v: unknown, fb: string) => fb) as never;
    expect(consoleLightLabel("rest", tr)).toBe("In rest mode");
    expect(consoleLightLabel("unreachable", tr)).toBe("Not answering");
    expect(consoleLightLabel("down", tr)).toBe("Not connected");
    expect(consoleLightLabel("up", tr)).toBe("Connected");
  });
});

describe("the reconnect loop holds while the console sleeps", () => {
  it("holds on rest, and runs its normal flow once awake", () => {
    const base = { status: "down" as const, busy: false, delivered: 0 };
    expect(autoRedeployDecision({ ...base, power: "rest" })).toBe("hold");
    expect(autoRedeployDecision({ ...base, power: "awake" })).toBe("redeploy");
    expect(autoRedeployDecision({ ...base, power: "unknown" })).toBe("redeploy");
    expect(autoRedeployDecision({ ...base, power: "unreachable" })).toBe("redeploy");
    // The helper came back on its own: re-arm as before.
    expect(autoRedeployDecision({ ...base, status: "up", power: "rest" })).toBe("rearm");
  });
});

describe("keep-awake skips sleeping consoles", () => {
  it("ticks only consoles that are not in rest mode", () => {
    const runtimes = {
      "10.0.0.1": { power: "rest" as const },
      "10.0.0.2": { power: "awake" as const },
    };
    expect(keepAwakeTargets(["10.0.0.1", "10.0.0.2", "10.0.0.3"], runtimes)).toEqual([
      "10.0.0.2",
      "10.0.0.3",
    ]);
  });
});

describe("Connection's Check on a closed loader port", () => {
  it("says rest mode from the status reply without asking DDP", async () => {
    const ask = vi.fn(async () => 200);
    expect(await checkSaysRest({ ...down, power: "rest" } as never, ask)).toBe(true);
    expect(ask).not.toHaveBeenCalled();
  });

  it("asks DDP when the reply has no verdict: 620 is rest, anything else is not", async () => {
    expect(await checkSaysRest(down, async () => 620)).toBe(true);
    expect(await checkSaysRest(down, async () => 200)).toBe(false);
    expect(
      await checkSaysRest(null, async () => {
        throw new Error("no reply");
      }),
    ).toBe(false);
  });

  it("a helper that answers is never rest", async () => {
    const ask = vi.fn(async () => 620);
    expect(
      await checkSaysRest({ reachable: true, engineReachable: true, session: "connected" }, ask),
    ).toBe(false);
    expect(ask).not.toHaveBeenCalled();
  });
});

describe("uploads wait for the wake", () => {
  it("parks on the rest-mode reason in either build", () => {
    expect(shouldParkForWake(new Error("x"), "console_rest_mode", "x", false)).toBe(true);
    // And it resumes on wake in the browser build too (nothing about it needs a helper send).
    expect(isAutoRecoverable("console_rest_mode", "x", { canSendHelper: false })).toBe(true);
  });

  it("parks a recoverable failure while the console reads rest", () => {
    expect(shouldParkForWake(new Error("reset"), null, "connection reset", true)).toBe(true);
    // The browser build cannot send the helper, but a sleeping console is still a wait.
    expect(shouldParkForWake(new Error("x"), "helper_not_ava1", "x", true)).toBe(true);
    expect(shouldParkForWake(new Error("reset"), null, "connection reset", false)).toBe(false);
  });

  it("never parks what a wake cannot fix, or work already committed", () => {
    expect(shouldParkForWake(new Error("x"), "fs_write_failed_errno_28", "x", true)).toBe(false);
    expect(shouldParkForWake(new PostUploadStepError("install"), "console_rest_mode", "x", true)).toBe(
      false,
    );
  });
});
