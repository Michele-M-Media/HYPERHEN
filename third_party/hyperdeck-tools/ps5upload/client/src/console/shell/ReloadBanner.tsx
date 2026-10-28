import { useEffect, useRef } from "react";
import { RotateCw } from "lucide-react";
import { t } from "../strings";

/**
 * Shown when the API answers 401: the helper restarted (a takeover or a
 * relaunch) and has a new token this page does not know. Reloading fetches it.
 * A nav modal, so the D-pad stays on its button.
 */
export function ReloadBanner() {
  const button = useRef<HTMLButtonElement>(null);
  useEffect(() => {
    button.current?.focus();
  }, []);
  return (
    <div className="cw-banner" role="alertdialog" aria-labelledby="cw-reload-title" data-nav-modal>
      <h2 id="cw-reload-title">{t("cw_reload_title")}</h2>
      <p>{t("cw_reload_body")}</p>
      <button
        ref={button}
        type="button"
        className="cw-focusable cw-button is-primary"
        onClick={() => window.location.reload()}
      >
        <RotateCw aria-hidden />
        <span>{t("cw_reload_button")}</span>
      </button>
    </div>
  );
}
