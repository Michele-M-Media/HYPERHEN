import { Ellipsis } from "lucide-react";
import { t } from "../strings";

/**
 * The on-screen "⋯" that opens an item's actions menu: the always-available
 * twin of the △ shortcut, for list rows (Files, Home tasks).
 */
export function ActionsButton({ onPress }: { onPress: () => void }) {
  return (
    <button
      type="button"
      className="cw-focusable cw-icon-button"
      aria-label={t("cw_actions")}
      title={t("cw_actions")}
      onClick={(e) => {
        e.stopPropagation();
        onPress();
      }}
    >
      <Ellipsis aria-hidden />
    </button>
  );
}
