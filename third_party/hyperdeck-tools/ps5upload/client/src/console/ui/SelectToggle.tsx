import { SquareCheck } from "lucide-react";
import { t } from "../strings";

/** The toolbar's "Select" toggle: the on-screen twin of □ (select several). */
export function SelectToggle({ on, onToggle }: { on: boolean; onToggle: () => void }) {
  return (
    <button type="button" className="cw-focusable cw-button" aria-pressed={on} onClick={onToggle}>
      <SquareCheck aria-hidden />
      <span>{t("cw_select")}</span>
    </button>
  );
}
