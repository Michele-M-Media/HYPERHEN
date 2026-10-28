/**
 * Long names scroll while their row is focused (Payload Manager's marquee): a
 * name cut off with "…" slides to its end and back, so the whole of it can be
 * read on the TV without opening Info. One document-level focusin/focusout
 * pair for the whole page (no per-row work; a 10k-entry folder costs
 * nothing until a row is focused). The motion itself is CSS (.cw-marquee in
 * console.css), only under prefers-reduced-motion: no-preference; with
 * reduced motion the name stays cut off as before.
 */

/** The names that may scroll, inside a focused control. */
export const MARQUEE_SELECTOR = ".cw-file-name";
const ON = "cw-marquee";

/** Pixels a name must slide to show its end; 0 when it fits (1 px of rounding is not overflow). */
export function marqueeShift(scrollWidth: number, clientWidth: number): number {
  const over = Math.ceil(scrollWidth - clientWidth);
  return over > 1 ? over : 0;
}

/** Seconds for one slide: about 40 px a second, never under 3 s or over 15 s. */
export function marqueeSeconds(shift: number): number {
  return Math.min(15, Math.max(3, Math.round((shift / 40) * 10) / 10));
}

function start(root: Element): void {
  const names = root.querySelectorAll<HTMLElement>(MARQUEE_SELECTOR);
  for (let i = 0; i < names.length && i < 4; i++) {
    const el = names[i];
    const shift = marqueeShift(el.scrollWidth, el.clientWidth);
    if (!shift) continue;
    el.style.setProperty("--cw-marquee", `${shift}px`);
    el.style.setProperty("--cw-marquee-s", `${marqueeSeconds(shift)}s`);
    el.classList.add(ON);
  }
}

function stop(root: Element): void {
  root.querySelectorAll<HTMLElement>(`.${ON}`).forEach((el) => {
    el.classList.remove(ON);
    el.style.removeProperty("--cw-marquee");
    el.style.removeProperty("--cw-marquee-s");
  });
}

/** Listen on `doc` (the page's document); returns the stop function. */
export function installMarquee(doc: Document = document): () => void {
  const onIn = (e: FocusEvent) => {
    const el = e.target as Element | null;
    if (el && typeof el.querySelectorAll === "function") start(el);
  };
  const onOut = (e: FocusEvent) => {
    const el = e.target as Element | null;
    if (el && typeof el.querySelectorAll === "function") stop(el);
  };
  doc.addEventListener("focusin", onIn);
  doc.addEventListener("focusout", onOut);
  return () => {
    doc.removeEventListener("focusin", onIn);
    doc.removeEventListener("focusout", onOut);
  };
}
