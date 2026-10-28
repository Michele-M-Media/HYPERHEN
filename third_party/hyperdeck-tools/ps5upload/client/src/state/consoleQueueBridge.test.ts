import { describe, expect, it } from "vitest";
import {
  enqueueInstall,
  getInstallExecutor,
  registerInstallEnqueuer,
  registerInstallExecutor,
} from "./consoleQueueBridge";

describe("consoleQueueBridge", () => {
  it("routes enqueueInstall to the registered enqueuer", async () => {
    registerInstallEnqueuer((input) => ({
      id: "q1",
      done: Promise.resolve({ ok: true, message: input.displayName }),
    }));
    const r = enqueueInstall({
      host: "10.0.0.2",
      request: { via: "library", path: "/user/data/ps5upload/pkg_library/a.pkg" },
      displayName: "A",
    });
    expect(r.id).toBe("q1");
    await expect(r.done).resolves.toEqual({ ok: true, message: "A" });
  });

  it("throws a clear error when no queue is registered", () => {
    registerInstallEnqueuer(null as never);
    expect(() =>
      enqueueInstall({ host: "h", request: { via: "library", path: "/p" }, displayName: "x" }),
    ).toThrow(/queue is not ready/);
  });

  it("holds the executor", () => {
    const fn = async () => ({ ok: true });
    registerInstallExecutor(fn);
    expect(getInstallExecutor()).toBe(fn);
  });
});
