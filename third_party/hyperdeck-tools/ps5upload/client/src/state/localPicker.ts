// Global in-app file/folder picker, used on Android where the native
// dialog can't return a real filesystem path (scoped storage gives
// content:// URIs / no folder paths). One <LocalPathPicker/> is mounted
// at the app root; any screen requests a path imperatively:
//
//   import { pickLocalPath } from "../../state/localPicker";
//   const path = await pickLocalPath({ mode: "folder" });
//
// Desktop screens keep using @tauri-apps/plugin-dialog (real paths); they
// branch on isAndroid() before calling this. Every platform uses it to browse
// a saved server (`source: { connectionId }`).

import { create } from "zustand";

export interface LocalPickOptions {
  /** "any": pick a file, or use the open folder (a game is either). */
  mode: "file" | "folder" | "any";
  /** Optional modal title override. */
  title?: string;
  /** Only show files with these extensions (folders are always shown). */
  filters?: { name: string; extensions: string[] }[];
  /** A saved server to browse instead of this device (resolves with a `remote://` path), or
   *  the console at `console` (resolves with a `ps5://` path). */
  source?: "local" | { connectionId: string } | { console: string };
  /** Opened to look around (Connections → Browse): each file row gets Install / Send to PS5. */
  actions?: { onInstall: (path: string) => void; onSend: (path: string) => void };
}

interface PendingReq extends LocalPickOptions {
  /** Rows get checkboxes; the picker answers with every ticked path. */
  multiple?: boolean;
  resolve: (paths: string[] | null) => void;
}

interface LocalPickerState {
  pending: PendingReq | null;
  /** Open the picker; resolves with the chosen real path, or null. */
  open: (opts: LocalPickOptions) => Promise<string | null>;
  /** Open it for several; resolves with every path picked, or [] when cancelled. */
  openMany: (opts: LocalPickOptions) => Promise<string[]>;
  /** Close, resolving the in-flight request. */
  settle: (path: string | null) => void;
  /** Close with several paths (a multi-pick's Add). */
  settleMany: (paths: string[] | null) => void;
}

export const useLocalPickerStore = create<LocalPickerState>((set, get) => {
  const request = (opts: LocalPickOptions, multiple: boolean) =>
    new Promise<string[] | null>((resolve) => {
      // Only one picker at a time — cancel any already-open request.
      const prev = get().pending;
      if (prev) prev.resolve(null);
      set({ pending: { ...opts, multiple, resolve } });
    });
  const settleMany = (paths: string[] | null) => {
    const req = get().pending;
    if (req) {
      req.resolve(paths);
      set({ pending: null });
    }
  };
  return {
    pending: null,
    open: (opts) => request(opts, false).then((r) => r?.[0] ?? null),
    openMany: (opts) => request(opts, true).then((r) => r ?? []),
    settle: (path) => settleMany(path === null ? null : [path]),
    settleMany,
  };
});

/** Imperative helper for screens. */
export const pickLocalPath = (opts: LocalPickOptions) =>
  useLocalPickerStore.getState().open(opts);

/** Several at once: files (or folders, in folder mode) ticked across any folders browsed. */
export const pickLocalPaths = (opts: LocalPickOptions) =>
  useLocalPickerStore.getState().openMany(opts);
