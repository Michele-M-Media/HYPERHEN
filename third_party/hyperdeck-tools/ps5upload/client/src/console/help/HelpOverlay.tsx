import { Gamepad2 } from "lucide-react";
import type { InputMap } from "../nav";
import { Glyphs } from "../shell/HintBar";
import { t } from "../strings";
import { Modal } from "../ui/Modal";
import { screenHints, type ScreenId } from "./hints";

export interface HelpOverlayProps {
  map: InputMap;
  /** The section behind the overlay. */
  screen: ScreenId;
  onControls: () => void;
  onClose: () => void;
}

/**
 * Help for the screen behind (OPTIONS, or the rail's Help): the buttons that
 * work there, the ones not confirmed on this PS5 yet, the pointer, and the way
 * to the Controls screen.
 */
export function HelpOverlay({ map, screen, onControls, onClose }: HelpOverlayProps) {
  const hints = screenHints(map, screen);
  const works = hints.filter((h) => h.works);
  const unconfirmed = hints.filter((h) => !h.works);
  return (
    <Modal label={t("cw_help_title")} className="cw-help" onBack={onClose}>
      <h2>{t("cw_help_title")}</h2>
      <ul className="cw-help-list" data-help="works">
        {works.map((h) => (
          <li key={h.labelKey} className="cw-hint">
            <Glyphs glyphs={h.glyphs} />
            {t(h.labelKey)}
          </li>
        ))}
        <li className="cw-hint">
          <Glyphs glyphs={map.filter((b) => b.action === "pointer").map((b) => b.glyph)} />
          {t("cw_help_pointer")}
        </li>
      </ul>
      {unconfirmed.length ? (
        <>
          <p className="cw-dim cw-help-sub">{t("cw_help_unconfirmed")}</p>
          <ul className="cw-help-list is-dim" data-help="unconfirmed">
            {unconfirmed.map((h) => (
              <li key={h.labelKey} className="cw-hint">
                <Glyphs glyphs={h.glyphs} />
                {t(h.labelKey)}
              </li>
            ))}
          </ul>
        </>
      ) : null}
      <div className="cw-dialog-buttons">
        <button type="button" className="cw-focusable cw-button" data-action="open-controls" onClick={onControls}>
          <Gamepad2 aria-hidden />
          <span>{t("cw_help_controls")}</span>
        </button>
        <button type="button" className="cw-focusable cw-button is-primary" data-nav-default onClick={onClose}>
          <span>{t("cw_help_close")}</span>
        </button>
      </div>
    </Modal>
  );
}
