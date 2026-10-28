import { describe, expect, it, vi } from "vitest";
import { appcacheManifest, installOfflineCopy, serviceWorkerSource } from "./offlineCopy";

/** The service worker run against a fake worker scope: caches in a Map, a scripted network. */
function worker(network: (url: string) => Promise<Response>) {
  const listeners: Record<string, (e: unknown) => void> = {};
  const stores = new Map<string, Map<string, Response>>();
  stores.set("cw-page-old", new Map());
  stores.set("other-app", new Map());
  const keyOf = (k: unknown) => (typeof k === "string" ? new URL(k, "http://127.0.0.1:9180").href : (k as Request).url);
  const caches = {
    open: async (name: string) => {
      if (!stores.has(name)) stores.set(name, new Map());
      const s = stores.get(name)!;
      return {
        put: async (k: unknown, r: Response) => {
          s.set(keyOf(k), r);
        },
        match: async (k: unknown) => s.get(keyOf(k)),
        add: async (req: Request) => {
          s.set(req.url, await network(req.url));
        },
      };
    },
    keys: async () => Array.from(stores.keys()),
    delete: async (name: string) => stores.delete(name),
  };
  const self = {
    location: new URL("http://127.0.0.1:9180/sw.js"),
    addEventListener: (type: string, fn: (e: unknown) => void) => {
      listeners[type] = fn;
    },
    skipWaiting: vi.fn(async () => undefined),
    clients: { claim: vi.fn(async () => undefined) },
  };
  const fetchFn = (req: Request) => network(req.url);
  new Function("self", "caches", "fetch", serviceWorkerSource("abc123"))(self, caches, fetchFn);

  /** Dispatch a fetch; the response the worker gave, or null when it left the request alone. */
  const get = async (path: string, method = "GET"): Promise<Response | null> => {
    let answered: Promise<Response> | null = null;
    listeners.fetch({
      request: new Request(`http://127.0.0.1:9180${path}`, { method }),
      respondWith: (p: Promise<Response>) => {
        answered = p;
      },
    });
    return answered ? await answered : null;
  };
  const lifecycle = async (type: "install" | "activate") => {
    let wait: Promise<unknown> = Promise.resolve();
    listeners[type]({ waitUntil: (p: Promise<unknown>) => (wait = p) });
    await wait;
  };
  const flush = () => new Promise((r) => setTimeout(r, 0));
  return { get, lifecycle, stores, self, flush };
}

const ok = (body: string) => Promise.resolve(new Response(body, { status: 200 }));
const down = () => Promise.reject(new TypeError("Failed to fetch"));

describe("offline copy: service worker", () => {
  it("never touches /api/* nor POSTs (no cache, straight to the network)", async () => {
    const net = vi.fn(() => ok("{}"));
    const w = worker(net);
    expect(await w.get("/api/token")).toBeNull();
    expect(await w.get("/api/status", "POST")).toBeNull();
    expect(await w.get("/", "POST")).toBeNull();
    expect(net).not.toHaveBeenCalled();
    await w.flush();
    for (const [, s] of w.stores) for (const k of s.keys()) expect(k).not.toContain("/api/");
  });

  it("serves the page from the network while it answers, and from the copy once it does not", async () => {
    let up = true;
    const w = worker(() => (up ? ok("<html>v2</html>") : down()));
    expect(await (await w.get("/"))!.text()).toBe("<html>v2</html>");
    await w.flush();
    up = false;
    expect(await (await w.get("/"))!.text()).toBe("<html>v2</html>");
  });

  it("keeps the strings for offline use, and lets other paths through", async () => {
    let up = true;
    const w = worker(() => (up ? ok('{"cw_x":"y"}') : down()));
    await w.get("/strings/de.json?v=abc");
    await w.flush();
    up = false;
    expect(await (await w.get("/strings/de.json?v=abc"))!.text()).toBe('{"cw_x":"y"}');
    expect(await w.get("/favicon.ico")).toBeNull();
  });

  it("with no network and no copy the request fails like the network would", async () => {
    const w = worker(down);
    const r = await w.get("/");
    expect(r!.type).toBe("error");
  });

  it("install stores the page; activate drops older copies and only its own", async () => {
    const w = worker(() => ok("<html>page</html>"));
    await w.lifecycle("install");
    expect(w.self.skipWaiting).toHaveBeenCalled();
    expect(w.stores.get("cw-page-abc123")!.size).toBe(1);
    await w.lifecycle("activate");
    expect(Array.from(w.stores.keys()).sort()).toEqual(["cw-page-abc123", "other-app"]);
    expect(w.self.clients.claim).toHaveBeenCalled();
  });

  it("the version must be a plain hash (it is pasted into the script)", () => {
    expect(() => serviceWorkerSource('x"; evil()')).toThrow();
  });
});

describe("offline copy: appcache and registration", () => {
  it("the manifest caches the page only and names the build", () => {
    expect(appcacheManifest("abc123")).toBe("CACHE MANIFEST\n# PS5Upload Minion abc123\n\nNETWORK:\n*\n");
  });

  function fakeWindow(secure: boolean) {
    const fns: Record<string, () => void> = {};
    const win = {
      isSecureContext: secure,
      navigator: { serviceWorker: { register: vi.fn(() => Promise.resolve()) } },
      applicationCache: {
        addEventListener: (type: string, fn: () => void) => {
          fns[type] = fn;
        },
        swapCache: vi.fn(),
      },
      location: { reload: vi.fn() },
    };
    return { win, fns };
  }

  it("registers the worker at / in a secure context (the console's 127.0.0.1) only", () => {
    const a = fakeWindow(true);
    installOfflineCopy(a.win as unknown as Window, vi.fn());
    expect(a.win.navigator.serviceWorker.register).toHaveBeenCalledWith("/sw.js", { scope: "/" });
    const b = fakeWindow(false); // a phone at the console's LAN address
    installOfflineCopy(b.win as unknown as Window, vi.fn());
    expect(b.win.navigator.serviceWorker.register).not.toHaveBeenCalled();
  });

  it("a newer appcache copy offers the reload banner instead of reloading by itself", () => {
    const a = fakeWindow(true);
    const onUpdate = vi.fn();
    installOfflineCopy(a.win as unknown as Window, onUpdate);
    a.fns.updateready();
    expect(a.win.applicationCache.swapCache).toHaveBeenCalled();
    expect(onUpdate).toHaveBeenCalledTimes(1);
    expect(a.win.location.reload).not.toHaveBeenCalled();
  });
});
