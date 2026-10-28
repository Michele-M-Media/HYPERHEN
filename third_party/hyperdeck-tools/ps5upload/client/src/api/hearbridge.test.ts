import { afterEach, describe, expect, it, vi } from "vitest";

import { hbAct, hbOverview, parseHbStatus, parseSaved, parseScanned } from "./hearbridge";

// A /api/status body as HearBridge's status_json writes it (token already removed by the
// engine), trimmed to the fields the app reads plus a few it ignores.
const FULL = {
  version: "1.9.0",
  connected: 1,
  state: "streaming",
  device: "WH-1000XM4",
  gain_pct: 140,
  muted: 0,
  codec: "SBC",
  latency: { target_ms: 120, estimate_ms: 185, capture_ms: 10 },
  lat_extra: { backoff_ms: 0, normal_ms: 120, auto: 1, auto_ms: 110 },
  battery: { status: "ok", level: 3, pct: 70, src: "hf", none: 0 },
  link: { score: 88, rssi: -54, lq: 255, drops_min: 0 },
  night: { on: 1, db10: 60 },
  game: { avail: 1, id: "PPSA01234", name: "Astro Bot", profile: 1, active: 1 },
  events: ["Connected", "Streaming"],
  chip: { vid: "0e8d", vendor: "MediaTek" },
};

describe("HearBridge status", () => {
  it("reads every field the card and panel show", () => {
    expect(parseHbStatus(FULL)).toEqual({
      version: "1.9.0",
      state: "streaming",
      device: "WH-1000XM4",
      gainPct: 140,
      muted: false,
      codec: "SBC",
      latencyTargetMs: 120,
      latencyEstimateMs: 185,
      latencyAuto: true,
      batteryPct: 70,
      linkScore: 88,
      night: true,
      gameName: "Astro Bot",
      gameProfile: true,
      events: ["Connected", "Streaming"],
    });
  });

  it("leaves out what an older HearBridge doesn't send, never guessing", () => {
    const s = parseHbStatus({ version: "0.3", state: "disconnected" });
    expect(s.gainPct).toBeNull();
    expect(s.muted).toBeNull();
    expect(s.latencyAuto).toBeNull();
    expect(s.batteryPct).toBeNull();
    expect(s.events).toEqual([]);
    // -1 is HearBridge's "unknown".
    expect(parseHbStatus({ battery: { pct: -1 }, link: { score: -1 } }).batteryPct).toBeNull();
    expect(parseHbStatus(null).version).toBe("");
    expect(parseHbStatus({ events: [1, "x", null] }).events).toEqual(["x"]);
  });

  it("reads saved and scanned headsets, dropping malformed rows", () => {
    expect(
      parseSaved({ devices: [{ addr: "AA:BB:CC:DD:EE:FF", name: "Buds", kind: "audio", current: 1 }, { name: "no addr" }] }),
    ).toEqual([{ addr: "AA:BB:CC:DD:EE:FF", name: "Buds", kind: "audio", current: true }]);
    expect(
      parseScanned({ version: 1, devices: [{ index: 2, addr: "11:22:33:44:55:66", name: "Pods", rssi: null }, { addr: "x" }] }),
    ).toEqual([{ index: 2, addr: "11:22:33:44:55:66", name: "Pods", kind: "", rssi: null }]);
    expect(parseSaved("garbage")).toEqual([]);
  });
});

describe("HearBridge through the engine", () => {
  afterEach(() => vi.unstubAllGlobals());

  it("reports a console without HearBridge as absent, not an error", async () => {
    const fetchMock = vi.fn(async () => new Response(JSON.stringify({ ok: true, present: false, error: "no answer" })));
    vi.stubGlobal("fetch", fetchMock);
    await expect(hbOverview("192.168.1.5")).resolves.toEqual({ present: false, error: "no answer" });
    expect(String((fetchMock.mock.calls[0] as unknown[])[0])).toContain("/api/ps5/hearbridge/status?host=192.168.1.5");
  });

  it("posts one change with the console and returns the new status", async () => {
    const fetchMock = vi.fn(
      async () => new Response(JSON.stringify({ ok: true, result: { ...FULL, gain_pct: 200 } })),
    );
    vi.stubGlobal("fetch", fetchMock);
    const s = await hbAct("192.168.1.5:9120", { action: "volume", pct: 200 });
    expect(s?.gainPct).toBe(200);
    const [url, init] = fetchMock.mock.calls[0] as unknown as [string, RequestInit];
    expect(url).toContain("/api/ps5/hearbridge/action");
    expect(JSON.parse(String(init.body))).toEqual({ host: "192.168.1.5", action: "volume", pct: 200 });
  });

  it("returns no status for an action HearBridge answers with {ok:1}", async () => {
    vi.stubGlobal("fetch", vi.fn(async () => new Response(JSON.stringify({ ok: true, result: { ok: 1 } }))));
    await expect(hbAct("10.0.0.2", { action: "reconnect" })).resolves.toBeNull();
  });

  it("surfaces the engine's error", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn(async () => new Response(JSON.stringify({ error: "HearBridge refused" }), { status: 502 })),
    );
    await expect(hbAct("10.0.0.2", { action: "stop" })).rejects.toThrow("HearBridge refused");
  });
});
