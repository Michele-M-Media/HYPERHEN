import type { CollectionScanStatus } from "../../api/collection";

export type ScanPhase = "finding" | "reading" | "naming" | "saving";

export interface ScanView {
  phase: ScanPhase;
  /** 0..1 for the bar; null while there is nothing to measure yet (it pulses). */
  value: number | null;
  /** "{done} of {total}" for the phase that counts something. */
  counts: { done: number; total: number } | null;
  /** "Folder {n} of {total}" while several folders are searched. */
  folder: { n: number; total: number } | null;
  /** The folder being searched, or the item or game being read. */
  current: string;
}

const fraction = (done: number, total: number) => (total > 0 ? Math.min(1, done / total) : null);

/** What the Collection's progress card shows for a running scan. */
export function scanView(st: CollectionScanStatus): ScanView {
  // An engine without `phase` reports only found/done: nothing found yet = still finding.
  const phase: ScanPhase = st.phase ?? (st.found > 0 ? "reading" : "finding");
  const rootsTotal = st.roots_total ?? 0;
  const folder =
    phase === "finding" && rootsTotal > 1
      ? { n: Math.min(rootsTotal, (st.roots_done ?? 0) + 1), total: rootsTotal }
      : null;
  const current = st.current ?? "";
  switch (phase) {
    case "reading":
      return {
        phase,
        value: fraction(st.done, st.found),
        counts: st.found > 0 ? { done: st.done, total: st.found } : null,
        folder: null,
        current,
      };
    case "naming": {
      const total = st.games_total ?? 0;
      const done = st.games_done ?? 0;
      return {
        phase,
        value: fraction(done, total),
        counts: total > 0 ? { done, total } : null,
        folder: null,
        current,
      };
    }
    case "saving":
      return { phase, value: 1, counts: null, folder: null, current: "" };
    default:
      return { phase: "finding", value: null, counts: null, folder, current };
  }
}

/** Time since `startMs`: "1:05", or "1:02:05" past an hour. */
export function elapsedLabel(startMs: number, nowMs: number): string {
  const total = Math.max(0, Math.floor((nowMs - startMs) / 1000));
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = String(total % 60).padStart(2, "0");
  return h > 0 ? `${h}:${String(m).padStart(2, "0")}:${s}` : `${m}:${s}`;
}
