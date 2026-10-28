import { afterEach, describe, expect, it, vi } from "vitest";

vi.mock("../state/engine", () => ({ getEngineUrl: () => "http://engine.test:19113" }));

import {
  pairingCancel,
  pairingConfirm,
  pairingForget,
  pairingStatus,
  sameConsole,
  startPs5ToPs5,
} from "./ava1";

const reply = (status: number, body: unknown) =>
  new Response(JSON.stringify(body), { status });

afterEach(() => vi.unstubAllGlobals());

describe("pairing routes", () => {
  it("asks the engine by bare host; no code comes back, the user reads it off the console", async () => {
    const f = vi.fn().mockResolvedValue(
      reply(200, { state: "code", console_name: "PS5-Pro" }),
    );
    vi.stubGlobal("fetch", f);
    const v = await pairingStatus("10.0.0.2:9113");
    expect(v).toEqual({ state: "code", consoleName: "PS5-Pro" });
    const [url, init] = f.mock.calls[0];
    expect(url).toBe("http://engine.test:19113/api/ava1/pairing/start");
    expect(init.method).toBe("POST");
    expect(JSON.parse(init.body)).toEqual({ addr: "10.0.0.2" });
  });

  it("posts the typed code and maps the closed window", async () => {
    const f = vi.fn().mockResolvedValue(reply(200, { state: "closed" }));
    vi.stubGlobal("fetch", f);
    expect(await pairingConfirm("10.0.0.2", "004821")).toEqual({ state: "closed" });
    const [url, init] = f.mock.calls[0];
    expect(url).toBe("http://engine.test:19113/api/ava1/pairing/confirm");
    expect(JSON.parse(init.body)).toEqual({ addr: "10.0.0.2", code: "004821" });
  });

  it("maps a wrong code and a different console", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue(
        reply(200, { state: "wrong_code", console_name: "PS5" }),
      ),
    );
    expect(await pairingConfirm("10.0.0.2", "1")).toEqual({
      state: "wrong_code",
      consoleName: "PS5",
    });
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue(reply(200, { state: "wrong_console" })),
    );
    expect(await pairingStatus("10.0.0.2")).toEqual({ state: "wrong_console" });
  });

  it("cancels and forgets by bare host", async () => {
    const f = vi.fn().mockResolvedValue(reply(200, { ok: true }));
    vi.stubGlobal("fetch", f);
    await pairingCancel("10.0.0.2:9114");
    await pairingForget("10.0.0.2");
    expect(f.mock.calls[0][0]).toBe(
      "http://engine.test:19113/api/ava1/pairing/cancel",
    );
    expect(f.mock.calls[1][0]).toBe(
      "http://engine.test:19113/api/ava1/pairing/forget",
    );
    expect(JSON.parse(f.mock.calls[0][1].body)).toEqual({ addr: "10.0.0.2" });
    expect(JSON.parse(f.mock.calls[1][1].body)).toEqual({ addr: "10.0.0.2" });
  });

  it("surfaces an unreachable console as an error", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue(reply(502, { state: "none", error: "timed out" })),
    );
    await expect(pairingStatus("10.0.0.2")).rejects.toThrow("timed out");
  });
});

describe("PS5 to PS5", () => {
  it("starts the relay with both consoles as bare hosts and returns the job", async () => {
    const f = vi.fn().mockResolvedValue(reply(202, { job_id: "j1" }));
    vi.stubGlobal("fetch", f);
    expect(
      await startPs5ToPs5("10.0.0.2:9114", "/data/a", "10.0.0.3", "/data/b"),
    ).toBe("j1");
    expect(JSON.parse(f.mock.calls[0][1].body)).toEqual({
      from: "10.0.0.2",
      src: "/data/a",
      to: "10.0.0.3",
      dest: "/data/b",
      tx_id: null,
      route: "auto",
      direct_addr: null,
    });
  });

  it("throws the engine's reason when the relay refuses", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue(reply(400, { error: "different consoles" })),
    );
    await expect(startPs5ToPs5("a", "/x", "a", "/y")).rejects.toThrow(
      "different consoles",
    );
  });
});

describe("sameConsole (a console whose address changed)", () => {
  it("asks the engine with both bare hosts and reads its verdict", async () => {
    const f = vi.fn().mockResolvedValue(reply(200, { ok: true, result: "same" }));
    vi.stubGlobal("fetch", f);
    expect(await sameConsole("192.168.137.2:9113", "192.168.2.36")).toBe("same");
    expect(f.mock.calls[0][0]).toBe("http://engine.test:19113/api/ava1/same-console");
    expect(JSON.parse(f.mock.calls[0][1].body)).toEqual({
      old: "192.168.137.2",
      new: "192.168.2.36",
    });
  });

  it("reads anything it cannot use as unknown, never as same", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(reply(200, { result: "maybe" })));
    expect(await sameConsole("10.0.0.2", "10.0.0.3")).toBe("unknown");
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(reply(404, {})));
    expect(await sameConsole("10.0.0.2", "10.0.0.3")).toBe("unknown");
    vi.stubGlobal("fetch", vi.fn().mockRejectedValue(new Error("offline")));
    expect(await sameConsole("10.0.0.2", "10.0.0.3")).toBe("unknown");
  });
});
