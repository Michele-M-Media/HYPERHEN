import { useState } from "react";
import { fullscreenSupported } from "../fullscreen";
import { t } from "../strings";
import { Modal } from "../ui/Modal";

/** The three tip cards: a title and its lines (cw_ keys). */
export const TOUR_CARDS: { title: string; lines: string[] }[] = [
  {
    title: "cw_tour1_title",
    lines: ["cw_tour1_home", "cw_tour1_files", "cw_tour1_cheats", "cw_tour1_settings"],
  },
  {
    title: "cw_tour2_title",
    lines: ["cw_tour2_cross", "cw_tour2_circle", "cw_tour2_more"],
  },
  {
    title: "cw_tour3_title",
    lines: ["cw_tour3_pointer", "cw_tour3_fullscreen"],
  },
];

export interface TourViewProps {
  step: number;
  /** Whether the browser can go full screen (the card's tip is left out otherwise). */
  fullscreen: boolean;
  onNext: () => void;
  /** Skip, ○, or Got it on the last card. */
  onClose: () => void;
}

/** One card of the first-run tour, without state (tests render it directly). */
export function TourView({ step, fullscreen, onNext, onClose }: TourViewProps) {
  const i = Math.max(0, Math.min(step, TOUR_CARDS.length - 1));
  const card = TOUR_CARDS[i];
  const last = i === TOUR_CARDS.length - 1;
  const lines = card.lines.filter((k) => fullscreen || k !== "cw_tour3_fullscreen");
  return (
    <Modal label={t("cw_tour_label")} className="cw-tour" onBack={onClose}>
      <p className="cw-dim cw-tour-step">{t("cw_tour_step", { n: i + 1, total: TOUR_CARDS.length })}</p>
      <h2>{t(card.title)}</h2>
      <ul className="cw-tour-lines" data-card={i + 1}>
        {lines.map((k) => (
          <li key={k}>{t(k)}</li>
        ))}
      </ul>
      <div className="cw-dialog-buttons">
        {last ? null : (
          <button type="button" className="cw-focusable cw-button" data-action="tour-skip" onClick={onClose}>
            <span>{t("cw_tour_skip")}</span>
          </button>
        )}
        {/* The same element on every card (its slot never moves), so focus stays on it. */}
        <button
          type="button"
          className="cw-focusable cw-button is-primary"
          data-action={last ? "tour-done" : "tour-next"}
          data-nav-default
          onClick={last ? onClose : onNext}
        >
          <span>{t(last ? "cw_tour_done" : "cw_tour_next")}</span>
        </button>
      </div>
    </Modal>
  );
}

/** The first-run tour: three cards, ○ or Skip ends it. */
export function Tour({ onClose }: { onClose: () => void }) {
  const [step, setStep] = useState(0);
  return (
    <TourView
      step={step}
      fullscreen={fullscreenSupported()}
      onNext={() => setStep((n) => Math.min(n + 1, TOUR_CARDS.length - 1))}
      onClose={onClose}
    />
  );
}
