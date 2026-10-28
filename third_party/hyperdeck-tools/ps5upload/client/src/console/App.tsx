import { useCallback, useEffect, useMemo, useReducer, useState } from "react";
import { call, isOffline, onTokenExpired } from "./api";
import { useBackLevel } from "./backHistory";
import { toggleFullscreen } from "./fullscreen";
import { Controls } from "./help/Controls";
import { HelpOverlay } from "./help/HelpOverlay";
import { overlayReducer } from "./help/overlay";
import { Tour } from "./help/Tour";
import { focusElement, onNavAction, type InputMap } from "./nav";
import { SECTIONS, type SectionContext, type SectionParams } from "./sections";
import { HintBar } from "./shell/HintBar";
import { Rail } from "./shell/Rail";
import { ReloadBanner } from "./shell/ReloadBanner";
import { TopBar, type ConsoleStatus } from "./shell/TopBar";
import { focusLost } from "./ui/restoreFocus";
import { pollWhileVisible } from "./ui/visiblePoll";
import { useHandoffPoll } from "./files/handoff";
import { t } from "./strings";

/** The console chip refreshes this often (the temperature moves). */
const STATUS_POLL_MS = 10_000;

function railButton(id: string): HTMLElement | null {
  return document.querySelector<HTMLElement>(`[data-rail-id="${id}"]`);
}

/** The page: top bar, rail of sections, the active section, hint bar, reload banner. */
export function App({ inputMap }: { inputMap: InputMap }) {
  const [active, setActive] = useState(SECTIONS[0].id);
  const [expired, setExpired] = useState(false);
  const [status, setStatus] = useState<ConsoleStatus | null>(null);
  const [offline, setOffline] = useState(false);
  /** Bumped to read /api/status again at once. */
  const [statusTick, setStatusTick] = useState(0);
  /** What the active section was opened with (Home's drive cards → Files at a path). */
  const [params, setParams] = useState<SectionParams | null>(null);
  /** The tour (first run), the help overlay or the Controls screen; at most one. */
  const [overlay, overlayDo] = useReducer(overlayReducer, null, () => overlayReducer(null, { type: "start" }));
  const closeOverlay = useCallback(() => overlayDo({ type: "close" }), []);
  const openHelp = useCallback(() => overlayDo({ type: "help" }), []);
  const openControls = useCallback(() => overlayDo({ type: "controls" }), []);

  /** Switch section; params go only to a section opened through openSection. */
  const show = useCallback((id: string, p: SectionParams | null = null) => {
    setActive(id);
    setParams(p);
  }, []);

  // Away from Home, the browser's Back (○) comes back towards it (rail, then Home).
  useBackLevel(active !== SECTIONS[0].id);

  useEffect(() => onTokenExpired(() => setExpired(true)), []);

  // The console chip. Stops once the token is gone: every call would 401.
  useEffect(() => {
    if (expired) return;
    let live = true;
    const load = () => {
      call<ConsoleStatus>("/api/status").then(
        (s) => {
          if (!live) return;
          setStatus(s);
          setOffline(false);
        },
        (e: unknown) => {
          if (live && isOffline(e)) setOffline(true);
        },
      );
    };
    // Not while the page is hidden (the browser in the background): no reason to wake the helper.
    const stop = pollWhileVisible(load, STATUS_POLL_MS);
    return () => {
      live = false;
      stop();
    };
  }, [expired, statusTick]);

  // Start with focus on the rail, so the D-pad works at once (after the tour, when it shows first:
  // it opened before anything had focus, so there is nothing to go back to).
  useEffect(() => {
    if (overlay !== null || !focusLost()) return;
    const el = railButton(active);
    if (el) focusElement(el);
    // Only when an overlay closes (or the page opens), not on every section switch.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [overlay]);

  // L1/R1 step through the sections; ○ goes from the content to the rail, then to Home;
  // Create re-reads the status (sections re-read their own data first); the touchpad toggles full screen.
  useEffect(
    () =>
      onNavAction((action) => {
        if (action === "refresh") {
          setStatusTick((n) => n + 1);
          return true;
        }
        if (action === "help") {
          openHelp();
          return true;
        }
        if (action === "fullscreen") {
          toggleFullscreen();
          return true;
        }
        const i = SECTIONS.findIndex((s) => s.id === active);
        if (action === "prevSection" || action === "nextSection") {
          const step = action === "nextSection" ? 1 : SECTIONS.length - 1;
          const next = SECTIONS[(i + step) % SECTIONS.length].id;
          show(next);
          const el = railButton(next);
          if (el) focusElement(el);
          return true;
        }
        if (action === "back") {
          const inRail = !!document.activeElement?.closest(".cw-rail");
          if (!inRail) {
            const el = railButton(active);
            if (el) focusElement(el);
          } else if (active !== SECTIONS[0].id) {
            show(SECTIONS[0].id);
            const el = railButton(SECTIONS[0].id);
            if (el) focusElement(el);
          }
          return true;
        }
        return false;
      }, undefined, { fallback: true }),
    [active, show, openHelp],
  );

  // "Show on TV": the PC app left a folder; Files opens there (any section).
  const openFromPc = useCallback(
    (p: SectionParams) => {
      show("files", p);
      const el = railButton("files");
      if (el) focusElement(el);
    },
    [show],
  );
  useHandoffPoll(openFromPc, !expired);

  const ctx = useMemo<SectionContext>(
    () => ({
      status,
      refreshStatus: () => setStatusTick((n) => n + 1),
      openSection: (id, p) => {
        if (!SECTIONS.some((s) => s.id === id)) return;
        show(id, p ?? null);
        const el = railButton(id);
        if (el) focusElement(el);
      },
      hasSection: (id) => SECTIONS.some((s) => s.id === id),
      canOpen: (id) => SECTIONS.some((s) => s.id === id && s.acceptsParams),
      params,
      openControls,
      showTips: () => overlayDo({ type: "tips" }),
    }),
    [status, params, show, openControls],
  );

  const section = SECTIONS.find((s) => s.id === active) ?? SECTIONS[0];
  const chipFallback = offline || expired ? t("cw_status_offline") : status ? null : t("cw_status_connecting");

  return (
    <div className="cw-app">
      <TopBar status={status} chipFallback={chipFallback} />
      <div className="cw-body" aria-hidden={expired ? true : undefined}>
        <Rail sections={SECTIONS} active={active} onSelect={(id) => show(id)} onHelp={openHelp} />
        <main className="cw-content" data-nav-region="content">
          {section.render(ctx)}
        </main>
      </div>
      <HintBar map={inputMap} screen={overlay ?? section.id} />
      {overlay === "tour" ? <Tour onClose={closeOverlay} /> : null}
      {overlay === "help" ? (
        <HelpOverlay map={inputMap} screen={section.id} onControls={openControls} onClose={closeOverlay} />
      ) : null}
      {overlay === "controls" ? <Controls map={inputMap} onClose={closeOverlay} /> : null}
      {expired ? <ReloadBanner /> : null}
    </div>
  );
}
