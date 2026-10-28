import { beforeEach, describe, expect, it, vi } from "vitest";

import { useConnectionStore } from "../../state/connection";
import { useRunningAppsStore } from "../../state/runningApps";
import { captureHostProbe } from "../../lib/staleHostGuard";
import type { RunningGame } from "../../lib/runningGames";
import { readRunningApps, refreshRunningApps, type RunningAppsDeps } from "./runningAppsRead";

const ADDR = "192.168.1.50:9200";

function deps(over: Partial<RunningAppsDeps> = {}): RunningAppsDeps {
  return {
    appListRunning: async () => ({ ok: true, apps: [{ app_id: 7 }] }),
    fetchRunningGames: async () =>
      new Map<string, RunningGame>([["PPSA01234", { titleId: "PPSA01234", appId: 7, pid: 70 }]]),
    ...over,
  };
}

beforeEach(() => {
  useConnectionStore.setState({ host: "192.168.1.50" });
  useRunningAppsStore.setState({ titleIds: new Set(["CUSA00001"]), host: "192.168.1.50", updatedAtMs: 1 });
});

describe("readRunningApps", () => {
  it("joins against app.db when it answers", async () => {
    const r = await readRunningApps(
      ADDR,
      async () => [{ app_id: 7, title_id: "PPSA09999", name: "Game" }],
      deps(),
    );
    expect(r.rows).toEqual([{ app_id: 7, title_id: "PPSA09999", name: "Game" }]);
    expect(r.titleIds).toEqual(["PPSA09999"]);
  });

  it("falls back to the process list when app.db fails (or there is none, as in the browser)", async () => {
    const failing = await readRunningApps(ADDR, async () => Promise.reject(new Error("no route")), deps());
    expect(failing.titleIds).toEqual(["PPSA01234"]);
    expect(failing.rows[0].title_id).toBe("PPSA01234");
    const browser = await readRunningApps(ADDR, null, deps());
    expect(browser.titleIds).toEqual(["PPSA01234"]);
  });

  it("reports unknown (null), not empty, when no title source answers", async () => {
    const r = await readRunningApps(
      ADDR,
      null,
      deps({ fetchRunningGames: async () => Promise.reject(new Error("down")) }),
    );
    expect(r.titleIds).toBeNull();
    expect(r.rows).toEqual([{ app_id: 7, title_id: null, name: null }]);
  });
});

describe("refreshRunningApps", () => {
  it("keeps the shared running set when the title read failed", async () => {
    const onRows = vi.fn();
    await refreshRunningApps({
      addr: ADDR,
      probe: captureHostProbe(),
      appdb: null,
      onRows,
      deps: deps({ fetchRunningGames: async () => Promise.reject(new Error("down")) }),
    });
    expect(onRows).toHaveBeenCalled();
    expect([...useRunningAppsStore.getState().titleIds]).toEqual(["CUSA00001"]);
  });

  it("drops a reply that lands after a console switch", async () => {
    const onRows = vi.fn();
    const probe = captureHostProbe();
    const pending = refreshRunningApps({
      addr: ADDR,
      probe,
      appdb: null,
      onRows,
      deps: deps(),
    });
    useConnectionStore.setState({ host: "10.0.0.2" });
    await pending;
    expect(onRows).not.toHaveBeenCalled();
    expect([...useRunningAppsStore.getState().titleIds]).toEqual(["CUSA00001"]);
  });

  it("publishes the running set on a good read", async () => {
    const onRows = vi.fn();
    await refreshRunningApps({ addr: ADDR, probe: captureHostProbe(), appdb: null, onRows, deps: deps() });
    expect([...useRunningAppsStore.getState().titleIds]).toEqual(["PPSA01234"]);
  });
});
