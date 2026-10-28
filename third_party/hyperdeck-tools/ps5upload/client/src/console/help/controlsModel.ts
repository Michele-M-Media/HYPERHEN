/**
 * The Controls screen's logic, without React: which DualSense button an input
 * event belongs to, what the screen shows for each button, and the session
 * that listens while the screen is open (and turns the input log on).
 *
 * Display only: lighting a button here never changes what the page acts on.
 * Which buttons the page acts on stays the `verified` flag in nav.ts, updated
 * from the input log after a measurement pass.
 */
import { setInputLogEnabled } from "../inputLog";
import { bindingForKey, type ButtonAction, type ButtonId, type InputBinding, type InputMap } from "../nav";

/** The fields read from an event (a real Event has them; tests pass plain objects). */
export interface PressEvent {
  type: string;
  key?: string;
  keyCode?: number;
  code?: string;
  detail?: number;
  isTrusted?: boolean;
}

/** The button `e` came from, or null (a key the table doesn't list). */
export function buttonForEvent(map: InputMap, e: PressEvent): ButtonId | null {
  if (e.type === "keydown") {
    const key = e.key ?? "";
    const code = e.keyCode ?? 0;
    const byKey = map.find((b) => b.action !== "system" && b.keys.includes(key));
    if (byKey) return byKey.button;
    if (key.length === 1 || !code) return null;
    return map.find((b) => b.action !== "system" && b.keyCodes.includes(code))?.button ?? null;
  }
  // The browser's pointer (left stick) and its scrolling (right stick).
  if (e.type === "mousemove") return "lstick";
  if (e.type === "wheel") return "rstick";
  // A pointer click (detail ≥ 1): on the PS5 that is ✕ pressed at the pointer.
  if (e.type === "click" && (e.detail ?? 0) > 0) return "cross";
  return null;
}

/**
 * Whether the page acts on `e` today (nav.ts, ruling R18): a key only when
 * bindingForKey matches it to `button` (verified, or one of the always-live
 * keys: arrows, Enter, Escape, Backspace); the pointer, scrolling and a
 * pointer click always (the browser's own). A key that arrives but is not
 * acted on yet is only "received".
 */
export function pageActsOn(map: InputMap, e: PressEvent, button: ButtonId): boolean {
  if (e.type !== "keydown") return true;
  return bindingForKey(map, e.key ?? "", e.keyCode ?? 0)?.button === button;
}

/** A key the table doesn't know, as one short line for the screen (never a typed character). */
export function describeUnknownKey(e: PressEvent): string {
  const key = e.key && e.key.length > 1 ? e.key : "?";
  return `${key} (${e.code || "-"}, ${e.keyCode ?? 0})`;
}

/** The job (a cw_ key) a button has on this page. */
export function jobKey(b: Pick<InputBinding, "button" | "action">): string {
  if (b.button === "ps") return "cw_job_ps";
  if (b.button === "mute") return "cw_job_mute";
  const byAction: Partial<Record<ButtonAction, string>> = {
    confirm: "cw_job_confirm",
    back: "cw_job_back",
    actions: "cw_job_actions",
    select: "cw_job_select",
    up: "cw_job_move",
    down: "cw_job_move",
    left: "cw_job_move",
    right: "cw_job_move",
    pointer: "cw_job_pointer",
    scroll: "cw_job_scroll",
    prevSection: "cw_job_prev_section",
    nextSection: "cw_job_next_section",
    pageUp: "cw_job_page_up",
    pageDown: "cw_job_page_down",
    top: "cw_job_top",
    rail: "cw_job_rail",
    help: "cw_job_help",
    refresh: "cw_job_refresh",
    fullscreen: "cw_job_fullscreen",
  };
  return byAction[b.action] ?? "cw_job_move";
}

/** A button's state on the screen: works (the page acts on it), received only, or not yet. */
export type ButtonState = "system" | "works" | "received" | "waiting";

export interface ControlsState {
  /** Buttons received since the screen opened. */
  seen: ButtonId[];
  /** Of those, the ones the page acts on. */
  works: ButtonId[];
  /** The button pressed last (lit brightly for a moment). */
  lit: ButtonId | null;
  /** The last key received that the table doesn't list. */
  unknown: string | null;
}

export const INITIAL_CONTROLS: ControlsState = { seen: [], works: [], lit: null, unknown: null };

export type ControlsEvent =
  | { type: "press"; button: ButtonId; works: boolean }
  | { type: "unknown"; text: string }
  | { type: "fade" };

export function controlsReducer(s: ControlsState, ev: ControlsEvent): ControlsState {
  switch (ev.type) {
    case "press": {
      const works = ev.works && !s.works.includes(ev.button) ? s.works.concat(ev.button) : s.works;
      if (s.lit === ev.button && s.seen.includes(ev.button) && works === s.works) return s;
      return { ...s, lit: ev.button, works, seen: s.seen.includes(ev.button) ? s.seen : s.seen.concat(ev.button) };
    }
    case "unknown":
      return s.unknown === ev.text ? s : { ...s, unknown: ev.text };
    case "fade":
      return s.lit === null ? s : { ...s, lit: null };
  }
}

export function buttonState(b: Pick<InputBinding, "button" | "action">, s: ControlsState): ButtonState {
  if (b.action === "system") return "system";
  if (s.works.includes(b.button)) return "works";
  return s.seen.includes(b.button) ? "received" : "waiting";
}

/** Keys that still go back (and so close the screen): ○ as Escape/Backspace. */
const BACK_KEYS: ReadonlySet<string> = new Set(["Escape", "Backspace", "GoBack", "BrowserBack"]);

const LISTENED = ["keydown", "mousemove", "wheel", "click"];

export interface ControlsSessionDeps {
  /** Where events are heard (the window). */
  target: Pick<EventTarget, "addEventListener" | "removeEventListener">;
  map: InputMap;
  /** `works`: the page acts on this input today (pageActsOn). */
  onPress: (button: ButtonId, works: boolean) => void;
  onUnknown: (text: string) => void;
  /** Default: inputLog.setInputLogEnabled. */
  setLog?: (on: boolean) => void;
}

/**
 * While the Controls screen is open: the input log is on, every event lights
 * its button, and keys do nothing else (no focus move, no click, no browser
 * shortcut such as F5 reloading the page) — except ○'s keys, which close the
 * screen as Back always does. Returns the function that ends it (log off).
 */
export function startControlsSession(deps: ControlsSessionDeps): () => void {
  const setLog = deps.setLog ?? setInputLogEnabled;
  const on = (ev: Event) => {
    const e = ev as unknown as PressEvent & Event;
    if (e.type === "click" && e.isTrusted === false) return;
    const button = buttonForEvent(deps.map, e);
    if (button) deps.onPress(button, pageActsOn(deps.map, e, button));
    else if (e.type === "keydown") deps.onUnknown(describeUnknownKey(e));
    if (e.type === "keydown" && !BACK_KEYS.has(e.key ?? "") && typeof e.preventDefault === "function") {
      // Captured before nav.ts's listener, which skips a defaultPrevented key.
      e.preventDefault();
    }
  };
  setLog(true);
  for (const type of LISTENED) deps.target.addEventListener(type, on, true);
  return () => {
    for (const type of LISTENED) deps.target.removeEventListener(type, on, true);
    setLog(false);
  };
}
