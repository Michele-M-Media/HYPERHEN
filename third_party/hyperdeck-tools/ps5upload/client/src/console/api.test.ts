import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

type Api = typeof import("./api");

interface Seen {
  url: string;
  init?: RequestInit;
}

function jsonResponse(status: number, body: unknown): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

/** A fetch stand-in: /api/token answers a fixed token, other routes `routeReply`. */
function installFetch(routeReply: () => Response): Seen[] {
  const seen: Seen[] = [];
  globalThis.fetch = (async (input: unknown, init?: RequestInit) => {
    const url = String(input);
    seen.push({ url, init });
    if (url === "/api/token") return jsonResponse(200, { token: "ab".repeat(32) });
    return routeReply();
  }) as typeof fetch;
  return seen;
}

let api: Api;
const realFetch = globalThis.fetch;

beforeEach(async () => {
  vi.resetModules();
  api = await import("./api");
});
afterEach(() => {
  globalThis.fetch = realFetch;
});

describe("call", () => {
  it("call_sends_token_header", async () => {
    const seen = installFetch(() => jsonResponse(200, { ok: 1 }));
    const got = await api.call<{ ok: number }>("/api/status", { a: 1 });
    expect(got).toEqual({ ok: 1 });
    const post = seen.find((s) => s.url === "/api/status");
    expect(post?.init?.method).toBe("POST");
    const headers = new Headers(post?.init?.headers);
    expect(headers.get("X-PS5U-Token")).toBe("ab".repeat(32));
    expect(post?.init?.body).toBe(JSON.stringify({ a: 1 }));
  });

  it("fetches the token once for many calls", async () => {
    const seen = installFetch(() => jsonResponse(200, {}));
    await api.call("/api/status");
    await api.call("/api/volumes");
    expect(seen.filter((s) => s.url === "/api/token")).toHaveLength(1);
  });

  it("call_401_triggers_token_expired", async () => {
    installFetch(() => jsonResponse(401, { error: "token" }));
    const fn = vi.fn();
    const off = api.onTokenExpired(fn);
    await expect(api.call("/api/status")).rejects.toMatchObject({ status: 401, code: "token" });
    expect(fn).toHaveBeenCalledTimes(1);
    off();
    await expect(api.call("/api/status")).rejects.toBeTruthy();
    expect(fn).toHaveBeenCalledTimes(1);
  });

  it("503 stopping reads as offline, an API error keeps its code", async () => {
    installFetch(() => jsonResponse(503, { error: "stopping" }));
    const err = await api.call("/api/status").catch((e: unknown) => e);
    expect(api.isOffline(err)).toBe(true);

    installFetch(() => jsonResponse(400, { error: "path_not_allowed" }));
    const err2 = await api.call("/api/fs/list").catch((e: unknown) => e);
    expect(err2).toMatchObject({ status: 400, code: "path_not_allowed" });
    expect(api.isOffline(err2)).toBe(false);
  });

  it("a network failure is offline and the token is fetched again next time", async () => {
    let down = true;
    const seen: string[] = [];
    globalThis.fetch = (async (input: unknown) => {
      seen.push(String(input));
      if (down) throw new TypeError("Load failed");
      if (String(input) === "/api/token") return jsonResponse(200, { token: "cd".repeat(32) });
      return jsonResponse(200, { up: true });
    }) as typeof fetch;
    const err = await api.call("/api/status").catch((e: unknown) => e);
    expect(api.isOffline(err)).toBe(true);
    down = false;
    await expect(api.call("/api/status")).resolves.toEqual({ up: true });
    expect(seen.filter((u) => u === "/api/token")).toHaveLength(2);
  });
});

describe("probeHelper (is the helper there, or is this the offline copy?)", () => {
  it("true when the token arrives, and the token is kept for the calls after", async () => {
    const seen = installFetch(() => jsonResponse(200, { ok: true }));
    expect(await api.probeHelper()).toBe(true);
    await api.call("/api/status");
    expect(seen.filter((s) => s.url === "/api/token")).toHaveLength(1);
  });

  it("false when nothing answers, when it is stopping, or when the answer is not the helper's", async () => {
    globalThis.fetch = (async () => {
      throw new TypeError("Failed to fetch");
    }) as typeof fetch;
    expect(await api.probeHelper()).toBe(false);
    globalThis.fetch = (async () => jsonResponse(503, { error: "stopping" })) as typeof fetch;
    expect(await api.probeHelper()).toBe(false);
    globalThis.fetch = (async () => new Response("<!doctype html><p>router</p>", { status: 200 })) as typeof fetch;
    expect(await api.probeHelper()).toBe(false);
  });

  it("true for any other answer from the helper (the page then shows its own errors)", async () => {
    globalThis.fetch = (async () => jsonResponse(500, { error: "x" })) as typeof fetch;
    expect(await api.probeHelper()).toBe(true);
  });

  it("a token read that never answers gives up after TOKEN_TIMEOUT_MS (offline screen, not a blank page)", async () => {
    vi.useFakeTimers();
    try {
      globalThis.fetch = ((_u: unknown, init?: RequestInit) =>
        new Promise<Response>((_res, rej) => {
          init?.signal?.addEventListener("abort", () => rej(new Error("aborted")));
        })) as typeof fetch;
      const p = api.probeHelper();
      await vi.advanceTimersByTimeAsync(api.TOKEN_TIMEOUT_MS);
      expect(await p).toBe(false);
    } finally {
      vi.useRealTimers();
    }
  });
});

