import { isValidElement, type ReactElement, type ReactNode } from "react";
import { afterEach, describe, expect, it, vi } from "vitest";
import de from "../locales/de.json";
import { _setStringsLoaderForTests, chooseLanguage, getLanguage, storedLanguage, stringsReady, t } from "../strings";
import { ConfirmDialog } from "../ui/ConfirmDialog";
import { lanToggleStep, SettingsView, waitTileApplied, type SettingsViewProps, type WebSettings } from "./Settings";

function elements(node: ReactNode): ReactElement<Record<string, unknown>>[] {
  const out: ReactElement<Record<string, unknown>>[] = [];
  const walk = (n: ReactNode) => {
    if (Array.isArray(n)) return n.forEach(walk);
    if (!isValidElement(n)) return;
    const el = n as ReactElement<Record<string, unknown>>;
    out.push(el);
    walk(el.props.children as ReactNode);
  };
  walk(node);
  return out;
}

const base = (over: Partial<SettingsViewProps> = {}): SettingsViewProps => ({
  settings: { lan: false, tile: true, version: "7.1.0" },
  loadFailed: false,
  language: "",
  askLan: false,
  busy: false,
  error: null,
  onToggleLan: vi.fn(),
  onConfirmLan: vi.fn(),
  onDismissLan: vi.fn(),
  onToggleTile: vi.fn(),
  onLanguage: vi.fn(),
  ...over,
});

afterEach(() => {
  vi.unstubAllGlobals();
  vi.restoreAllMocks();
  _setStringsLoaderForTests(null);
  chooseLanguage("");
});

describe("settings", () => {
  it("lan_toggle_requires_confirm", () => {
    expect(lanToggleStep(false)).toBe("confirm");
    expect(lanToggleStep(true)).toBe("apply");

    // Asking shows the warning in a confirm dialog; confirming is a separate action.
    const props = base({ askLan: true });
    const dialog = elements(SettingsView(props)).find((e) => e.type === ConfirmDialog);
    expect(dialog).toBeTruthy();
    expect(dialog?.props.body).toBe(t("cw_settings_lan_warning"));
    expect(t("cw_settings_lan_warning")).toBe("Anyone on your network can then use this page and manage your files.");
    expect(props.onConfirmLan).not.toHaveBeenCalled();
    (dialog?.props.onConfirm as () => void)();
    expect(props.onConfirmLan).toHaveBeenCalledTimes(1);

    // No dialog until asked; the toggle only reports the press.
    const idle = base();
    expect(elements(SettingsView(idle)).some((e) => e.type === ConfirmDialog)).toBe(false);
    const lan = elements(SettingsView(idle)).find((e) => e.props.action === "lan");
    (lan?.props.onClick as () => void)();
    expect(idle.onToggleLan).toHaveBeenCalledTimes(1);
  });

  it("tile_switch_waits_for_the_real_result", async () => {
    const at = (over: Partial<WebSettings>): WebSettings => ({ lan: false, tile: false, version: "7.1.0", ...over });
    const reads = [at({ tile_busy: true }), at({ tile_busy: false, tile_failed: true })];
    const read = vi.fn(async () => reads.shift()!);
    const sleep = vi.fn(async () => undefined);
    // The reply to the switch says "still working": re-read until it is done; the failure comes back.
    const done = await waitTileApplied(at({ tile_busy: true }), read, sleep);
    expect(read).toHaveBeenCalledTimes(2);
    expect(done?.tile_failed).toBe(true);
    // Already applied: no re-read.
    const read2 = vi.fn();
    expect(await waitTileApplied(at({ tile_busy: false }), read2, sleep)).toMatchObject({ tile_busy: false });
    expect(read2).not.toHaveBeenCalled();
    // A helper that never finishes: gives up after the limit, failed reads are retried.
    const stuck = vi.fn<() => Promise<WebSettings>>().mockRejectedValueOnce(new Error("x")).mockResolvedValue(at({ tile_busy: true }));
    const last = await waitTileApplied(at({ tile_busy: true }), stuck, sleep, 5000, 1000);
    expect(stuck).toHaveBeenCalledTimes(5);
    expect(last?.tile_busy).toBe(true);
  });

  it("full screen switch shows only where the browser supports it", () => {
    const onToggleFullscreen = vi.fn();
    const on = elements(SettingsView(base({ fullscreen: true, onToggleFullscreen }))).find((e) => e.props.action === "fullscreen");
    expect(on?.props.on).toBe(true);
    (on?.props.onClick as () => void)();
    expect(onToggleFullscreen).toHaveBeenCalledTimes(1);
    const hidden = elements(SettingsView(base({ fullscreen: null }))).find((e) => e.props.action === "fullscreen");
    expect(hidden).toBeUndefined();
  });

  it("language_choice_persists_and_survives_storage_errors", async () => {
    _setStringsLoaderForTests((code) => (code === "de" ? Promise.resolve(de) : Promise.reject(new Error("offline"))));
    vi.spyOn(console, "warn").mockImplementation(() => {});
    const store = new Map<string, string>();
    vi.stubGlobal("localStorage", {
      getItem: (k: string) => store.get(k) ?? null,
      setItem: (k: string, v: string) => void store.set(k, v),
      removeItem: (k: string) => void store.delete(k),
    });
    chooseLanguage("de");
    expect(getLanguage()).toBe("de");
    expect(storedLanguage()).toBe("de");
    await stringsReady();
    expect(t("cw_nav_settings")).toBe(de.cw_nav_settings);
    expect(t("cw_nav_settings")).not.toBe("Settings");
    chooseLanguage("");
    expect(storedLanguage()).toBe("");

    // Blocked storage: the choice still applies, nothing throws.
    const boom = () => {
      throw new Error("blocked");
    };
    vi.stubGlobal("localStorage", { getItem: boom, setItem: boom, removeItem: boom });
    expect(() => chooseLanguage("fr")).not.toThrow();
    expect(getLanguage()).toBe("fr");
    expect(storedLanguage()).toBe("");
  });

  it("theme_row_changes_the_theme_and_contrast_switch_toggles", () => {
    const onTheme = vi.fn();
    const onToggleContrast = vi.fn();
    const els = elements(SettingsView(base({ theme: "dark", contrast: false, onTheme, onToggleContrast })));
    const choices = els.filter((e) => e.props["data-theme-choice"] !== undefined);
    expect(choices.map((e) => e.props["data-theme-choice"])).toEqual(["dark", "light", "oled", "peach"]);
    expect(choices.map((e) => e.props["aria-pressed"])).toEqual([true, false, false, false]);
    (choices[3].props.onClick as () => void)();
    expect(onTheme).toHaveBeenCalledWith("peach");
    const sw = els.find((e) => e.props.action === "contrast");
    expect(sw?.props.on).toBe(false);
    (sw?.props.onClick as () => void)();
    expect(onToggleContrast).toHaveBeenCalled();
  });
});
