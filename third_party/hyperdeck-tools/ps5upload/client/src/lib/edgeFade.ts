import { useEffect, type RefObject } from "react";

/**
 * How far to fade each edge of a sideways-scrolling strip.
 *
 * A strip that clips its content with no cue (the console tabs cut mid-pill
 * beside the search field) reads as a layout bug and hides that there is more
 * to scroll to. `.fade-x` in index.css turns these two widths into a soft fade
 * on the clipped edge(s). "start" and "end" are in reading direction; the CSS
 * mirrors them for right-to-left.
 */
export interface EdgeFade {
  /** px to fade at the inline-start edge; 0 when nothing is hidden there. */
  start: number;
  end: number;
}

/** The widest a fade gets. */
export const MAX_FADE_PX = 40;

/** Anything within a pixel of an edge counts as at the edge (zoom rounding). */
const SLOP = 1;

export function edgeFade(el: {
  scrollLeft: number;
  scrollWidth: number;
  clientWidth: number;
}): EdgeFade {
  const hidden = el.scrollWidth - el.clientWidth;
  if (hidden <= SLOP) return { start: 0, end: 0 };
  // In a right-to-left strip scrollLeft runs from 0 down to -hidden.
  const before = Math.min(Math.abs(el.scrollLeft), hidden);
  const after = hidden - before;
  // Never wider than what is hidden: a few clipped pixels get a few pixels of
  // fade, not one that swallows the whole last button.
  const width = (px: number) => (px <= SLOP ? 0 : Math.min(px, MAX_FADE_PX));
  return { start: width(before), end: width(after) };
}

/**
 * Keep `ref`'s fade in step with its scroll position and size. The element
 * needs the `fade-x` class. Written straight to the element's style rather
 * than through state, so scrolling the strip re-renders nothing. `dep` is
 * anything that changes what the strip holds.
 */
export function useEdgeFade(ref: RefObject<HTMLElement | null>, dep?: unknown): void {
  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    const measure = () => {
      const { start, end } = edgeFade(el);
      el.style.setProperty("--fade-start", `${start}px`);
      el.style.setProperty("--fade-end", `${end}px`);
    };
    measure();
    el.addEventListener("scroll", measure, { passive: true });
    const ro =
      typeof ResizeObserver === "undefined" ? null : new ResizeObserver(measure);
    ro?.observe(el);
    for (const child of Array.from(el.children)) ro?.observe(child);
    return () => {
      el.removeEventListener("scroll", measure);
      ro?.disconnect();
    };
  }, [ref, dep]);
}
