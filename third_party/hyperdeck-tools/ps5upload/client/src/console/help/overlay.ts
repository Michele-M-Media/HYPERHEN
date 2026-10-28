/**
 * The page's guide overlays — the first-run tour, the help overlay (OPTIONS
 * or the rail's Help) and the Controls screen — at most one at a time, as a
 * small reducer App keeps in state.
 *
 * The tour shows once: closing it (Skip, ○ or Got it) stores it as seen in
 * localStorage; Settings' "Show tips again" opens it again. A browser that
 * refuses storage sees it once per page load.
 */

export type Overlay = "tour" | "help" | "controls" | null;

export type OverlayEvent =
  /** The page opened: the tour, unless seen. */
  | { type: "start" }
  /** OPTIONS / the rail's Help: the help overlay over a section (never over another overlay). */
  | { type: "help" }
  /** Settings or the help overlay: the Controls screen. */
  | { type: "controls" }
  /** Settings: Show tips again. */
  | { type: "tips" }
  | { type: "close" };

const SEEN_KEY = "ps5upload.console.tourSeen";
let seenThisLoad = false;

export function tourSeen(): boolean {
  if (seenThisLoad) return true;
  try {
    return localStorage.getItem(SEEN_KEY) === "1";
  } catch {
    return false;
  }
}

export function markTourSeen(): void {
  seenThisLoad = true;
  try {
    localStorage.setItem(SEEN_KEY, "1");
  } catch {
    // storage refused: seen for this page load only
  }
}

/** Tests: forget the in-memory flag. */
export function _resetTourForTests(): void {
  seenThisLoad = false;
}

export function overlayReducer(state: Overlay, ev: OverlayEvent): Overlay {
  switch (ev.type) {
    case "start":
      return state ?? (tourSeen() ? null : "tour");
    case "help":
      return state === null ? "help" : state;
    case "controls":
      return state === "tour" ? state : "controls";
    case "tips":
      return "tour";
    case "close":
      if (state === "tour") markTourSeen();
      return null;
  }
}
