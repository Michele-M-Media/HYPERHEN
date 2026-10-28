import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import html from "./index.html?raw";
import { applyStoredTheme, getContrast, getTheme, setContrast, setTheme } from "./theme";

function stubDom(store: Record<string, string> | "refuse") {
  const attrs = new Map<string, string>();
  vi.stubGlobal("document", {
    documentElement: {
      setAttribute: (k: string, v: string) => void attrs.set(k, v),
      removeAttribute: (k: string) => void attrs.delete(k),
      getAttribute: (k: string) => attrs.get(k) ?? null,
    },
  });
  vi.stubGlobal("localStorage", {
    getItem: (k: string) => {
      if (store === "refuse") throw new Error("denied");
      return store[k] ?? null;
    },
    setItem: (k: string, v: string) => {
      if (store === "refuse") throw new Error("denied");
      store[k] = v;
    },
  });
  return attrs;
}

describe("console theme", () => {
  beforeEach(() => vi.unstubAllGlobals());
  afterEach(() => vi.unstubAllGlobals());

  it("defaults_to_dark_without_attributes", () => {
    const attrs = stubDom({});
    applyStoredTheme();
    expect(getTheme()).toBe("dark");
    expect(getContrast()).toBe(false);
    expect(attrs.has("data-theme")).toBe(false);
    expect(attrs.has("data-contrast")).toBe(false);
  });

  it("applies_and_persists_theme_and_contrast", () => {
    const store: Record<string, string> = {};
    const attrs = stubDom(store);
    setTheme("peach");
    setContrast(true);
    expect(attrs.get("data-theme")).toBe("peach");
    expect(attrs.get("data-contrast")).toBe("high");
    // a fresh page load reads it back
    attrs.clear();
    applyStoredTheme();
    expect(getTheme()).toBe("peach");
    expect(attrs.get("data-theme")).toBe("peach");
    expect(attrs.get("data-contrast")).toBe("high");
    setTheme("dark");
    setContrast(false);
    expect(attrs.has("data-theme")).toBe(false);
    expect(attrs.has("data-contrast")).toBe(false);
  });

  it("ignores_an_unknown_stored_theme", () => {
    stubDom({ "ps5upload.console.theme": "neon" });
    expect(getTheme()).toBe("dark");
  });

  it("storage_failure_still_applies_for_the_session", () => {
    const attrs = stubDom("refuse");
    applyStoredTheme();
    expect(getTheme()).toBe("dark");
    expect(() => setTheme("oled")).not.toThrow();
    expect(() => setContrast(true)).not.toThrow();
    expect(attrs.get("data-theme")).toBe("oled");
    expect(attrs.get("data-contrast")).toBe("high");
    expect(getTheme()).toBe("oled");
    expect(getContrast()).toBe(true);
  });
});

describe("head script (index.html)", () => {
  const script = html.match(/<script>([\s\S]*?)<\/script>/)?.[1] ?? "";

  function runHead(store: Record<string, string> | "refuse") {
    const attrs = new Map<string, string>();
    const meta = { content: "dark", setAttribute: (_k: string, v: string) => void (meta.content = v) };
    const ctx = {
      localStorage: {
        getItem: (k: string) => {
          if (store === "refuse") throw new Error("denied");
          return store[k] ?? null;
        },
      },
      document: {
        documentElement: { setAttribute: (k: string, v: string) => void attrs.set(k, v) },
        querySelector: () => meta,
      },
    };
    new Function("localStorage", "document", script)(ctx.localStorage, ctx.document);
    return { attrs, meta };
  }

  it("is_present_and_classic", () => {
    expect(script).toContain("ps5upload.console.theme");
    expect(html.indexOf("<script>")).toBeLessThan(html.indexOf('type="module"'));
  });

  it("matches_theme_ts_for_every_stored_choice", () => {
    for (const theme of ["dark", "light", "oled", "peach"] as const) {
      for (const high of [false, true]) {
        // what theme.ts stores and applies ...
        const store: Record<string, string> = {};
        const attrs = stubDom(store);
        setTheme(theme);
        setContrast(high);
        const want = [...attrs.entries()].sort().join("|");
        vi.unstubAllGlobals();
        // ... the head script reproduces from the same storage
        const got = runHead(store);
        expect([...got.attrs.entries()].sort().join("|")).toBe(want);
        expect(got.meta.content).toBe(theme === "light" || theme === "peach" ? "light" : "dark");
      }
    }
  });

  it("survives_refused_storage", () => {
    const { attrs } = runHead("refuse");
    expect(attrs.size).toBe(0);
  });
});
