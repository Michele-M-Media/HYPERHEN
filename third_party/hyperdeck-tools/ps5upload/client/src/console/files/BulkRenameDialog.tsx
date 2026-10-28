import { useMemo, useState } from "react";
import { ArrowRight, Square, SquareCheck } from "lucide-react";
import { t } from "../strings";
import { Modal } from "../ui/Modal";
import { PhoneHandoff } from "../ui/PhoneHandoff";
import { planRenames, type RenameItem, type RenameMode, type RenamePlan, type RenameRow } from "./bulkRename";

type Kind = RenameMode["kind"];

const MODES: { kind: Kind; key: string }[] = [
  { kind: "replace", key: "cw_files_br_mode_replace" },
  { kind: "prefix", key: "cw_files_br_mode_prefix" },
  { kind: "suffix", key: "cw_files_br_mode_suffix" },
  { kind: "number", key: "cw_files_br_mode_number" },
];

/** Rows drawn in the preview (the plan checks every row; the rest are counted). */
export const PREVIEW_ROWS = 60;

/** Why a row cannot be renamed, as a sentence. */
export function problemText(r: RenameRow): string {
  switch (r.problem) {
    case "empty":
      return t("cw_files_br_err_empty", { name: r.from });
    case "slash":
      return t("cw_files_br_err_slash", { name: r.from });
    case "dots":
      return t("cw_files_br_err_dots", { name: r.from });
    case "long":
      return t("cw_files_br_err_long", { name: r.from });
    case "unchanged":
      return t("cw_files_br_err_unchanged", { name: r.from });
    case "exists":
      return t("cw_files_br_err_exists", { name: r.to });
    case "twice":
      return t("cw_files_br_err_twice", { name: r.to });
    default:
      return "";
  }
}

/** The mode as typed so far. */
export interface BulkFields {
  kind: Kind;
  find: string;
  replace: string;
  caseSensitive: boolean;
  text: string;
  template: string;
  start: string;
}

export const EMPTY_FIELDS: BulkFields = {
  kind: "replace",
  find: "",
  replace: "",
  caseSensitive: true,
  text: "",
  template: "",
  start: "1",
};

export function modeOf(f: BulkFields): RenameMode {
  switch (f.kind) {
    case "replace":
      return { kind: "replace", find: f.find, replace: f.replace, caseSensitive: f.caseSensitive };
    case "prefix":
      return { kind: "prefix", text: f.text };
    case "suffix":
      return { kind: "suffix", text: f.text };
    case "number": {
      const n = /^\d{1,6}$/.test(f.start.trim()) ? parseInt(f.start.trim(), 10) : 1;
      return { kind: "number", template: f.template, start: n };
    }
  }
}

/** Nothing typed yet for a mode that needs text: the preview waits instead of listing "unchanged". */
export function waiting(f: BulkFields): boolean {
  if (f.kind === "replace") return f.find === "";
  if (f.kind === "prefix" || f.kind === "suffix") return f.text === "";
  return false;
}

function Field({
  label,
  value,
  onChange,
  hint,
  first,
  numeric,
  name,
}: {
  label: string;
  value: string;
  onChange: (v: string) => void;
  hint?: string;
  first?: boolean;
  numeric?: boolean;
  name: string;
}) {
  return (
    <label className="cw-field">
      <span className="cw-dim">{label}</span>
      <input
        className="cw-focusable cw-input"
        type="text"
        inputMode={numeric ? "numeric" : undefined}
        value={value}
        data-br-field={name}
        data-nav-default={first ? "" : undefined}
        autoComplete="off"
        autoCapitalize="off"
        spellCheck={false}
        onChange={(e) => onChange(e.target.value)}
      />
      {hint ? <span className="cw-dim cw-br-hint">{hint}</span> : null}
    </label>
  );
}

/**
 * Rename several items (or one) by a rule: find and replace, add a prefix,
 * add a suffix (before the extension), or numbering. The preview shows old →
 * new for each; Rename (✕) is held while any new name cannot be used, and the
 * first reason shows under the preview.
 */
export function BulkRenameDialog({
  items,
  existing,
  initial = EMPTY_FIELDS,
  onRename,
  onCancel,
}: {
  /** The entries to rename, in the folder's order (numbering follows it). */
  items: RenameItem[];
  /** Every name in the folder (the items' own included). */
  existing: string[];
  initial?: BulkFields;
  onRename: (plan: RenamePlan) => void;
  onCancel: () => void;
}) {
  const [f, setF] = useState<BulkFields>(initial);
  const set = (patch: Partial<BulkFields>) => setF((x) => ({ ...x, ...patch }));
  const plan = useMemo(() => planRenames(items, existing, modeOf(f)), [items, existing, f]);
  const wait = waiting(f);
  const title = items.length === 1 ? t("cw_files_br_title_one", { name: items[0].name }) : t("cw_files_br_title", { n: items.length });
  const shown = plan.rows.slice(0, PREVIEW_ROWS);
  return (
    <Modal label={title} className="cw-bulk-rename" onBack={onCancel}>
      <form
        onSubmit={(e) => {
          e.preventDefault();
          if (plan.ok && !wait) onRename(plan);
        }}
      >
        <h2 className="cw-sheet-title">{title}</h2>
        <div className="cw-br-modes" role="radiogroup" aria-label={t("cw_files_br_mode")}>
          {MODES.map((m) => (
            <button
              key={m.kind}
              type="button"
              role="radio"
              aria-checked={f.kind === m.kind}
              className={`cw-focusable cw-button cw-br-mode${f.kind === m.kind ? " is-on" : ""}`}
              data-br-mode={m.kind}
              onClick={() => set({ kind: m.kind })}
            >
              <span>{t(m.key)}</span>
            </button>
          ))}
        </div>
        <div className="cw-br-body">
          <div className="cw-br-fields">
            {f.kind === "replace" ? (
              <>
                <Field name="find" label={t("cw_files_br_find")} value={f.find} first onChange={(v) => set({ find: v })} />
                <Field name="replace" label={t("cw_files_br_replace")} value={f.replace} onChange={(v) => set({ replace: v })} />
                <button
                  type="button"
                  className="cw-focusable cw-button cw-conflict-all"
                  data-action="br-case"
                  aria-pressed={f.caseSensitive}
                  onClick={() => set({ caseSensitive: !f.caseSensitive })}
                >
                  {f.caseSensitive ? <SquareCheck aria-hidden /> : <Square aria-hidden />}
                  <span>{t("cw_files_br_case")}</span>
                </button>
              </>
            ) : null}
            {f.kind === "prefix" ? (
              <Field name="text" label={t("cw_files_br_prefix")} value={f.text} first onChange={(v) => set({ text: v })} />
            ) : null}
            {f.kind === "suffix" ? (
              <Field
                name="text"
                label={t("cw_files_br_suffix")}
                value={f.text}
                first
                hint={t("cw_files_br_suffix_hint")}
                onChange={(v) => set({ text: v })}
              />
            ) : null}
            {f.kind === "number" ? (
              <>
                <Field
                  name="template"
                  label={t("cw_files_br_template")}
                  value={f.template}
                  first
                  hint={t("cw_files_br_template_hint")}
                  onChange={(v) => set({ template: v })}
                />
                <Field name="start" label={t("cw_files_br_start")} value={f.start} numeric onChange={(v) => set({ start: v })} />
              </>
            ) : null}
          </div>
          <div className="cw-br-preview" aria-label={t("cw_files_br_preview")}>
            <p className="cw-dim">{t("cw_files_br_preview")}</p>
            {wait ? (
              <p className="cw-dim">{t("cw_files_br_waiting")}</p>
            ) : (
              <ul className="cw-br-rows">
                {shown.map((r) => (
                  <li key={r.from} className={`cw-br-row${r.problem ? " is-bad" : ""}`} data-br-row={r.from}>
                    <span className="cw-br-from">{r.from}</span>
                    <ArrowRight aria-hidden />
                    <span className="cw-br-to">{r.to || "—"}</span>
                  </li>
                ))}
                {plan.rows.length > shown.length ? (
                  <li className="cw-dim">{t("cw_files_br_more", { n: plan.rows.length - shown.length })}</li>
                ) : null}
              </ul>
            )}
            <PhoneHandoff />
          </div>
        </div>
        {!wait && plan.first ? (
          <p className="cw-field-error" role="alert" data-br-problem={plan.first.problem ?? ""}>
            {problemText(plan.first)}
          </p>
        ) : null}
        <div className="cw-dialog-buttons">
          <button type="button" className="cw-focusable cw-button" onClick={onCancel}>
            <span>{t("cw_dialog_cancel")}</span>
          </button>
          <button
            type="submit"
            className="cw-focusable cw-button is-primary"
            data-action="br-go"
            disabled={wait || !plan.ok}
          >
            <span>{t("cw_files_br_go", { n: items.length })}</span>
          </button>
        </div>
      </form>
    </Modal>
  );
}
