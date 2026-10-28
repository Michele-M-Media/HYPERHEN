import { useCallback, useEffect, useRef, useState } from "react";
import { RotateCw, X } from "lucide-react";
import { probeHelper } from "../api";
import { t } from "../strings";
import { pollWhileVisible } from "../ui/visiblePoll";

/** While the offline screen shows, the helper is asked again this often. */
export const OFFLINE_RETRY_MS = 5000;

const reloadPage = () => window.location.reload();

/**
 * Shown instead of the page when the helper did not answer GET /api/token: the
 * page came from the browser's offline copy (offlineCopy.ts) and the helper is
 * not running. It asks again every few seconds (and on Try again); once the
 * helper answers the page reloads, so the helper's own (maybe newer) page
 * opens. Close leaves the browser (PS5: back to the home screen).
 */
export function OfflinePage({
  probe = probeHelper,
  onBack = reloadPage,
}: {
  probe?: () => Promise<boolean>;
  /** The helper answered. */
  onBack?: () => void;
}) {
  const [checking, setChecking] = useState(false);
  const retry = useRef<HTMLButtonElement>(null);
  const live = useRef(true);
  const busy = useRef(false);

  const check = useCallback(() => {
    if (busy.current) return;
    busy.current = true;
    setChecking(true);
    void probe().then((up) => {
      busy.current = false;
      if (!live.current) return;
      setChecking(false);
      if (up) onBack();
    });
  }, [probe, onBack]);

  useEffect(() => {
    live.current = true;
    retry.current?.focus();
    // The first check ran before this screen (that is why it shows): wait one period.
    let first = true;
    const stop = pollWhileVisible(() => {
      if (first) {
        first = false;
        return;
      }
      check();
    }, OFFLINE_RETRY_MS);
    return () => {
      live.current = false;
      stop();
    };
  }, [check]);

  return (
    <div className="cw-offline" role="alertdialog" aria-labelledby="cw-offline-title" data-nav-modal>
      <h1 id="cw-offline-title">{t("cw_offline_title")}</h1>
      <p>{t("cw_offline_body")}</p>
      <div className="cw-dialog-buttons">
        <button
          ref={retry}
          type="button"
          className="cw-focusable cw-button is-primary"
          data-nav-default=""
          aria-busy={checking || undefined}
          onClick={check}
        >
          <RotateCw aria-hidden />
          <span>{checking ? t("cw_offline_checking") : t("cw_offline_retry")}</span>
        </button>
        <button
          type="button"
          className="cw-focusable cw-button"
          onClick={() => {
            try {
              window.close();
            } catch {
              /* not closable */
            }
          }}
        >
          <X aria-hidden />
          <span>{t("cw_offline_close")}</span>
        </button>
      </div>
    </div>
  );
}
