/**
 * Run `fn` now and every `ms` while the page is visible. While it is hidden
 * (the PS5 browser in the background, another tab) nothing runs; when it shows
 * again `fn` runs at once and the interval starts over. Returns the stop function.
 */
export interface VisibilityDoc {
  readonly hidden: boolean;
  addEventListener(type: "visibilitychange", fn: () => void): void;
  removeEventListener(type: "visibilitychange", fn: () => void): void;
}

export function pollWhileVisible(fn: () => void, ms: number, doc: VisibilityDoc = document): () => void {
  let timer: ReturnType<typeof setInterval> | undefined;
  const start = () => {
    if (timer !== undefined) return;
    fn();
    timer = setInterval(fn, ms);
  };
  const pause = () => {
    if (timer !== undefined) clearInterval(timer);
    timer = undefined;
  };
  const onChange = () => (doc.hidden ? pause() : start());
  doc.addEventListener("visibilitychange", onChange);
  if (!doc.hidden) start();
  return () => {
    pause();
    doc.removeEventListener("visibilitychange", onChange);
  };
}
