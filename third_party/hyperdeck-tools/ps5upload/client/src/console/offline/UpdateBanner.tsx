import { useEffect, useRef } from "react";
import { RotateCw } from "lucide-react";
import { t } from "../strings";
import { dismissUpdate, useUpdateReady } from "./update";

/** A newer page is waiting (update.ts): one press reloads into it; Later keeps working on this one. */
export function UpdateBanner({ reload = () => window.location.reload() }: { reload?: () => void }) {
  const ready = useUpdateReady();
  const button = useRef<HTMLButtonElement>(null);
  useEffect(() => {
    if (ready) button.current?.focus();
  }, [ready]);
  if (!ready) return null;
  return (
    <div className="cw-banner" role="alertdialog" aria-labelledby="cw-update-title" data-nav-modal>
      <h2 id="cw-update-title">{t("cw_update_ready")}</h2>
      <div className="cw-dialog-buttons">
        <button type="button" className="cw-focusable cw-button" onClick={dismissUpdate}>
          <span>{t("cw_update_later")}</span>
        </button>
        <button ref={button} type="button" className="cw-focusable cw-button is-primary" onClick={reload}>
          <RotateCw aria-hidden />
          <span>{t("cw_error_reload")}</span>
        </button>
      </div>
    </div>
  );
}
