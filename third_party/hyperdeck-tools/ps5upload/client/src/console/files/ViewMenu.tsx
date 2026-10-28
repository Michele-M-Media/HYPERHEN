import { ArrowDownAZ, ArrowUpAZ, Check, Eye, EyeOff, type LucideIcon } from "lucide-react";
import { t } from "../strings";
import { Modal } from "../ui/Modal";
import type { SortKey, ViewPrefs } from "./listing";

const SORTS: { key: SortKey; label: string }[] = [
  { key: "name", label: "cw_files_sort_name" },
  { key: "size", label: "cw_files_sort_size" },
  { key: "date", label: "cw_files_sort_date" },
];

/**
 * "Sort and view": one small menu behind one toolbar button (D-pad + ✕).
 * Choosing a sort key keeps the menu open so the order can be flipped too;
 * ○ closes it.
 */
export function ViewMenu({
  view,
  onChange,
  onClose,
}: {
  view: ViewPrefs;
  onChange: (patch: Partial<ViewPrefs>) => void;
  onClose: () => void;
}) {
  const Order: LucideIcon = view.dir === "asc" ? ArrowDownAZ : ArrowUpAZ;
  const Hidden: LucideIcon = view.hidden ? Eye : EyeOff;
  return (
    <Modal label={t("cw_files_sort_view")} className="cw-sheet" onBack={onClose}>
      <h2 className="cw-sheet-title">{t("cw_files_sort_view")}</h2>
      <div className="cw-sheet-items">
        {SORTS.map((s, i) => (
          <button
            key={s.key}
            type="button"
            className="cw-focusable cw-sheet-item"
            data-sort={s.key}
            aria-pressed={view.sort === s.key}
            data-nav-default={i === 0 ? "" : undefined}
            onClick={() => onChange({ sort: s.key })}
          >
            {view.sort === s.key ? <Check aria-hidden /> : <span className="cw-sheet-gap" aria-hidden />}
            <span>{t(s.label)}</span>
          </button>
        ))}
        <button
          type="button"
          className="cw-focusable cw-sheet-item"
          data-action="order"
          onClick={() => onChange({ dir: view.dir === "asc" ? "desc" : "asc" })}
        >
          <Order aria-hidden />
          <span>{t("cw_files_order", { dir: t(view.dir === "asc" ? "cw_files_order_asc" : "cw_files_order_desc") })}</span>
        </button>
        <button
          type="button"
          className="cw-focusable cw-sheet-item"
          data-action="hidden"
          aria-pressed={view.hidden}
          onClick={() => onChange({ hidden: !view.hidden })}
        >
          <Hidden aria-hidden />
          <span>{t(view.hidden ? "cw_files_hidden_on" : "cw_files_hidden_off")}</span>
        </button>
      </div>
      <div className="cw-dialog-buttons">
        <button type="button" className="cw-focusable cw-button is-primary" onClick={onClose}>
          <span>{t("cw_files_ok")}</span>
        </button>
      </div>
    </Modal>
  );
}
