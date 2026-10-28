import { useEffect, useReducer, type ReactNode } from "react";
import type { ButtonId, InputBinding, InputMap } from "../nav";
import { t } from "../strings";
import { Modal } from "../ui/Modal";
import {
  buttonState,
  controlsReducer,
  INITIAL_CONTROLS,
  jobKey,
  startControlsSession,
  type ControlsState,
} from "./controlsModel";

/** How long the button pressed last stays brightly lit. */
const LIT_MS = 700;

type Shape =
  | { kind: "rect"; x: number; y: number; w: number; h: number; r: number }
  | { kind: "circle"; cx: number; cy: number; r: number };

/** Where each button sits on the drawing (viewBox 600 × 400). */
const LAYOUT: Record<ButtonId, { shape: Shape; label?: string; lx?: number; ly?: number }> = {
  l2: { shape: { kind: "rect", x: 112, y: 10, w: 86, h: 26, r: 11 }, label: "L2" },
  l1: { shape: { kind: "rect", x: 106, y: 42, w: 98, h: 20, r: 9 }, label: "L1" },
  r2: { shape: { kind: "rect", x: 402, y: 10, w: 86, h: 26, r: 11 }, label: "R2" },
  r1: { shape: { kind: "rect", x: 396, y: 42, w: 98, h: 20, r: 9 }, label: "R1" },
  touchpad: { shape: { kind: "rect", x: 222, y: 78, w: 156, h: 92, r: 14 }, label: "" },
  create: { shape: { kind: "rect", x: 196, y: 86, w: 14, h: 30, r: 7 } },
  options: { shape: { kind: "rect", x: 390, y: 86, w: 14, h: 30, r: 7 } },
  up: { shape: { kind: "rect", x: 128, y: 110, w: 26, h: 30, r: 5 }, label: "▲" },
  down: { shape: { kind: "rect", x: 128, y: 168, w: 26, h: 30, r: 5 }, label: "▼" },
  left: { shape: { kind: "rect", x: 96, y: 141, w: 31, h: 26, r: 5 }, label: "◀" },
  right: { shape: { kind: "rect", x: 155, y: 141, w: 31, h: 26, r: 5 }, label: "▶" },
  triangle: { shape: { kind: "circle", cx: 458, cy: 118, r: 18 }, label: "△" },
  cross: { shape: { kind: "circle", cx: 458, cy: 190, r: 18 }, label: "✕" },
  square: { shape: { kind: "circle", cx: 422, cy: 154, r: 18 }, label: "□" },
  circle: { shape: { kind: "circle", cx: 494, cy: 154, r: 18 }, label: "○" },
  lstick: { shape: { kind: "circle", cx: 222, cy: 236, r: 34 } },
  l3: { shape: { kind: "circle", cx: 222, cy: 236, r: 17 }, label: "L3" },
  rstick: { shape: { kind: "circle", cx: 378, cy: 236, r: 34 } },
  r3: { shape: { kind: "circle", cx: 378, cy: 236, r: 17 }, label: "R3" },
  ps: { shape: { kind: "circle", cx: 300, cy: 236, r: 13 }, label: "PS" },
  mute: { shape: { kind: "rect", x: 286, y: 262, w: 28, h: 10, r: 5 } },
};

/** The controller body: a DualSense outline with its two grips. */
const BODY =
  "M150 70 C210 56 390 56 450 70 C520 86 560 150 575 250 C590 332 562 384 520 382 C488 380 470 346 452 304 " +
  "C436 270 410 262 300 262 C190 262 164 270 148 304 C130 346 112 380 80 382 C38 384 10 332 25 250 C40 150 80 86 150 70 Z";

function shapeEl(s: Shape): ReactNode {
  return s.kind === "rect" ? (
    <rect x={s.x} y={s.y} width={s.w} height={s.h} rx={s.r} />
  ) : (
    <circle cx={s.cx} cy={s.cy} r={s.r} />
  );
}

function centre(s: Shape): [number, number] {
  return s.kind === "rect" ? [s.x + s.w / 2, s.y + s.h / 2] : [s.cx, s.cy];
}

function stateClass(b: InputBinding, s: ControlsState): string {
  const st = buttonState(b, s);
  return `cw-ctl-btn is-${st}${s.lit === b.button ? " is-lit" : ""}`;
}

/** The drawing: every button, lit when pressed, green once it works, amber when only received, faded when the PS5 keeps it. */
export function ControllerPicture({ map, state }: { map: InputMap; state: ControlsState }) {
  // The sticks before their clicks (L3/R3 sit on top of them).
  const order = map.slice().sort((a, b) => (a.button.endsWith("stick") ? 0 : 1) - (b.button.endsWith("stick") ? 0 : 1));
  return (
    <svg className="cw-ctl-picture" viewBox="0 0 600 400" role="img" aria-label={t("cw_ctl_picture")}>
      <path className="cw-ctl-body" d={BODY} />
      {order.map((b) => {
        const place = LAYOUT[b.button];
        if (!place) return null;
        const [x, y] = centre(place.shape);
        return (
          <g key={b.button} className={stateClass(b, state)} data-button={b.button}>
            {shapeEl(place.shape)}
            {place.label ? (
              <text x={x} y={y} textAnchor="middle" dominantBaseline="central">
                {place.label}
              </text>
            ) : null}
          </g>
        );
      })}
    </svg>
  );
}

/** Each button's job here and what this screen has seen of it. */
function Legend({ map, state }: { map: InputMap; state: ControlsState }) {
  return (
    <ul className="cw-ctl-legend">
      {map.map((b) => {
        const st = buttonState(b, state);
        const note =
          st === "system"
            ? t("cw_ctl_system")
            : st === "works"
              ? t("cw_ctl_ok")
              : st === "received"
                ? t("cw_ctl_received")
                : t("cw_ctl_not_yet");
        return (
          <li key={b.button} className={stateClass(b, state)} data-legend={b.button}>
            <span className="cw-glyph" aria-hidden>
              {b.glyph}
            </span>
            <span className="cw-ctl-job">{t(jobKey(b))}</span>
            <span className="cw-ctl-state">{note}</span>
          </li>
        );
      })}
    </ul>
  );
}

/** The button pressed last and its job: "Last pressed: ✕ Works: Select / open". */
function LastPressed({ map, state }: { map: InputMap; state: ControlsState }) {
  const id = state.lit ?? state.seen[state.seen.length - 1];
  const b = id ? map.find((x) => x.button === id) : undefined;
  return (
    <p className={`cw-ctl-last${b && !state.works.includes(b.button) ? " is-received" : ""}`} aria-live="polite">
      {b ? (
        <>
          {t("cw_ctl_last")}
          <span className="cw-glyph" aria-hidden>
            {b.glyph}
          </span>
          <strong>
            {state.works.includes(b.button)
              ? t("cw_ctl_works", { job: t(jobKey(b)) })
              : t("cw_ctl_received_job", { job: t(jobKey(b)) })}
          </strong>
        </>
      ) : null}
    </p>
  );
}

export interface ControlsViewProps {
  map: InputMap;
  state: ControlsState;
  onClose: () => void;
}

/** The Controls screen without state (tests render it directly). */
export function ControlsView({ map, state, onClose }: ControlsViewProps) {
  return (
    <Modal label={t("cw_ctl_title")} className="cw-controls" onBack={onClose}>
      <div className="cw-ctl-head">
        <h2>{t("cw_ctl_title")}</h2>
        <button type="button" className="cw-focusable cw-button" data-action="close-controls" onClick={onClose}>
          <span>{t("cw_help_close")}</span>
        </button>
      </div>
      <p className="cw-ctl-lead">{t("cw_ctl_press_each")}</p>
      <LastPressed map={map} state={state} />
      <div className="cw-ctl-main">
        {/* Focus rests on the drawing: ✕ pressed here lights ✕ instead of clicking a button. */}
        <div className="cw-focusable cw-ctl-stage" tabIndex={0} data-nav-default>
          <ControllerPicture map={map} state={state} />
        </div>
        <Legend map={map} state={state} />
      </div>
      <p className="cw-dim cw-ctl-foot">
        {state.unknown ? `${t("cw_ctl_other_key", { key: state.unknown })} · ` : ""}
        {t("cw_ctl_logging")}
      </p>
    </Modal>
  );
}

/** The Controls screen: lights each button as it arrives; the input log is on while it is open. */
export function Controls({ map, onClose }: { map: InputMap; onClose: () => void }) {
  const [state, dispatch] = useReducer(controlsReducer, INITIAL_CONTROLS);
  useEffect(
    () =>
      startControlsSession({
        target: window,
        map,
        onPress: (button, works) => dispatch({ type: "press", button, works }),
        onUnknown: (text) => dispatch({ type: "unknown", text }),
      }),
    [map],
  );
  useEffect(() => {
    if (!state.lit) return;
    const timer = setTimeout(() => dispatch({ type: "fade" }), LIT_MS);
    return () => clearTimeout(timer);
  }, [state.lit, state.seen]);
  return <ControlsView map={map} state={state} onClose={onClose} />;
}
