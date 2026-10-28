import { useEffect, useRef, type ReactNode } from "react";
import { onNavAction } from "../nav";
import { useBackLevel } from "../backHistory";
import { useModalFocus } from "./restoreFocus";

/**
 * A nav modal over the page (actions sheet, path picker, viewer, prompts): the
 * D-pad stays inside, focus starts on its [data-nav-default] (else its first
 * control) and goes back to the opener when it closes. ○ calls onBack (close,
 * or up one level in the path picker); △ calls onActions, else closes too.
 * Every other action is swallowed while it is open.
 */
export function Modal({
  label,
  className,
  onBack,
  onActions,
  children,
}: {
  label: string;
  className?: string;
  onBack: () => void;
  onActions?: () => void;
  children: ReactNode;
}) {
  const root = useRef<HTMLDivElement>(null);
  useModalFocus(root);
  // Open: the browser's Back (○) closes it (or steps back inside it).
  useBackLevel(true);
  useEffect(
    () =>
      onNavAction(
        (a) => {
          if (a === "back") onBack();
          else if (a === "actions") (onActions ?? onBack)();
          return true;
        },
        () => root.current,
      ),
    [onBack, onActions],
  );
  return (
    <div className="cw-scrim">
      <div
        ref={root}
        className={`cw-dialog${className ? ` ${className}` : ""}`}
        role="dialog"
        aria-modal="true"
        aria-label={label}
        data-nav-modal
      >
        {children}
      </div>
    </div>
  );
}
