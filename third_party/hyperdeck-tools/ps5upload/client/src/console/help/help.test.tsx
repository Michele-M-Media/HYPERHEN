import { renderToStaticMarkup } from "react-dom/server";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

vi.mock("../inputLog", () => ({ setInputLogEnabled: vi.fn() }));

import { setInputLogEnabled } from "../inputLog";
import { DEFAULT_INPUT_MAP, type InputMap } from "../nav";
import { SettingsView, type SettingsViewProps } from "../settings/Settings";
import { HintBar } from "../shell/HintBar";
import { chooseLanguage, t } from "../strings";
import { ControlsView } from "./Controls";
import {
  buttonForEvent,
  buttonState,
  controlsReducer,
  INITIAL_CONTROLS,
  pageActsOn,
  startControlsSession,
} from "./controlsModel";
import { HelpOverlay } from "./HelpOverlay";
import { barHints, screenHints } from "./hints";
import { _resetTourForTests, overlayReducer, tourSeen } from "./overlay";
import { TourView, TOUR_CARDS } from "./Tour";

/** The table with some buttons marked measured. */
function verified(...ids: string[]): InputMap {
  return DEFAULT_INPUT_MAP.map((b) => (ids.includes(b.button) ? { ...b, verified: true } : b));
}

function fakeStorage() {
  const store = new Map<string, string>();
  vi.stubGlobal("localStorage", {
    getItem: (k: string) => store.get(k) ?? null,
    setItem: (k: string, v: string) => void store.set(k, v),
    removeItem: (k: string) => void store.delete(k),
  });
  return store;
}

beforeEach(() => {
  _resetTourForTests();
  vi.mocked(setInputLogEnabled).mockClear();
});
afterEach(() => {
  vi.unstubAllGlobals();
  chooseLanguage("");
});

describe("Controls screen", () => {
  it("lights the pressed button and says it works", () => {
    // Enter is ✕, ArrowUp the D-pad, F1 OPTIONS (seeded, unverified: still shown here).
    expect(buttonForEvent(DEFAULT_INPUT_MAP, { type: "keydown", key: "Enter", keyCode: 13 })).toBe("cross");
    expect(buttonForEvent(DEFAULT_INPUT_MAP, { type: "keydown", key: "F1", keyCode: 112 })).toBe("options");
    expect(buttonForEvent(DEFAULT_INPUT_MAP, { type: "keydown", key: "Unidentified", keyCode: 38 })).toBe("up");
    expect(buttonForEvent(DEFAULT_INPUT_MAP, { type: "keydown", key: "q", keyCode: 81 })).toBeNull();
    expect(buttonForEvent(DEFAULT_INPUT_MAP, { type: "mousemove" })).toBe("lstick");
    expect(buttonForEvent(DEFAULT_INPUT_MAP, { type: "wheel" })).toBe("rstick");
    expect(buttonForEvent(DEFAULT_INPUT_MAP, { type: "click", detail: 1 })).toBe("cross");
    expect(buttonForEvent(DEFAULT_INPUT_MAP, { type: "click", detail: 0 })).toBeNull();

    // ✕ works (the page acts on Enter); △ arrived but is unverified: only "received" (R18).
    const crossed = controlsReducer(INITIAL_CONTROLS, { type: "press", button: "cross", works: true });
    const s = controlsReducer(crossed, { type: "press", button: "triangle", works: false });
    expect(s.lit).toBe("triangle");
    expect(s.seen).toEqual(["cross", "triangle"]);
    const html = renderToStaticMarkup(<ControlsView map={DEFAULT_INPUT_MAP} state={s} onClose={() => {}} />);
    expect(html).toMatch(/<g class="cw-ctl-btn is-received is-lit" data-button="triangle">/);
    expect(html).toMatch(/<g class="cw-ctl-btn is-works" data-button="cross">/);
    expect(html).toContain(t("cw_ctl_received_job", { job: t("cw_job_actions") }));
    expect(html).not.toContain(t("cw_ctl_works", { job: t("cw_job_actions") }));
    expect(html).toContain(t("cw_ctl_received"));
    expect(html).toContain(t("cw_ctl_ok"));
    const lastCross = renderToStaticMarkup(
      <ControlsView map={DEFAULT_INPUT_MAP} state={{ ...crossed, lit: "cross" }} onClose={() => {}} />,
    );
    expect(lastCross).toContain(t("cw_ctl_works", { job: t("cw_job_confirm") }));
    // Not pressed: waiting; PS and Mute: kept by the PS5.
    expect(html).toMatch(/<g class="cw-ctl-btn is-waiting" data-button="square">/);
    expect(html).toMatch(/<g class="cw-ctl-btn is-system" data-button="ps">/);
    expect(html).toContain(t("cw_ctl_not_yet"));
    expect(html).toContain(t("cw_ctl_system"));
    expect(html).toContain(t("cw_ctl_press_each"));

    // The light fades; the button stays received.
    const faded = controlsReducer(s, { type: "fade" });
    expect(faded.lit).toBeNull();
    expect(buttonState({ button: "triangle", action: "actions" }, faded)).toBe("received");
  });

  it("turns the input log on while open and off on close; keys do nothing else", () => {
    // A fake window (Node's EventTarget does not remove capture listeners).
    const listeners = new Map<string, Set<(e: Event) => void>>();
    const target = {
      addEventListener: (type: string, fn: EventListenerOrEventListenerObject | null) => {
        if (!listeners.has(type)) listeners.set(type, new Set());
        listeners.get(type)?.add(fn as (e: Event) => void);
      },
      removeEventListener: (type: string, fn: EventListenerOrEventListenerObject | null) => {
        listeners.get(type)?.delete(fn as (e: Event) => void);
      },
      dispatchEvent: (e: Event) => {
        listeners.get(e.type)?.forEach((fn) => fn(e));
        return true;
      },
    };
    const pressed: string[] = [];
    const works: boolean[] = [];
    const unknown: string[] = [];
    const stop = startControlsSession({
      target,
      map: DEFAULT_INPUT_MAP,
      onPress: (b, w) => {
        pressed.push(b);
        works.push(w);
      },
      onUnknown: (k) => unknown.push(k),
    });
    expect(setInputLogEnabled).toHaveBeenLastCalledWith(true);

    const key = (k: string, keyCode: number) => {
      const e = new Event("keydown", { cancelable: true });
      Object.assign(e, { key: k, keyCode, code: "" });
      target.dispatchEvent(e);
      return e;
    };
    // ✕ as Enter lights ✕ and never clicks or moves focus (default prevented: nav.ts skips it).
    expect(key("Enter", 13).defaultPrevented).toBe(true);
    // ○'s keys still go back (the screen closes the way Back always does).
    expect(key("Escape", 27).defaultPrevented).toBe(false);
    key("MediaPlayPause", 179);
    // F1 is OPTIONS' seeded key, not verified: the page does not act on it, so it is only received.
    key("F1", 112);
    expect(pressed).toEqual(["cross", "circle", "options"]);
    expect(works).toEqual([true, true, false]);
    // Verified, it works.
    const map = DEFAULT_INPUT_MAP.map((b) => (b.button === "options" ? { ...b, verified: true } : b));
    expect(pageActsOn(map, { type: "keydown", key: "F1", keyCode: 112 }, "options")).toBe(true);
    expect(pageActsOn(DEFAULT_INPUT_MAP, { type: "mousemove" }, "lstick")).toBe(true);
    expect(unknown).toEqual(["MediaPlayPause (-, 179)"]);

    stop();
    expect(setInputLogEnabled).toHaveBeenLastCalledWith(false);
    key("Enter", 13);
    expect(pressed).toHaveLength(3);
  });
});

describe("first-run tour", () => {
  it("shows once, is skippable, and Show tips again brings it back", () => {
    const store = fakeStorage();
    expect(tourSeen()).toBe(false);
    expect(overlayReducer(null, { type: "start" })).toBe("tour");
    // Skip (or ○) closes it and stores it as seen.
    expect(overlayReducer("tour", { type: "close" })).toBeNull();
    expect(store.get("ps5upload.console.tourSeen")).toBe("1");
    _resetTourForTests();
    expect(overlayReducer(null, { type: "start" })).toBeNull();
    // Settings' Show tips again.
    expect(overlayReducer(null, { type: "tips" })).toBe("tour");

    const onClose = vi.fn();
    const onNext = vi.fn();
    const first = TourView({ step: 0, fullscreen: true, onNext, onClose });
    // ○ is Modal's onBack: skips the tour.
    expect((first.props as { onBack: () => void }).onBack).toBe(onClose);
    const html = renderToStaticMarkup(first);
    expect(html).toContain(t("cw_tour_skip"));
    expect(html).toContain(t("cw_tour1_cheats"));
    expect(html).toContain(t("cw_tour_step", { n: 1, total: 3 }));
    const last = renderToStaticMarkup(TourView({ step: TOUR_CARDS.length - 1, fullscreen: false, onNext, onClose }));
    expect(last).toContain(t("cw_tour_done"));
    expect(last).toContain(t("cw_tour3_pointer"));
    // No full screen in this browser: no full-screen tip.
    expect(last).not.toContain(t("cw_tour3_fullscreen"));
  });

  it("is seen for the page load even when storage is blocked", () => {
    const boom = () => {
      throw new Error("blocked");
    };
    vi.stubGlobal("localStorage", { getItem: boom, setItem: boom, removeItem: boom });
    expect(overlayReducer(null, { type: "start" })).toBe("tour");
    expect(() => overlayReducer("tour", { type: "close" })).not.toThrow();
    expect(overlayReducer(null, { type: "start" })).toBeNull();
  });

  it("help never opens over another overlay; Controls replaces help", () => {
    expect(overlayReducer(null, { type: "help" })).toBe("help");
    expect(overlayReducer("controls", { type: "help" })).toBe("controls");
    expect(overlayReducer("help", { type: "controls" })).toBe("controls");
  });
});

describe("hints", () => {
  it("help overlay lists the current screen's buttons", () => {
    const map = verified("triangle", "l1", "r1");
    const files = renderToStaticMarkup(
      <HelpOverlay map={map} screen="files" onControls={() => {}} onClose={() => {}} />,
    );
    const works = files.slice(files.indexOf('data-help="works"'), files.indexOf("</ul>"));
    expect(works).toContain("△");
    expect(works).toContain(t("cw_actions"));
    expect(works).toContain(t("cw_hint_switch_section"));
    expect(works).toContain(t("cw_hint_open"));
    expect(works).toContain(t("cw_hint_back"));
    // □ has a job in Files but was not measured: listed as not confirmed.
    const rest = files.slice(files.indexOf('data-help="unconfirmed"'));
    expect(rest).toContain("□");
    expect(files).toContain(t("cw_help_controls"));

    // Home has no △ job.
    const home = renderToStaticMarkup(<HelpOverlay map={map} screen="home" onControls={() => {}} onClose={() => {}} />);
    expect(home).not.toContain("△");
    // Cheats: □ switches the cheat.
    expect(screenHints(map, "cheats").find((h) => h.glyphs.includes("□"))?.labelKey).toBe("cw_hint_switch");
  });

  it("hint bar: ✕ and ○ always, others only where they work and once verified", () => {
    const labels = (screen: string, map: InputMap = DEFAULT_INPUT_MAP) => barHints(map, screen).map((h) => h.labelKey);
    expect(labels("home")).toEqual(["cw_hint_open", "cw_hint_back"]);
    expect(labels("files", verified("triangle"))).toEqual(["cw_hint_open", "cw_hint_back", "cw_actions"]);
    expect(labels("home", verified("triangle"))).toEqual(["cw_hint_open", "cw_hint_back"]);
    // Only L1 measured: "Switch section" waits for R1 too.
    expect(labels("home", verified("l1"))).not.toContain("cw_hint_switch_section");
    expect(labels("controls", verified("triangle", "options"))).toEqual(["cw_hint_open", "cw_hint_back"]);
    const html = renderToStaticMarkup(<HintBar map={verified("options")} screen="settings" />);
    expect(html).toContain('data-screen="settings"');
    expect(html).toContain(t("cw_hint_help"));
  });
});

describe("settings layout", () => {
  it("Theme · Language · Full screen · Controls · Show tips again · Allow other devices · tile · About", () => {
    const props: SettingsViewProps = {
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
      theme: "dark",
      contrast: false,
      fullscreen: false,
      onOpenControls: vi.fn(),
      onShowTips: vi.fn(),
    };
    const html = renderToStaticMarkup(<SettingsView {...props} />);
    const order = Array.from(html.matchAll(/data-setting="([a-z]+)"/g), (m) => m[1]);
    expect(order).toEqual(["theme", "language", "fullscreen", "controls", "tips", "lan", "tile", "about"]);
    expect(html).toContain(t("cw_settings_tips"));
  });
});
