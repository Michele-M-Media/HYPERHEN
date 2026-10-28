import { useEffect, useRef, useState } from "react";
import { ArrowUp, ChevronRight } from "lucide-react";
import type { Volume } from "../home/Home";
import { focusElement } from "../nav";
import { t } from "../strings";
import { barItems, pathSegments } from "./pathParts";

/**
 * Up, then the folder shown as buttons: ✕ on a segment jumps there. A long
 * path shows its drive, "…" and the last two folders; ✕ on "…" shows them all
 * (focus moves to the first one it revealed). Keyed by path where it is used,
 * so a new folder starts collapsed.
 */
export function PathBar({
  path,
  volumes,
  onOpen,
  onUp,
}: {
  path: string;
  volumes: Volume[];
  onOpen: (path: string) => void;
  onUp: () => void;
}) {
  const [expanded, setExpanded] = useState(false);
  const root = useRef<HTMLElement>(null);
  /** After "…": focus the first segment it revealed. */
  const revealAt = useRef<number | null>(null);
  const items = barItems(pathSegments(path, volumes), expanded);

  useEffect(() => {
    if (revealAt.current == null) return;
    const el = root.current?.querySelectorAll<HTMLElement>("[data-seg]")[revealAt.current];
    revealAt.current = null;
    if (el) focusElement(el);
  }, [expanded]);

  return (
    <nav ref={root} className="cw-pathbar" aria-label={t("cw_files_path")}>
      <button type="button" className="cw-focusable cw-button" data-action="up" onClick={onUp}>
        <ArrowUp aria-hidden />
        <span>{t("cw_files_up")}</span>
      </button>
      <ol className="cw-crumbs">
        {items.map((it, i) => (
          <li key={it.kind === "seg" ? it.seg.path : "more"} className="cw-crumb">
            {i > 0 ? <ChevronRight className="cw-crumb-sep" aria-hidden /> : null}
            {it.kind === "more" ? (
              <button
                type="button"
                className="cw-focusable cw-crumb-button"
                data-action="more-path"
                aria-label={t("cw_files_path_more", { n: it.hidden.length })}
                title={it.hidden.map((s) => s.label).join("/")}
                onClick={() => {
                  revealAt.current = 1;
                  setExpanded(true);
                }}
              >
                …
              </button>
            ) : (
              <button
                type="button"
                className={`cw-focusable cw-crumb-button${it.last ? " is-current" : ""}`}
                data-seg={it.seg.path}
                aria-current={it.last ? "location" : undefined}
                onClick={() => onOpen(it.seg.path)}
              >
                {it.seg.label}
              </button>
            )}
          </li>
        ))}
      </ol>
    </nav>
  );
}
