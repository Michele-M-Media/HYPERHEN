import { useEffect } from "react";
import { Download, RefreshCw } from "lucide-react";
import { focusElement } from "../nav";
import { t } from "../strings";
import { Modal } from "../ui/Modal";
import type { FinderState } from "./finder";
import { CHEAT_REPOS, sameVersion, type CheatEntry } from "./repos";

const repoName = (id: string) => CHEAT_REPOS.find((r) => r.id === id)?.name ?? id;

function EntryRow({
  entry,
  match,
  busy,
  disabled,
  done,
  first,
  onDownload,
}: {
  entry: CheatEntry;
  match: boolean;
  busy: boolean;
  disabled: boolean;
  done: boolean;
  first: boolean;
  onDownload: (e: CheatEntry) => void;
}) {
  let tag: string | null = null;
  if (busy) tag = t("cw_cheats_find_downloading");
  else if (done) tag = t("cw_cheats_find_downloaded");
  return (
    <li className="cw-finder-item">
      <button
        type="button"
        className={`cw-focusable cw-button cw-finder-entry${match ? " is-match" : ""}`}
        aria-busy={busy || undefined}
        aria-disabled={disabled || undefined}
        data-cheat-file={entry.filename}
        data-nav-default={first ? "" : undefined}
        onClick={() => onDownload(entry)}
      >
        <Download aria-hidden />
        <span className="cw-finder-text">
          <span className="cw-finder-name">
            {entry.gameVersion ? `v${entry.gameVersion}` : t("cw_cheats_find_any_version")}
            {match ? <span className="cw-tag is-on">{t("cw_cheats_find_your_version")}</span> : null}
          </span>
          <span className="cw-dim cw-finder-meta">
            {entry.filename} · {entry.format.toUpperCase()} · {repoName(entry.repoId)}
            {entry.gameTitle ? ` · ${entry.gameTitle}` : ""}
          </span>
        </span>
        {tag ? <span className="cw-tag">{tag}</span> : null}
      </button>
    </li>
  );
}

/** The finder over the Cheats section: ✕ on a file downloads it to the console; ○ or Close leaves. */
export function FindCheats({
  state,
  onDownload,
  onRetry,
  onClose,
}: {
  state: FinderState;
  onDownload: (e: CheatEntry) => void;
  onRetry: () => void;
  onClose: () => void;
}) {
  // The list (or Try again) replaces "Looking…" after the modal took focus: focus moves to it.
  const ready = state.kind === "list" ? `list:${state.entries.length}` : state.kind;
  useEffect(() => {
    if (ready !== "offline" && !ready.startsWith("list:")) return;
    const el = document.querySelector<HTMLElement>(".cw-finder [data-nav-default]");
    if (el) focusElement(el);
  }, [ready]);
  if (state.kind === "closed") return null;
  const title = t("cw_cheats_find_title", { title: state.title });
  let body;
  if (state.kind === "loading") {
    body = (
      <p className="cw-dim" role="status">
        {t("cw_cheats_find_loading")}
      </p>
    );
  } else if (state.kind === "offline") {
    body = (
      <div className="cw-msg is-error" role="alert">
        {t("cw_cheats_find_offline")}
      </div>
    );
  } else if (state.entries.length === 0) {
    body = (
      <>
        <p role="status">{t("cw_cheats_find_none")}</p>
        {state.partial ? <p className="cw-dim">{t("cw_cheats_find_partial")}</p> : null}
      </>
    );
  } else {
    const v = state.version;
    const noMatch = !!v && !state.entries.some((e) => sameVersion(e.gameVersion, v));
    body = (
      <>
        <p className="cw-finder-hint">{t("cw_cheats_find_hint")}</p>
        <p className="cw-dim cw-finder-hint">
          {v ? <span className="cw-finder-version">{t("cw_cheats_find_version", { v })}</span> : null}
          {t("cw_cheats_find_how")}
        </p>
        {noMatch ? <div className="cw-msg">{t("cw_cheats_find_no_match", { v: v ?? "" })}</div> : null}
        {state.partial ? <p className="cw-dim cw-finder-hint">{t("cw_cheats_find_partial")}</p> : null}
        <ul className="cw-finder-list">
          {state.entries.map((e, i) => (
            <EntryRow
              key={`${e.repoId}/${e.filename}`}
              entry={e}
              match={sameVersion(e.gameVersion, v)}
              busy={state.busy === e.filename}
              disabled={state.busy !== null}
              done={state.done.includes(e.filename)}
              first={i === 0}
              onDownload={onDownload}
            />
          ))}
        </ul>
      </>
    );
  }
  const message = state.kind === "list" ? state.message : null;
  return (
    <Modal label={title} className="cw-finder" onBack={onClose}>
      <h2 className="cw-sheet-title">{title}</h2>
      {body}
      {message ? (
        <div className={`cw-msg ${message.tone === "error" ? "is-error" : "is-info"}`} role={message.tone === "error" ? "alert" : "status"}>
          {message.text}
        </div>
      ) : null}
      <div className="cw-dialog-buttons">
        {state.kind === "offline" ? (
          <button type="button" className="cw-focusable cw-button" data-nav-default="" onClick={onRetry}>
            <RefreshCw aria-hidden />
            <span>{t("cw_cheats_find_retry")}</span>
          </button>
        ) : null}
        <button type="button" className="cw-focusable cw-button" data-action="finder-close" onClick={onClose}>
          {t("cw_cheats_find_close")}
        </button>
      </div>
    </Modal>
  );
}
