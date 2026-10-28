import { useEffect, useRef, useState, type ReactNode } from "react";
import { t } from "../strings";
import { Modal } from "../ui/Modal";
import { PhoneHandoff } from "../ui/PhoneHandoff";
import { validName } from "./move";

/** The default check: a file or folder name ("/", "." and ".." refused). */
const checkName = (v: string): string | null => (validName(v) ? null : t("cw_files_bad_name"));

/**
 * Rename / New folder / Go to path: one text box (the PS5 shows its keyboard),
 * OK and Cancel. `check` says why a value cannot be used (shown under the box,
 * the prompt stays open); by default a name with "/" (or "." / "..") is
 * refused here, before any call. An async check (Go to path asks the console)
 * holds OK until it answers. With LAN access on, "Continue on your phone" (a QR
 * code of this page's LAN address) shows under the box.
 */
export function TextPrompt({
  title,
  initial,
  fieldLabel,
  check = checkName,
  extra,
  onSubmit,
  onCancel,
}: {
  title: string;
  initial: string;
  /** The box's label; default "Name". */
  fieldLabel?: string;
  check?: (value: string) => string | null | Promise<string | null>;
  /** Shown under the box (Go to path's pinned folders). */
  extra?: ReactNode;
  onSubmit: (value: string) => void;
  onCancel: () => void;
}) {
  const [value, setValue] = useState(initial);
  const [bad, setBad] = useState<string | null>(null);
  const [checking, setChecking] = useState(false);
  /** False once the prompt is gone: a check that answers late (after Cancel) does nothing. */
  const mounted = useRef(true);
  useEffect(() => {
    mounted.current = true;
    return () => {
      mounted.current = false;
    };
  }, []);
  const submit = () => {
    if (checking) return;
    const r = check(value);
    if (r == null || typeof r === "string") {
      if (r != null) setBad(r);
      else onSubmit(value);
      return;
    }
    setChecking(true);
    r.then(
      (why) => {
        if (!mounted.current) return;
        setChecking(false);
        if (why != null) setBad(why);
        else onSubmit(value);
      },
      () => {
        if (mounted.current) setChecking(false);
      },
    );
  };
  return (
    <Modal label={title} onBack={onCancel}>
      <form
        onSubmit={(e) => {
          e.preventDefault();
          submit();
        }}
      >
        <h2>{title}</h2>
        <label className="cw-field">
          <span className="cw-dim">{fieldLabel ?? t("cw_files_name")}</span>
          <input
            className="cw-focusable cw-input"
            type="text"
            value={value}
            data-nav-default=""
            autoComplete="off"
            autoCapitalize="off"
            spellCheck={false}
            onChange={(e) => {
              setValue(e.target.value);
              setBad(null);
            }}
          />
        </label>
        {bad ? (
          <p className="cw-field-error" role="alert">
            {bad}
          </p>
        ) : null}
        {extra}
        <PhoneHandoff />
        <div className="cw-dialog-buttons">
          <button type="button" className="cw-focusable cw-button" onClick={onCancel}>
            <span>{t("cw_dialog_cancel")}</span>
          </button>
          <button type="submit" className="cw-focusable cw-button is-primary" aria-busy={checking || undefined}>
            <span>{checking ? t("cw_home_loading") : t("cw_files_ok")}</span>
          </button>
        </div>
      </form>
    </Modal>
  );
}
