/**
 * Files' clipboard and the paste conflict being asked about.
 *
 * Both live outside any component (like batch.ts): the clipboard stays while
 * folders change and while another section is open, for the whole page
 * session. A conflict is asked only while Files is open: Stop, or Files
 * closing, answers the waiting one (and every later one) with Skip
 * (skipPendingConflict), so a paste never waits behind a dialog nobody sees.
 * Nothing here is saved: a reload empties it.
 *
 * Ruling R22: ○ (browser Back) never clears the clipboard, at any level. Only
 * the bar's Clear empties it; a new Copy / Cut replaces it; a cut item leaves
 * it once it has moved (or was skipped on purpose), and a copy stays to be
 * pasted again.
 */
import { useEffect, useState } from "react";
import type { Conflict, Decision, PasteMode } from "./paste";

export interface ClipItem {
  /** Full path. */
  path: string;
  /** A folder (Keep both does not split an extension off a folder's name). */
  dir: boolean;
}

export interface Clip {
  mode: PasteMode;
  items: ClipItem[];
}

/** A tiny observable value with a React hook. */
function store<T>(initial: T) {
  let value = initial;
  const listeners = new Set<() => void>();
  return {
    get: () => value,
    set(next: T) {
      value = next;
      listeners.forEach((fn) => fn());
    },
    use(): T {
      const [v, setV] = useState(value);
      useEffect(() => {
        setV(value);
        const fn = () => setV(value);
        listeners.add(fn);
        return () => {
          listeners.delete(fn);
        };
      }, []);
      return v;
    },
  };
}

const clip = store<Clip | null>(null);

export function getClipboard(): Clip | null {
  return clip.get();
}

/** Copy or Cut: the clipboard now holds these items (whatever it held before is replaced). */
export function setClipboard(mode: PasteMode, items: ClipItem[]): void {
  clip.set(items.length ? { mode, items } : null);
}

export function clearClipboard(): void {
  clip.set(null);
}

/** Take these paths out (a cut item that has moved); empty → cleared. */
export function dropFromClipboard(paths: string[]): void {
  const c = clip.get();
  if (!c || !paths.length) return;
  const gone = new Set(paths);
  const left = c.items.filter((i) => !gone.has(i.path));
  if (left.length !== c.items.length) clip.set(left.length ? { ...c, items: left } : null);
}

export const useClipboard = (): Clip | null => clip.use();

/* ---- the conflict being asked ---- */

export interface PendingConflict {
  conflict: Conflict;
  answer: (d: Decision) => void;
}

const pending = store<PendingConflict | null>(null);

/** Ask the page about one conflict; resolves when the dialog is answered. */
export function askConflict(conflict: Conflict): Promise<Decision> {
  return new Promise((resolve) => {
    pending.set({
      conflict,
      answer: (d) => {
        pending.set(null);
        resolve(d);
      },
    });
  });
}

/**
 * Answer a waiting conflict with Skip for it and every later one (Stop, or
 * Files closing): the batch never sits busy behind a dialog nobody can see.
 */
export function skipPendingConflict(): void {
  pending.get()?.answer({ choice: "skip", all: true });
}

export function currentConflict(): PendingConflict | null {
  return pending.get();
}

export const useConflict = (): PendingConflict | null => pending.use();
