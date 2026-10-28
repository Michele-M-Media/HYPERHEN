/**
 * A folder's entries, read a page at a time from /api/fs/list.
 *
 * Folders can hold thousands of entries (a Unity game's StreamingAssets), and
 * one helper reply holds at most 256: the first page loads at once, the next
 * only when asked (loadMore, from the "Show more" row), never two at a time.
 * `next` in the reply is the next page's offset (ruling R9), used as-is.
 */
import { useCallback, useEffect, useRef, useState } from "react";
import { call } from "../api";
import { LOAD_ALL_CAP, sortEntries as sortListing } from "./listing";

export const LIST_PAGE = 256;

/** One /api/fs/list entry (payload/src/webui_codec.c webui_list_to_json). */
export interface Entry {
  name: string;
  kind: "file" | "dir" | "link" | "other";
  size: number;
  mtime: number | null;
}

interface ListReply {
  entries?: Entry[];
  more?: boolean;
  next?: number;
}

export interface DirState {
  /** Folders first, then by name (numbers in names in numeric order). */
  entries: Entry[];
  /** More pages wait on the helper. */
  more: boolean;
  loading: boolean;
  /** The first page has come in. */
  loaded: boolean;
  /** The last read failed (null after a good one). */
  error: unknown;
}

export const EMPTY_DIR: DirState = { entries: [], more: false, loading: false, loaded: false, error: null };

/** Folders first, then by name (numbers in names in numeric order). */
export function sortEntries(entries: Entry[]): Entry[] {
  return sortListing(entries, "name", "asc");
}

/** "ok": the pages were read; "skipped": nothing ran (busy, done, folder left); "failed": a page failed. */
export type LoadAllResult = "ok" | "skipped" | "failed";

export interface DirLoader {
  /** Read the next page (the first on the first call). No-op while one is read or when done. */
  loadMore(): Promise<void>;
  /** Read every remaining page, up to `cap` entries in all (one emit at the end). */
  loadAll(cap: number): Promise<LoadAllResult>;
  /** Read again from the start, as many pages as were shown. */
  refresh(): Promise<void>;
  /** Drop every answer from now on (the folder was left). */
  stop(): void;
}

/** The paging behind useDir, with no React in it. */
export function createDirLoader(path: string, onState: (s: DirState) => void): DirLoader {
  let raw: Entry[] = [];
  let next = 0;
  let more = true;
  let pages = 0;
  let busy = false;
  let stopped = false;
  let loaded = false;
  /** A refresh asked for while a page was being read: run it after. */
  let pending = false;

  const settle = () => {
    busy = false;
    if (pending && !stopped) {
      pending = false;
      void loader.refresh();
    }
  };

  const emit = (loading: boolean, error: unknown) => {
    if (stopped) return;
    onState({ entries: sortEntries(raw), more: loaded ? more : false, loading, loaded, error });
  };

  async function page(offset: number): Promise<ListReply> {
    return call<ListReply>("/api/fs/list", { path, offset, limit: LIST_PAGE });
  }

  const loader: DirLoader = {
    async loadMore() {
      if (busy || stopped || (loaded && !more)) return;
      busy = true;
      emit(true, null);
      try {
        const r = await page(next);
        if (stopped) return;
        raw = raw.concat(Array.isArray(r?.entries) ? r.entries : []);
        more = !!r?.more;
        next = typeof r?.next === "number" ? r.next : next + LIST_PAGE;
        pages++;
        loaded = true;
        emit(false, null);
      } catch (e) {
        emit(false, e);
      } finally {
        settle();
      }
    },
    async loadAll(cap) {
      if (busy || stopped || (loaded && !more)) return "skipped";
      busy = true;
      emit(true, null);
      let result: LoadAllResult = "ok";
      try {
        while (!loaded || more) {
          if (loaded && raw.length >= cap) break;
          const r = await page(next);
          if (stopped) return "skipped";
          raw = raw.concat(Array.isArray(r?.entries) ? r.entries : []);
          more = !!r?.more;
          next = typeof r?.next === "number" ? r.next : next + LIST_PAGE;
          pages++;
          loaded = true;
        }
        emit(false, null);
      } catch (e) {
        result = "failed";
        emit(false, e);
      } finally {
        settle();
      }
      return result;
    },
    async refresh() {
      if (stopped) return;
      if (busy) {
        pending = true;
        return;
      }
      busy = true;
      emit(true, null);
      const want = Math.max(1, pages);
      let got: Entry[] = [];
      let at = 0;
      let left = true;
      let n = 0;
      try {
        while (n < want && left) {
          const r = await page(at);
          if (stopped) return;
          got = got.concat(Array.isArray(r?.entries) ? r.entries : []);
          left = !!r?.more;
          at = typeof r?.next === "number" ? r.next : at + LIST_PAGE;
          n++;
        }
        raw = got;
        next = at;
        more = left;
        pages = n;
        loaded = true;
        emit(false, null);
      } catch (e) {
        emit(false, e);
      } finally {
        settle();
      }
    },
    stop() {
      stopped = true;
    },
  };
  return loader;
}

export interface DirHandle extends DirState {
  loadMore(): void;
  /** Read all the remaining pages (bounded by LOAD_ALL_CAP). */
  loadAll(): Promise<LoadAllResult>;
  refresh(): void;
}

/** The folder at `path` (null: none, e.g. the drive list is shown). */
export function useDir(path: string | null): DirHandle {
  // Keyed by path: the render right after a folder change already shows the
  // new (empty) state, never the old folder's rows.
  const [held, setHeld] = useState<{ path: string | null; state: DirState }>({ path, state: EMPTY_DIR });
  const loader = useRef<DirLoader | null>(null);

  useEffect(() => {
    if (path == null) return;
    const l = createDirLoader(path, (state) => setHeld({ path, state }));
    loader.current = l;
    void l.loadMore();
    return () => {
      l.stop();
      if (loader.current === l) loader.current = null;
      // Forget this folder: coming back to it later starts from an empty list.
      setHeld({ path: null, state: EMPTY_DIR });
    };
  }, [path]);

  const loadMore = useCallback(() => {
    void loader.current?.loadMore();
  }, []);
  const loadAll = useCallback(async (): Promise<LoadAllResult> => {
    return (await loader.current?.loadAll(LOAD_ALL_CAP)) ?? "skipped";
  }, []);
  const refresh = useCallback(() => {
    void loader.current?.refresh();
  }, []);

  const state = held.path === path ? held.state : EMPTY_DIR;
  return { ...state, loadMore, loadAll, refresh };
}
