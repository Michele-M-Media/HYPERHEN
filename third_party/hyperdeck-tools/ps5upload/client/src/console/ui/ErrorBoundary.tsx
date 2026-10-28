import { Component, createRef, type ErrorInfo, type ReactNode } from "react";
import { RotateCw, Undo2 } from "lucide-react";
import { t } from "../strings";

/** This page's own saved settings (theme, language, full screen, Files view): all start with it. */
const PREFIX = "ps5upload.";
/** Kept by a reset: not a setting (the tour would come back). */
const KEEP = new Set(["ps5upload.console.tourSeen"]);

/** Forget this page's saved settings (Reset settings & reload). Never throws. */
export function resetPageSettings(store: Storage | null = safeLocalStorage()): number {
  if (!store) return 0;
  let n = 0;
  try {
    const keys: string[] = [];
    for (let i = 0; i < store.length; i++) {
      const k = store.key(i);
      if (k && k.startsWith(PREFIX) && !KEEP.has(k)) keys.push(k);
    }
    for (const k of keys) {
      store.removeItem(k);
      n++;
    }
  } catch {
    /* blocked storage: nothing was saved there either */
  }
  try {
    sessionStorage.clear();
  } catch {
    /* ignore */
  }
  return n;
}

function safeLocalStorage(): Storage | null {
  try {
    return typeof localStorage !== "undefined" ? localStorage : null;
  } catch {
    return null;
  }
}

interface Props {
  children: ReactNode;
  /** Tests: what the buttons do. */
  reload?: () => void;
  reset?: () => void;
}

/**
 * A render error anywhere in the page shows this instead of a blank screen
 * (the PS5 browser has no other way out than closing it): Reload, or Reset
 * settings & reload when a saved setting is what breaks the page. The error
 * text is shown small, for a bug report.
 */
export class ErrorBoundary extends Component<Props, { error: Error | null }> {
  state: { error: Error | null } = { error: null };
  private first = createRef<HTMLButtonElement>();

  static getDerivedStateFromError(error: unknown): { error: Error } {
    return { error: error instanceof Error ? error : new Error(String(error)) };
  }

  componentDidCatch(error: unknown, info: ErrorInfo): void {
    console.error("console page: render error", error, info.componentStack);
  }

  componentDidUpdate(): void {
    if (this.state.error) this.first.current?.focus();
  }

  render(): ReactNode {
    const { error } = this.state;
    if (!error) return this.props.children;
    const reload = this.props.reload ?? (() => window.location.reload());
    const reset =
      this.props.reset ??
      (() => {
        resetPageSettings();
        window.location.reload();
      });
    return (
      <div className="cw-offline" role="alertdialog" aria-labelledby="cw-error-title" data-nav-modal>
        <h1 id="cw-error-title">{t("cw_error_title")}</h1>
        <p>{t("cw_error_body")}</p>
        <pre className="cw-error-detail">{error.message || error.name}</pre>
        <div className="cw-dialog-buttons">
          <button
            ref={this.first}
            type="button"
            className="cw-focusable cw-button is-primary"
            data-nav-default=""
            onClick={reload}
          >
            <RotateCw aria-hidden />
            <span>{t("cw_error_reload")}</span>
          </button>
          <button type="button" className="cw-focusable cw-button" onClick={reset}>
            <Undo2 aria-hidden />
            <span>{t("cw_error_reset")}</span>
          </button>
        </div>
      </div>
    );
  }
}
