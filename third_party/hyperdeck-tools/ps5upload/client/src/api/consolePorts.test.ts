import { afterEach, describe, expect, it, vi } from "vitest";

vi.mock("../state/engine", () => ({ getEngineUrl: () => "http://engine.test:19113" }));

import { consolePorts, isOutgoing, portLink, type ConsolePort } from "./consolePorts";

const reply = (status: number, body: unknown) => new Response(JSON.stringify(body), { status });

afterEach(() => vi.unstubAllGlobals());

describe("ports in use on the console", () => {
  it("asks the engine by bare host and returns the list", async () => {
    const ports = [{ port: 9021, proto: "tcp", scope: "all", name: "elfldr (payload loader)" }];
    const f = vi.fn().mockResolvedValue(reply(200, { ports, elapsed_ms: 890 }));
    vi.stubGlobal("fetch", f);
    const r = await consolePorts("10.0.0.2:9120");
    expect(f.mock.calls[0][0]).toBe("http://engine.test:19113/api/ps5/net/ports?addr=10.0.0.2");
    expect(r.ports).toEqual(ports);
    expect(r.elapsed_ms).toBe(890);
  });

  it("says why it failed (an older helper has no net.ports)", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(reply(502, { error: "unknown method" })));
    await expect(consolePorts("10.0.0.2")).rejects.toThrow("unknown method");
  });

  it("links only TCP ports a PC can reach", () => {
    const p = (port: number, proto: "tcp" | "udp", scope: ConsolePort["scope"]): ConsolePort => ({
      port,
      proto,
      scope,
    });
    expect(portLink("10.0.0.2:9120", p(8084, "tcp", "all"))).toBe("http://10.0.0.2:8084/");
    expect(portLink("10.0.0.2", p(53, "udp", "all"))).toBeNull();
    expect(portLink("10.0.0.2", { ...p(9021, "tcp", "all"), web: false })).toBeNull();
    expect(portLink("10.0.0.2", { ...p(8084, "tcp", "all"), web: true })).toBe("http://10.0.0.2:8084/");
    expect(portLink("10.0.0.2", p(41800, "tcp", "console"))).toBeNull();
    expect(isOutgoing(p(49464, "udp", "lan"))).toBe(true);
    expect(isOutgoing(p(2121, "tcp", "all"))).toBe(false);
  });
});
