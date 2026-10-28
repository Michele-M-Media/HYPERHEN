import { isValidElement, type ReactElement, type ReactNode } from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { afterEach, describe, expect, it, vi } from "vitest";

vi.mock("../../state/engine", () => ({ getEngineUrl: () => "http://engine.test:19113" }));
vi.mock("../../state/lang", () => ({
  useTr: () => (key: string, vars?: Record<string, string | number>, fallback?: string) => {
    let s = fallback ?? key;
    for (const [k, v] of Object.entries(vars ?? {})) s = s.replace(`{${k}}`, String(v));
    return s;
  },
}));

import {
  changeConsolePrefs,
  fetchConsolePrefs,
  handoffToConsole,
  takeHandoffForPc,
} from "../../api/consoleFiles";
import { ConsolePinnedGroup, RecentPathsPanel } from "./RecentPathsDropdown";
import { showOnConsoleItems, type ConsolePins } from "./useConsolePins";

const reply = (status: number, body: unknown) => new Response(JSON.stringify(body), { status });

afterEach(() => vi.unstubAllGlobals());

/** Every <button> element in a rendered tree (host elements only). */
function buttons(node: ReactNode): ReactElement<{ onClick: () => void; children?: ReactNode }>[] {
  const out: ReactElement<{ onClick: () => void; children?: ReactNode }>[] = [];
  const walk = (n: ReactNode) => {
    if (Array.isArray(n)) return n.forEach(walk);
    if (!isValidElement(n)) return;
    const el = n as ReactElement<{ children?: ReactNode; onClick: () => void }>;
    if (el.type === "button") out.push(el);
    walk(el.props.children);
  };
  walk(node);
  return out;
}

describe("engine calls for the console's Files", () => {
  it("reads the console's pins by bare host, and a newer helper's answer is the prefs", async () => {
    const f = vi
      .fn()
      .mockResolvedValue(reply(200, { supported: true, favorites: ["/data/homebrew", "x"], recent: [], last: null }));
    vi.stubGlobal("fetch", f);
    const p = await fetchConsolePrefs("10.0.0.2:9120");
    expect(f.mock.calls[0][0]).toBe("http://engine.test:19113/api/ps5/files/prefs?addr=10.0.0.2");
    expect(p).toEqual({ favorites: ["/data/homebrew"], recent: [], last: null });
  });

  it("an older helper (supported:false) reads as null / false", async () => {
    vi.stubGlobal("fetch", vi.fn().mockImplementation(async () => reply(200, { supported: false })));
    expect(await fetchConsolePrefs("10.0.0.2")).toBeNull();
    expect(await changeConsolePrefs("10.0.0.2", "pin", "/data")).toBeNull();
    expect(await handoffToConsole("10.0.0.2", "/data")).toBe(false);
    expect(await takeHandoffForPc("10.0.0.2")).toEqual({ supported: false });
  });

  it("sends pin and Show on console with their wire names", async () => {
    const f = vi.fn().mockImplementation(async () => reply(200, { supported: true, ok: true, favorites: ["/data"] }));
    vi.stubGlobal("fetch", f);
    await changeConsolePrefs("10.0.0.2", "pin", "/data");
    expect(f.mock.calls[0][0]).toBe("http://engine.test:19113/api/ps5/files/prefs/set");
    expect(JSON.parse(f.mock.calls[0][1].body)).toEqual({ addr: "10.0.0.2", op: "pin", path: "/data" });
    expect(await handoffToConsole("10.0.0.2", "/data/a b")).toBe(true);
    expect(f.mock.calls[1][0]).toBe("http://engine.test:19113/api/ps5/files/handoff/put");
    expect(JSON.parse(f.mock.calls[1][1].body)).toEqual({ addr: "10.0.0.2", to: "console", path: "/data/a b" });
  });

  it("takes the PC's handoff: a path, or nothing waiting", async () => {
    const f = vi
      .fn()
      .mockResolvedValueOnce(reply(200, { supported: true, path: "/mnt/usb0/games", age_ms: 1500 }))
      .mockResolvedValueOnce(reply(200, { supported: true }));
    vi.stubGlobal("fetch", f);
    expect(await takeHandoffForPc("10.0.0.2")).toEqual({ supported: true, path: "/mnt/usb0/games", ageMs: 1500 });
    expect(JSON.parse(f.mock.calls[0][1].body)).toEqual({ addr: "10.0.0.2", for: "pc" });
    expect(await takeHandoffForPc("10.0.0.2")).toEqual({ supported: true, path: null, ageMs: 0 });
  });

  it("says why it failed", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(reply(502, { error: "helper_not_ava1: not running" })));
    await expect(handoffToConsole("10.0.0.2", "/data")).rejects.toThrow("helper_not_ava1");
  });
});

describe("Pinned on console in the path bar dropdown", () => {
  it("lists the console's pins above Recent; a pin opens here", () => {
    const onPick = vi.fn();
    const group = { pins: ["/data/homebrew", "/mnt/usb0"], pinned: false, onTogglePin: vi.fn() };
    const html = renderToStaticMarkup(
      <RecentPathsPanel paths={["/mnt/ext0"]} onPick={onPick} onRemove={() => {}} onClear={() => {}} console={group} />,
    );
    expect(html).toContain("Pinned on console");
    expect(html.indexOf("/data/homebrew")).toBeLessThan(html.indexOf("/mnt/ext0"));
    const tree = ConsolePinnedGroup({ group, onPick });
    const b = buttons(tree);
    b[1].props.onClick();
    expect(onPick).toHaveBeenCalledWith("/mnt/usb0");
  });

  it("offers pin for an unpinned folder and unpin for a pinned one", () => {
    const onTogglePin = vi.fn();
    const pinHtml = renderToStaticMarkup(
      <ConsolePinnedGroup group={{ pins: [], pinned: false, onTogglePin }} onPick={() => {}} />,
    );
    expect(pinHtml).toContain("Pin this folder on the console");
    const tree = ConsolePinnedGroup({ group: { pins: ["/data"], pinned: true, onTogglePin }, onPick: () => {} });
    const all = buttons(tree);
    const toggle = all[all.length - 1];
    expect(renderToStaticMarkup(toggle)).toContain("Unpin this folder on the console");
    toggle.props.onClick();
    expect(onTogglePin).toHaveBeenCalledOnce();
  });

  it("shows the group even with no recent folders, and no Clear all then", () => {
    const html = renderToStaticMarkup(
      <RecentPathsPanel
        paths={[]}
        onPick={() => {}}
        onRemove={() => {}}
        onClear={() => {}}
        console={{ pins: [], pinned: false, onTogglePin: () => {} }}
      />,
    );
    expect(html).toContain("fs-console-pins");
    expect(html).not.toContain("Clear all");
  });
});

describe("Show on console", () => {
  const tr = ((key: string, _v?: unknown, fallback?: string) => fallback ?? key) as never;
  const pins = (over: Partial<ConsolePins>): ConsolePins => ({
    pins: [],
    isPinned: () => false,
    toggle: async () => {},
    refresh: () => {},
    showOnConsole: async () => {},
    ...over,
  });

  it("is not offered against an older helper", () => {
    expect(showOnConsoleItems(pins({ pins: null }), "/data", tr, vi.fn())).toEqual([]);
  });

  it("hands the entry to the console and says so", async () => {
    const showOnConsole = vi.fn().mockResolvedValue(undefined);
    const toast = vi.fn();
    const [item] = showOnConsoleItems(pins({ showOnConsole }), "/data/homebrew/Game", tr, toast);
    expect(item.label).toBe("Show on console");
    item.run();
    await vi.waitFor(() => expect(toast).toHaveBeenCalled());
    expect(showOnConsole).toHaveBeenCalledWith("/data/homebrew/Game");
    expect(toast.mock.calls[0][0]).toMatchObject({
      tone: "success",
      message: "Sent to the PS5 — it opens in PS5Upload Minion",
    });
  });

  it("a failure is a warning with the reason", async () => {
    const toast = vi.fn();
    const [item] = showOnConsoleItems(
      pins({ showOnConsole: vi.fn().mockRejectedValue(new Error("helper_not_ava1")) }),
      "/data",
      tr,
      toast,
    );
    item.run();
    await vi.waitFor(() => expect(toast).toHaveBeenCalled());
    expect(toast.mock.calls[0][0].tone).toBe("warn");
    expect(toast.mock.calls[0][0].message).toContain("helper_not_ava1");
  });
});
