import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  ArrowDownUp,
  CheckCheck,
  ChevronsDown,
  ClipboardPaste,
  ClipboardX,
  Ellipsis,
  ChartBar,
  FilePlus,
  Filter,
  FilterX,
  FolderPlus,
  ListChecks,
  ListX,
  RotateCw,
  Search,
  Square,
  SquareCheck,
  Star,
  TextCursorInput,
  Upload,
  type LucideIcon,
} from "lucide-react";
import { formatBytes } from "../../lib/format";
import { ApiError, call } from "../api";
import { errorText, type Message, type Volume } from "../home/Home";
import { useJobs, type Job } from "../home/useJobs";
import { jobPercent } from "../home/TaskRow";
import { useBackLevel } from "../backHistory";
import { onNavAction, topModal } from "../nav";
import type { SectionContext } from "../sections";
import { t } from "../strings";
import { ConfirmDialog } from "../ui/ConfirmDialog";
import { anchorOf, focusLost, restoreFocus, type FocusAnchor } from "../ui/restoreFocus";
import { ActionsSheet, type SheetAction } from "./ActionsSheet";
import { canShowOnPc, showOnPc, uploadingNames, useHandoffNotice } from "./handoff";
import { DirList, ROW_WINDOW, viewKind } from "./DirList";
import { canEject, DriveList, isImage, isSystemVolume, placeLabel } from "./DriveList";
import { InfoDialog } from "./InfoDialog";
import { BulkRenameDialog } from "./BulkRenameDialog";
import { runRenames, type RenameItem, type RenameResult, type RenameRow } from "./bulkRename";
import { isFfpfsc, opCause, opResult, startOp, watchOp, type OpEnd } from "./opJob";
import { UnpackDialog, unpackCauseText, unpackErrorText } from "./UnpackDialog";
import { VerifyDialog } from "./VerifyDialog";
import {
  copyItems,
  deleteItems,
  isInside,
  fixPermItems,
  joinPath,
  moveItems,
  baseName,
  parentPath,
  renameEntry,
  upFrom,
  volumeOf,
  IntoItselfError,
  type BatchOptions,
  type BatchResult,
} from "./move";
import { currentBatch, startBatch, stopBatch, useBatch, waitJob, type BatchProgress } from "./batch";
import {
  askConflict,
  clearClipboard,
  dropFromClipboard,
  getClipboard,
  setClipboard,
  skipPendingConflict,
  useClipboard,
  useConflict,
  type Clip,
  type PendingConflict,
} from "./clipboard";
import { ConflictDialog } from "./ConflictDialog";
import { pasteItems, type PasteMode, type PasteResult } from "./paste";
import { PathBar } from "./PathBar";
import { DEFAULT_VIEW, invertShown, loadView, saveView, selectAllShown, statusOf, visibleEntries, LOAD_ALL_CAP, type Status, type ViewPrefs } from "./listing";
import { checkFolder, typedPath } from "./pathParts";
import { PathPicker } from "./PathPicker";
import { cleanPath, isPinned, usePrefs, type FilesPrefs } from "./prefs";
import { TextPrompt } from "./TextPrompt";
import { ViewMenu } from "./ViewMenu";
import { useDir, type DirState, type Entry } from "./useDir";
import { Viewer, type ViewKind } from "./Viewer";
import { GameBanner } from "./viewer/GameBanner";
import { sceSysName } from "./viewer/gameFolder";
import { SearchDialog } from "./search/SearchDialog";
import { UsageDialog } from "./usage/UsageDialog";
import { keepsPermissions } from "./permissions";
import { createFile, downloadItem, loadOnOtherDevice, TEXT_SAVE_MAX } from "./transfer";
import { UploadDialog } from "./UploadDialog";

export { upFrom, viewKind };

/** An action's result line clears itself after this long. */
const MESSAGE_MS = 8_000;

/** The modal Files has open, if any. Names are entries of the folder shown. */
export type FilesAsk =
  | { kind: "sheet"; names: string[] }
  | { kind: "delete"; names: string[] }
  | { kind: "fixperm"; names: string[] }
  | { kind: "rename"; name: string }
  /** Rename by a rule (one item or the selection). */
  | { kind: "bulkrename"; names: string[] }
  | { kind: "verify"; name: string }
  /** Decompress a .ffpfsc next to it. */
  | { kind: "unpack"; name: string }
  | { kind: "mkdir" }
  /** A new, empty file (then opened in the editor). */
  | { kind: "newfile" }
  /** Upload here, from the phone or PC the page is open on. */
  | { kind: "upload" }
  | { kind: "pick"; op: "copy" | "move"; names: string[] }
  | { kind: "info"; name: string }
  /** `edit`: straight into the text editor. */
  | { kind: "view"; name: string; type: ViewKind; edit?: boolean }
  /** Search by name (the helper's index) and Disk usage: Files tools over the folder shown. */
  | { kind: "search" }
  | { kind: "usage" }
  /** A pinned or recent folder's menu (the drive list). */
  | { kind: "place"; path: string }
  /** Eject a USB drive; `force` after the helper said it is still in use. */
  | { kind: "eject"; path: string; force: boolean }
  | { kind: "unmount"; path: string }
  | { kind: "goto" }
  /** Sort and view menu. */
  | { kind: "viewmenu" }
  /** The name filter's text box. */
  | { kind: "filter" };

/** The copy/move/delete batch running now (one item at a time, see batch.ts). */
export type Busy = BatchProgress;

/** The line a finished batch leaves: what failed, what a stop left out, else "Done." */
export function batchMessage(r: BatchResult, op?: Busy["op"]): Message | null {
  if (r.failed.length) {
    const why = filesErrorText(r.failed[0].error);
    return why ? { tone: "error", text: t("cw_files_some_failed", { n: r.failed.length, reason: why }) } : null;
  }
  if (r.jobFailed.length) return { tone: "error", text: t("cw_files_batch_failed", { n: r.jobFailed.length }) };
  if (r.skipped.length) return { tone: "info", text: t("cw_files_batch_stopped", { n: r.skipped.length }) };
  return { tone: "info", text: t(op === "chmod" ? "cw_files_fixperm_done" : "cw_files_done") };
}

/**
 * The items of `dir` a finished batch could not do (refused, or their job failed): they stay
 * selected afterwards, so a retry (or a look at them) is one press away. Items elsewhere (a
 * paste's sources) are left out.
 */
export function failedNamesIn(r: BatchResult, dir: string): string[] {
  const out: string[] = [];
  for (const p of r.failed.map((f) => f.item).concat(r.jobFailed)) {
    if (parentPath(p) !== dir) continue;
    const name = baseName(p);
    if (name && !out.includes(name)) out.push(name);
  }
  return out;
}

/** The line a finished paste leaves: batchMessage's, or what was skipped / already there. */
export function pasteMessage(r: PasteResult): Message | null {
  if (r.halted.length && !r.failed.length && !r.jobFailed.length) {
    const rest = r.skipped.length - r.halted.length;
    const text = t("cw_files_paste_halted", { name: baseName(r.halted[0]) });
    return { tone: "info", text: rest > 0 ? `${text} ${t("cw_files_batch_stopped", { n: rest })}` : text };
  }
  if (r.failed.length || r.jobFailed.length || r.skipped.length) return batchMessage(r);
  if (!r.done.length && !r.declined.length && r.here.length) return { tone: "info", text: t("cw_files_paste_here_already") };
  if (r.declined.length) return { tone: "info", text: t("cw_files_paste_done_skipped", { n: r.declined.length }) };
  return batchMessage(r);
}

/** A failed call as a short line; null for a 401 (the reload banner says it). */
export function filesErrorText(err: unknown): string | null {
  if (err instanceof ApiError) {
    if (err.status === 403) return t("cw_files_err_denied");
    if (err.status === 404) return t("cw_files_err_missing");
    // 409 is "exists" or "busy" (and cross_mount, handled by moveItems): only an
    // "exist…" code says the name is taken.
    if (err.status === 409 && /exist/i.test(err.code)) return t("cw_files_err_exists");
    if (err.code === "replace_delete_failed") return t("cw_files_replace_failed");
  }
  if (err instanceof IntoItselfError) return t("cw_files_err_into_itself");
  return errorText(err);
}

/**
 * Why the USB drive at `vol` must not be ejected now, or null. The page's own
 * batch says what it touches; the helper's job list does not say where a job
 * writes, so any other running job (a PC upload, a job a stopped batch left
 * running) holds every eject until it ends.
 */
export function ejectBlocker(vol: string, batch: Busy | null, jobs: Job[]): string | null {
  if (batch?.paths?.some((p) => isInside(p, vol))) return t("cw_files_eject_blocked_batch");
  if (jobs.some((j) => j.state === "running" && (!j.mine || !batch))) return t("cw_files_eject_blocked_jobs");
  return null;
}

/** Why an eject, unmount or pin did not work, by the helper's error code. */
export function volumeErrorText(err: unknown): string | null {
  if (err instanceof ApiError) {
    if (err.code === "usb_eject_busy") return t("cw_files_eject_busy");
    if (err.code === "usb_eject_not_mounted") return t("cw_files_eject_gone");
    if (err.code === "fs_unmount_busy") return t("cw_files_unmount_busy");
    if (err.code === "fs_unmount_not_our_mount") return t("cw_files_unmount_not_ours");
    if (err.code === "system_volume") return t("cw_files_unmount_system");
    if (err.code === "prefs_full") return t("cw_files_pin_full");
    if (err.code === "game_running") return t("cw_files_unmount_game");
  }
  return filesErrorText(err);
}

export interface FilesViewProps {
  /** The folder shown; null = the drive list. */
  path: string | null;
  volumes: Volume[] | null;
  volumesError: boolean;
  /** Read the drives again (the drive list's Try again). */
  onRetryVolumes?: () => void;
  dir: DirState;
  /** The rows to show (hidden files, filter and sort applied); default: dir.entries. */
  shown?: Entry[];
  view?: ViewPrefs;
  status?: Status;
  /** Rows drawn so far (the rest wait behind "Show more rows"). */
  rowLimit?: number;
  onMoreRows?: () => void;
  onView?: (patch: Partial<ViewPrefs>) => void;
  onLoadAll?: () => void;
  onSelectAll?: () => void;
  onSelectNone?: () => void;
  onSelectInvert?: () => void;
  selecting: boolean;
  selected: string[];
  ask: FilesAsk | null;
  busy: Busy | null;
  message: Message | null;
  /** Running jobs this page started (copies, moves, deletes). */
  tasks: Job[];
  /** Pinned / recent folders (null: not read, or a helper without them). */
  prefs?: FilesPrefs | null;
  /** The open eject/unmount dialog's request is running. */
  askBusy?: boolean;
  /** Why the open eject/unmount dialog's last try failed. */
  askError?: string | null;
  onPin?: (path: string) => void;
  onUnpin?: (path: string) => void;
  /** "Show on PC": the PC app's File System opens it. */
  onShowPc?: (path: string) => void;
  /** Entries of the folder shown that a PC upload is writing into. */
  uploading?: ReadonlySet<string>;
  onEject?: (path: string, force: boolean) => void;
  onUnmount?: (path: string) => void;
  onGoTo?: (path: string) => void;
  /** A game is running (Home's status): Unmount is held (it would take the game's image away). */
  gameRunning?: boolean;
  /** Stop the running batch before its next item. */
  onStopBatch?: () => void;
  /** What Copy / Cut put in the clipboard (kept across folders and sections). */
  clip?: Clip | null;
  /** A paste conflict waiting for an answer. */
  conflict?: PendingConflict | null;
  /** Copy / Cut these entries of the folder shown into the clipboard. */
  onClip?: (mode: PasteMode, names: string[]) => void;
  /** Paste the clipboard into the folder shown. */
  onPaste?: () => void;
  onClipClear?: () => void;
  onOpen: (path: string | null) => void;
  onUp: () => void;
  onEntry: (e: Entry) => void;
  onLoadMore: () => void;
  onRetry?: () => void;
  onToggleSelecting: () => void;
  onToggleItem: (name: string) => void;
  onAsk: (ask: FilesAsk) => void;
  onDismiss: () => void;
  onConfirmDelete: (names: string[]) => void;
  onConfirmFixPerm: (names: string[]) => void;
  onRename: (name: string, newName: string) => void;
  /** Run a bulk rename plan's rows (one at a time, stopping at the first failure). */
  onBulkRename?: (rows: RenameRow[]) => void;
  /** Decompress `name` (a .ffpfsc in the folder shown); `outName` = Keep both's name, else the image's own. */
  onUnpack?: (name: string, outName: string | null) => void;
  onMkdir: (name: string) => void;
  onPicked: (op: "copy" | "move", names: string[], destDir: string) => void;
  /** Open `dir` in Files with `name` focused (a search result); null name: the folder itself. */
  onReveal?: (dir: string, name: string | null) => void;
  /** Create an empty file named so in the folder shown (then it opens in the editor). */
  onNewFile?: (name: string) => void;
  /** Download an item to this device (a phone or PC: `remote`). */
  onDownload?: (path: string) => void;
  /** The page is open on another device: Upload here and Download are offered. */
  remote?: boolean;
}

/** "Copying 3 of 40" (item `at` is being sent or is running), or "Stopping…". */
export function busyText(b: Busy | null): string | null {
  if (!b) return null;
  if (b.stopping) return t("cw_files_batch_stopping");
  const at = Math.max(1, b.at);
  if (b.op === "copy") return t("cw_files_batch_copy", { at, total: b.total });
  if (b.op === "move") return t("cw_files_batch_move", { at, total: b.total });
  if (b.op === "chmod") return t("cw_files_fixperm_batch", { at, total: b.total });
  if (b.op === "rename") return t("cw_files_br_batch", { at, total: b.total });
  if (b.op === "unpack") {
    const line = t("cw_files_unpack_batch", { name: baseName(b.paths?.[0] ?? "") });
    const total = b.bytesTotal ?? 0;
    if (total <= 0) return line;
    const done = Math.min(b.bytesDone ?? 0, total);
    const pct = Math.min(100, Math.floor((done / total) * 100));
    return `${line} · ${t("cw_home_percent", { n: pct })} · ${formatBytes(done)} / ${formatBytes(total)}`;
  }
  return t("cw_files_batch_delete", { at, total: b.total });
}

/**
 * What the sheet offers for `names` (one entry, or the selection), by type: a
 * folder can be pinned; a file can be verified; a .ffpfsc can be decompressed.
 */
export function sheetActions(names: string[], entries: Entry[], pinned: boolean | null, remote = false): SheetAction[] {
  if (names.length !== 1) return ["clipcopy", "cut", "copy", "move", "bulkrename", "fixperm", "delete"];
  const e = entries.find((x) => x.name === names[0]);
  const out: SheetAction[] = [];
  if (e && e.kind !== "dir" && viewKind(e.name)) out.push("view");
  // Any small file opens in the text editor (a .cfg, .conf, .sh…); a binary one says it cannot.
  if (e && e.kind === "file" && (e.size ?? 0) <= TEXT_SAVE_MAX) out.push("edit");
  if (remote && e && (e.kind === "file" || e.kind === "dir")) out.push("download");
  out.push("clipcopy", "cut", "copy", "move", "rename", "bulkrename", "mkdir", "newfile", "info");
  if (e && e.kind === "dir" && pinned != null) out.push(pinned ? "unpin" : "pin");
  if (e && e.kind === "file") out.push("verify", "hex");
  if (e && e.kind === "file" && isFfpfsc(e.name)) out.push("unpack");
  if (canShowOnPc()) out.push("showpc");
  out.push("fixperm", "delete");
  return out;
}

/** `names` as bulk rename items, in the order the list shows them (numbering follows it). */
export function renameItems(names: string[], shown: Entry[], entries: Entry[]): RenameItem[] {
  const want = new Set(names);
  const kinds = new Map(entries.map((e) => [e.name, e.kind]));
  const ordered = shown.filter((e) => want.has(e.name)).map((e) => e.name);
  const seen = new Set(ordered);
  for (const n of names) if (!seen.has(n)) ordered.push(n);
  return ordered.map((name) => ({ name, dir: kinds.get(name) === "dir" }));
}

/*
 * askView is a plain call (no hooks), so the bulk rename dialog's inputs are
 * kept by their last arguments: the same names/shown/entries arrays give the
 * same items and names arrays, and the dialog's plan useMemo is not redone on
 * every Files render (a job poll, the batch line).
 */
let lastItems: { names: string[]; shown: Entry[]; entries: Entry[]; out: RenameItem[] } | null = null;
let lastNames: { entries: Entry[]; out: string[] } | null = null;

/** renameItems, the same array again for the same arguments. */
export function renameItemsKept(names: string[], shown: Entry[], entries: Entry[]): RenameItem[] {
  const c = lastItems;
  if (c && c.names === names && c.shown === shown && c.entries === entries) return c.out;
  const out = renameItems(names, shown, entries);
  lastItems = { names, shown, entries, out };
  return out;
}

/** The folder's names, the same array again for the same entries. */
export function entryNamesKept(entries: Entry[]): string[] {
  if (lastNames && lastNames.entries === entries) return lastNames.out;
  const out = entries.map((e) => e.name);
  lastNames = { entries, out };
  return out;
}

/** The line a finished bulk rename leaves. */
export function renameMessage(r: RenameResult): Message | null {
  if (r.failedRow) {
    const why = filesErrorText(r.failed[0]?.error);
    if (why == null) return null;
    return {
      tone: "error",
      text: t("cw_files_br_failed", { from: r.failedRow.from, to: r.failedRow.to, reason: why, done: r.done.length, rest: r.skipped.length }),
    };
  }
  if (r.skipped.length) return { tone: "info", text: t("cw_files_br_stopped", { done: r.done.length, rest: r.skipped.length }) };
  return { tone: "info", text: t("cw_files_br_done", { n: r.done.length }) };
}

/** The line a finished decompress leaves. */
export function unpackMessage(end: OpEnd | { end: "error"; error: unknown }, source: string): Message | null {
  if (end.end === "done") {
    const r = opResult<{ path?: unknown }>(end.status);
    const out = r && typeof r.path === "string" ? baseName(r.path) : null;
    return { tone: "info", text: out ? t("cw_files_unpack_done", { name: out, source }) : t("cw_files_unpack_done_plain") };
  }
  if (end.end === "cancelled") return { tone: "info", text: t("cw_files_unpack_stopped") };
  if (end.end === "gone") return { tone: "info", text: t("cw_files_op_gone") };
  if (end.end === "error") {
    const why = unpackErrorText(end.error);
    return why ? { tone: "error", text: t("cw_files_unpack_failed", { reason: why }) } : null;
  }
  const cause = opCause(end.status);
  const why = unpackCauseText(cause) ?? t("cw_files_unpack_failed_generic", { cause: cause || "?" });
  return { tone: "error", text: t("cw_files_unpack_failed", { reason: why }) };
}

/** Go to path's pinned folders: one button each, ✕ opens it. */
export function PinnedChoices({ paths, onOpen }: { paths: string[]; onOpen: (path: string) => void }) {
  return (
    <div className="cw-goto-pinned">
      <p className="cw-dim">{t("cw_files_goto_pinned")}</p>
      <ul className="cw-files" data-nav-list aria-label={t("cw_files_goto_pinned")}>
        {paths.map((x) => (
          <li key={x} className="cw-file is-plain" data-nav-item>
            <button type="button" className="cw-focusable cw-file-main" data-goto-pin={x} onClick={() => onOpen(x)}>
              <Star className="cw-file-icon is-pinned" aria-hidden />
              <span className="cw-file-text">
                <span className="cw-dim cw-mono cw-file-sub">{x}</span>
              </span>
            </button>
          </li>
        ))}
      </ul>
    </div>
  );
}

/** The modals of the drive list level (and Go to path, at any level). */
function placeAskView(p: FilesViewProps) {
  const a = p.ask;
  if (!a) return null;
  switch (a.kind) {
    case "place": {
      const pinned = isPinned(p.prefs ?? null, a.path);
      return (
        <ActionsSheet
          title={placeLabel(a.path, p.volumes ?? [])}
          actions={["open", pinned ? "unpin" : "pin"]}
          busy={false}
          note={a.path}
          onClose={p.onDismiss}
          onChoose={(act) => {
            if (act === "open") p.onOpen(a.path);
            else if (act === "pin") p.onPin?.(a.path);
            else if (act === "unpin") p.onUnpin?.(a.path);
          }}
        />
      );
    }
    case "eject":
      return (
        <ConfirmDialog
          title={t(a.force ? "cw_files_eject_force_title" : "cw_files_eject_title", { path: a.path })}
          body={t(a.force ? "cw_files_eject_force_body" : "cw_files_eject_body")}
          confirmLabel={t(a.force ? "cw_files_eject_force" : "cw_files_eject")}
          danger={a.force}
          busy={p.askBusy}
          error={p.askError}
          onConfirm={() => p.onEject?.(a.path, a.force)}
          onCancel={p.onDismiss}
        />
      );
    case "unmount":
      return (
        <ConfirmDialog
          title={t("cw_files_unmount_title", { path: a.path })}
          body={t("cw_files_unmount_body")}
          confirmLabel={t("cw_files_unmount")}
          danger
          busy={p.askBusy || p.gameRunning}
          error={p.gameRunning ? t("cw_files_unmount_game") : p.askError}
          onConfirm={() => p.onUnmount?.(a.path)}
          onCancel={p.onDismiss}
        />
      );
    case "viewmenu":
      return p.onView ? (
        <ViewMenu view={p.view ?? DEFAULT_VIEW} onChange={p.onView} onClose={p.onDismiss} />
      ) : null;
    case "filter":
      return (
        <TextPrompt
          title={t("cw_files_filter_title")}
          fieldLabel={t("cw_files_filter_label")}
          initial={p.view?.filter ?? ""}
          check={() => null}
          onSubmit={(v) => {
            p.onView?.({ filter: v });
            p.onDismiss();
          }}
          onCancel={p.onDismiss}
        />
      );
    case "goto":
      return (
        <TextPrompt
          title={t("cw_files_goto")}
          fieldLabel={t("cw_files_goto_label")}
          initial={p.path ?? "/"}
          check={checkFolder}
          onSubmit={(v) => p.onGoTo?.(typedPath(v) ?? v)}
          onCancel={p.onDismiss}
          extra={p.prefs?.favorites.length ? <PinnedChoices paths={p.prefs.favorites} onOpen={(x) => p.onGoTo?.(x)} /> : null}
        />
      );
    default:
      return null;
  }
}

function namesTitle(names: string[]): string {
  return names.length === 1 ? names[0] : t("cw_files_items", { n: names.length });
}

/** The modal for `p.ask` (a plain call, not a component: tests read its props). */
function askView(p: FilesViewProps) {
  const a = p.ask;
  if (!a) return null;
  const level = placeAskView(p);
  if (level || p.path == null) return level;
  const path = p.path;
  const pinnedOf = (name: string) => (p.prefs ? isPinned(p.prefs, joinPath(path, name)) : null);
  switch (a.kind) {
    case "sheet":
      return (
        <ActionsSheet
          title={namesTitle(a.names)}
          actions={sheetActions(a.names, p.dir.entries, pinnedOf(a.names[0]), !!p.remote)}
          busy={p.busy != null}
          note={busyText(p.busy)}
          onClose={p.onDismiss}
          onChoose={(act) => {
            const name = a.names[0];
            switch (act) {
              case "view": {
                const type = viewKind(name);
                if (type) p.onAsk({ kind: "view", name, type });
                return;
              }
              case "clipcopy":
                return p.onClip?.("copy", a.names);
              case "cut":
                return p.onClip?.("cut", a.names);
              case "copy":
              case "move":
                return p.onAsk({ kind: "pick", op: act, names: a.names });
              case "rename":
                return p.onAsk({ kind: "rename", name });
              case "bulkrename":
                return p.onAsk({ kind: "bulkrename", names: a.names });
              case "verify":
                return p.onAsk({ kind: "verify", name });
              case "hex":
                return p.onAsk({ kind: "view", name, type: "hex" });
              case "unpack":
                return p.onAsk({ kind: "unpack", name });
              case "mkdir":
                return p.onAsk({ kind: "mkdir" });
              case "newfile":
                return p.onAsk({ kind: "newfile" });
              case "edit":
                return p.onAsk({ kind: "view", name, type: "text", edit: true });
              case "download":
                return p.onDownload?.(joinPath(path, name));
              case "info":
                return p.onAsk({ kind: "info", name });
              case "pin":
                return p.onPin?.(joinPath(path, name));
              case "unpin":
                return p.onUnpin?.(joinPath(path, name));
              case "showpc":
                return p.onShowPc?.(joinPath(path, name));
              case "open":
                return;
              case "fixperm":
                return p.onAsk({ kind: "fixperm", names: a.names });
              case "delete":
                return p.onAsk({ kind: "delete", names: a.names });
            }
          }}
        />
      );
    case "delete": {
      const one = a.names.length === 1;
      const body = one
        ? t("cw_files_del_body")
        : `${t("cw_files_del_list_many", { name: a.names[0], more: a.names.length - 1 })} ${t("cw_files_del_body")}`;
      return (
        <ConfirmDialog
          title={one ? t("cw_files_del_one", { name: a.names[0] }) : t("cw_files_del_many", { n: a.names.length })}
          body={busyText(p.busy) ?? body}
          confirmLabel={t("cw_files_delete")}
          danger
          busy={p.busy != null}
          onConfirm={() => p.onConfirmDelete(a.names)}
          onCancel={p.onDismiss}
        />
      );
    }
    case "fixperm":
      return (
        <ConfirmDialog
          title={
            a.names.length === 1
              ? `${t("cw_files_fixperm")}: ${a.names[0]}`
              : t("cw_files_fixperm_many", { n: a.names.length })
          }
          body={busyText(p.busy) ?? t("cw_files_fixperm_body")}
          confirmLabel={t("cw_files_fixperm")}
          busy={p.busy != null}
          onConfirm={() => p.onConfirmFixPerm(a.names)}
          onCancel={p.onDismiss}
        />
      );
    case "rename":
      return (
        <TextPrompt
          title={t("cw_files_rename")}
          initial={a.name}
          onSubmit={(v) => p.onRename(a.name, v)}
          onCancel={p.onDismiss}
        />
      );
    case "mkdir":
      return (
        <TextPrompt title={t("cw_files_new_folder")} initial="" onSubmit={p.onMkdir} onCancel={p.onDismiss} />
      );
    case "newfile":
      return <TextPrompt title={t("cw_files_new_file")} initial="" onSubmit={(v) => p.onNewFile?.(v)} onCancel={p.onDismiss} />;
    case "upload": {
      const v = p.volumes ? volumeOf(path, p.volumes) : null;
      return (
        <UploadDialog
          dir={path}
          free={v ? v.free_bytes : null}
          fsNote={v && !keepsPermissions(v.fs_type) ? t("cw_files_perm_not_kept", { fs: v.fs_type }) : null}
          onDone={() => p.onRetry?.()}
          onClose={p.onDismiss}
        />
      );
    }
    case "bulkrename":
      return (
        <BulkRenameDialog
          items={renameItemsKept(a.names, p.shown ?? p.dir.entries, p.dir.entries)}
          existing={entryNamesKept(p.dir.entries)}
          onRename={(plan) => p.onBulkRename?.(plan.rows)}
          onCancel={p.onDismiss}
        />
      );
    case "verify":
      return <VerifyDialog path={joinPath(path, a.name)} name={a.name} onClose={p.onDismiss} />;
    case "unpack":
      return (
        <UnpackDialog
          dir={path}
          name={a.name}
          path={joinPath(path, a.name)}
          busy={p.busy != null}
          onStart={(outName) => p.onUnpack?.(a.name, outName)}
          onCancel={p.onDismiss}
        />
      );
    case "pick":
      return (
        <PathPicker
          title={t(a.op === "copy" ? "cw_files_copy_to" : "cw_files_move_to")}
          subtitle={namesTitle(a.names)}
          volumes={p.volumes ?? []}
          start={path}
          onPick={(d) => p.onPicked(a.op, a.names, d)}
          onCancel={p.onDismiss}
        />
      );
    case "info":
      return (
        <InfoDialog
          dir={path}
          name={a.name}
          path={joinPath(path, a.name)}
          fsType={p.volumes ? (volumeOf(path, p.volumes)?.fs_type ?? null) : null}
          onClose={p.onDismiss}
        />
      );
    case "view":
      return (
        <Viewer
          path={joinPath(path, a.name)}
          name={a.name}
          kind={a.type}
          size={p.dir.entries.find((e) => e.name === a.name)?.size}
          images={a.type === "image" ? (p.shown ?? p.dir.entries).filter((e) => e.kind !== "dir" && viewKind(e.name) === "image").map((e) => e.name) : undefined}
          onOpenImage={(name) => p.onAsk({ kind: "view", name, type: "image" })}
          startEditing={a.edit}
          onSaved={() => p.onRetry?.()}
          onClose={p.onDismiss}
        />
      );
    case "search":
      return (
        <SearchDialog
          folder={path}
          volumes={p.volumes ?? []}
          onReveal={(dir, name) => p.onReveal?.(dir, name)}
          onClose={p.onDismiss}
        />
      );
    case "usage":
      return <UsageDialog folder={path} volumes={p.volumes ?? []} onOpen={(dir) => p.onReveal?.(dir, null)} onClose={p.onDismiss} />;
    default:
      return null;
  }
}

function TasksLine({ tasks }: { tasks: Job[] }) {
  if (!tasks.length) return null;
  const pcts = tasks.map(jobPercent).filter((x): x is number => x != null);
  const pct = pcts.length ? Math.floor(pcts.reduce((s, x) => s + x, 0) / pcts.length) : null;
  return (
    <div className="cw-msg" role="status">
      {t("cw_files_tasks", { n: tasks.length })}
      {pct != null ? ` · ${t("cw_home_percent", { n: pct })}` : ""}
      <div className="cw-bar" aria-hidden>
        <div className={`cw-bar-fill${pct == null ? " is-unknown" : ""}`} style={{ width: `${pct ?? 100}%` }} />
      </div>
    </div>
  );
}

/**
 * One toolbar button: an icon, named by aria-label; the hint bar shows the
 * focused one's name (data-tip).
 */
function ToolButton({
  icon: Icon,
  label,
  action,
  onClick,
  pressed,
  disabled,
  className,
}: {
  icon: LucideIcon;
  label: string;
  action: string;
  onClick: () => void;
  pressed?: boolean;
  disabled?: boolean;
  className?: string;
}) {
  return (
    <button
      type="button"
      className={`cw-focusable cw-tool${className ? ` ${className}` : ""}`}
      data-action={action}
      data-tip={label}
      aria-label={label}
      aria-pressed={pressed}
      disabled={disabled}
      onClick={onClick}
    >
      <Icon aria-hidden />
    </button>
  );
}

/** "N items · M selected (size)"; folders count as selected but add no size. */
export function statusText(st: Status): string {
  const items =
    st.items === st.total ? t("cw_files_status_items", { n: st.items }) : t("cw_files_status_filtered", { n: st.items, total: st.total });
  if (!st.selected) return items;
  const size = st.folders ? t("cw_files_status_size_dirs", { size: formatBytes(st.bytes) }) : formatBytes(st.bytes);
  return `${items} · ${t("cw_files_status_selected", { n: st.selected, size })}`;
}

/** "3 items to paste (cut)" with Paste here and Clear: in the status line, not a toolbar row of its own. */
function ClipBar({ clip, busy, onPaste, onClear }: { clip: Clip; busy: boolean; onPaste?: () => void; onClear?: () => void }) {
  const what = clip.items.length === 1 ? baseName(clip.items[0].path) : t("cw_files_items", { n: clip.items.length });
  return (
    <div className="cw-clip" data-clip={clip.mode}>
      <span className="cw-clip-text">{t(clip.mode === "cut" ? "cw_files_clip_bar_cut" : "cw_files_clip_bar_copy", { what })}</span>
      {onPaste ? (
        <button type="button" className="cw-focusable cw-button cw-clip-button" data-action="paste" disabled={busy} onClick={onPaste}>
          <ClipboardPaste aria-hidden />
          <span>{t("cw_files_clip_paste")}</span>
        </button>
      ) : null}
      {onClear ? (
        <button type="button" className="cw-focusable cw-button cw-clip-button" data-action="clip-clear" onClick={onClear}>
          <ClipboardX aria-hidden />
          <span>{t("cw_files_clip_clear")}</span>
        </button>
      ) : null}
    </div>
  );
}

/** Files without state: everything comes in through props (tests call it directly). */
export function FilesView(p: FilesViewProps) {
  const vol = p.path != null && p.volumes ? volumeOf(p.path, p.volumes) : null;
  const status = busyText(p.busy);
  const shown = p.shown ?? p.dir.entries;
  const view = p.view ?? DEFAULT_VIEW;
  const capped = p.dir.more && p.dir.entries.length >= LOAD_ALL_CAP;
  const st = p.status;
  const pinned = p.path != null && p.prefs ? isPinned(p.prefs, p.path) : false;
  const sceSys = p.path != null ? sceSysName(p.dir.entries) : null;
  const loadAllButton = (
    <ToolButton
      icon={ChevronsDown}
      label={t("cw_files_load_all")}
      action="load-all"
      disabled={p.dir.loading || capped}
      onClick={() => p.onLoadAll?.()}
    />
  );
  return (
    <div className="cw-files-screen">
      <h1 className="cw-h1">{t("cw_nav_files")}</h1>
      {p.message ? (
        <div className={`cw-msg is-${p.message.tone}`} role={p.message.tone === "error" ? "alert" : "status"}>
          {p.message.text}
        </div>
      ) : null}
      {status ? (
        <div className="cw-msg" role="status">
          {status}
          {p.onStopBatch && p.busy && !p.busy.stopping && (p.busy.total > 1 || p.busy.op === "unpack") ? (
            <button
              type="button"
              className="cw-focusable cw-button cw-msg-action"
              data-action="stop-batch"
              onClick={p.onStopBatch}
            >
              <Square aria-hidden />
              <span>{t("cw_files_batch_stop")}</span>
            </button>
          ) : null}
        </div>
      ) : null}
      <TasksLine tasks={p.tasks} />

      {p.path == null ? (
        <DriveList
          volumes={p.volumes}
          error={p.volumesError}
          prefs={p.prefs}
          onOpen={p.onOpen}
          onRetry={p.onRetryVolumes}
          onEject={p.onEject ? (path) => p.onAsk({ kind: "eject", path, force: false }) : undefined}
          onUnmount={p.onUnmount ? (path) => p.onAsk({ kind: "unmount", path }) : undefined}
          unmountHeld={p.gameRunning ? t("cw_files_unmount_game") : null}
          onPlaceActions={(path) => p.onAsk({ kind: "place", path })}
          onGoTo={p.onGoTo ? () => p.onAsk({ kind: "goto" }) : undefined}
        />
      ) : (
        <>
          <div className="cw-toolbar-one">
            <PathBar key={p.path} path={p.path} volumes={p.volumes ?? []} onOpen={p.onOpen} onUp={p.onUp} />
            <div className="cw-tools" role="toolbar" aria-label={t("cw_files_tools")}>
              {p.selecting ? (
                <>
                  <ToolButton icon={CheckCheck} label={t("cw_files_select_all")} action="select-all" onClick={() => p.onSelectAll?.()} />
                  <ToolButton icon={ListX} label={t("cw_files_select_none")} action="select-none" onClick={() => p.onSelectNone?.()} />
                  <ToolButton icon={ListChecks} label={t("cw_files_select_invert")} action="select-invert" onClick={() => p.onSelectInvert?.()} />
                  {p.selected.length ? (
                    <ToolButton
                      icon={Ellipsis}
                      label={t("cw_actions")}
                      action="selection-actions"
                      onClick={() => p.onAsk({ kind: "sheet", names: p.selected })}
                    />
                  ) : null}
                  {view.filter ? (
                    <ToolButton
                      icon={FilterX}
                      label={t("cw_files_filter_clear")}
                      action="filter-clear"
                      onClick={() => p.onView?.({ filter: "" })}
                    />
                  ) : null}
                  {p.dir.more && p.onLoadAll ? loadAllButton : null}
                  <ToolButton
                    icon={SquareCheck}
                    label={t("cw_select")}
                    action="select"
                    pressed
                    onClick={p.onToggleSelecting}
                  />
                </>
              ) : (
                <>
                  {p.prefs && p.onPin && p.onUnpin ? (
                    <ToolButton
                      icon={Star}
                      label={t(pinned ? "cw_files_pinned_on" : "cw_files_pin")}
                      action="pin"
                      pressed={pinned}
                      className={pinned ? "is-pinned" : undefined}
                      onClick={() => (pinned ? p.onUnpin?.(p.path as string) : p.onPin?.(p.path as string))}
                    />
                  ) : null}
                  {p.onGoTo ? (
                    <ToolButton
                      icon={TextCursorInput}
                      label={t("cw_files_goto")}
                      action="goto"
                      onClick={() => p.onAsk({ kind: "goto" })}
                    />
                  ) : null}
                  <ToolButton
                    icon={FolderPlus}
                    label={t("cw_files_new_folder")}
                    action="mkdir"
                    onClick={() => p.onAsk({ kind: "mkdir" })}
                  />
                  <ToolButton icon={FilePlus} label={t("cw_files_new_file")} action="newfile" onClick={() => p.onAsk({ kind: "newfile" })} />
                  {p.remote ? (
                    <ToolButton icon={Upload} label={t("cw_files_up_title")} action="upload" onClick={() => p.onAsk({ kind: "upload" })} />
                  ) : null}
                  <ToolButton icon={SquareCheck} label={t("cw_select")} action="select" onClick={p.onToggleSelecting} />
                  {p.onReveal ? (
                    <>
                      <ToolButton icon={Search} label={t("cw_files_search")} action="search" onClick={() => p.onAsk({ kind: "search" })} />
                      <ToolButton icon={ChartBar} label={t("cw_files_usage")} action="usage" onClick={() => p.onAsk({ kind: "usage" })} />
                    </>
                  ) : null}
                  {p.onView ? (
                    <>
                      <ToolButton
                        icon={ArrowDownUp}
                        label={t("cw_files_sort_view")}
                        action="sortview"
                        onClick={() => p.onAsk({ kind: "viewmenu" })}
                      />
                      <ToolButton
                        icon={Filter}
                        label={view.filter ? t("cw_files_filter_active", { q: view.filter }) : t("cw_files_filter")}
                        action="filter"
                        pressed={view.filter !== ""}
                        onClick={() => p.onAsk({ kind: "filter" })}
                      />
                      {view.filter ? (
                        <ToolButton
                          icon={FilterX}
                          label={t("cw_files_filter_clear")}
                          action="filter-clear"
                          onClick={() => p.onView?.({ filter: "" })}
                        />
                      ) : null}
                      {p.dir.more && p.onLoadAll ? loadAllButton : null}
                    </>
                  ) : null}
                </>
              )}
            </div>
          </div>
          {(st && p.dir.loaded) || p.clip ? (
            <div className="cw-status-row">
              {st && p.dir.loaded ? (
                <p className="cw-dim cw-status" role="status" data-status="">
                  {[
                    statusText(st),
                    p.selecting && view.filter ? t("cw_files_filter_on") : null,
                    capped ? t("cw_files_capped", { n: p.dir.entries.length }) : null,
                    vol ? t("cw_files_free", { free: formatBytes(vol.free_bytes) }) : null,
                  ]
                    .filter(Boolean)
                    .join(" · ")}
                </p>
              ) : null}
              {p.clip ? <ClipBar clip={p.clip} busy={p.busy != null} onPaste={p.onPaste} onClear={p.onClipClear} /> : null}
            </div>
          ) : null}

          {p.dir.error && !p.dir.loading ? (
            <div className="cw-msg is-error" role="alert">
              {filesErrorText(p.dir.error) ?? t("cw_files_err_load")}
              {p.onRetry ? (
                <button type="button" className="cw-focusable cw-button cw-msg-action" onClick={p.onRetry}>
                  <RotateCw aria-hidden />
                  <span>{t("cw_files_retry")}</span>
                </button>
              ) : null}
            </div>
          ) : null}
          {!p.dir.loaded && !p.dir.error ? <p className="cw-dim">{t("cw_home_loading")}</p> : null}
          {p.dir.loaded && p.dir.entries.length > 0 && !shown.length && !p.dir.more ? (
            <p className="cw-dim">{t("cw_files_none_match")}</p>
          ) : null}
          {p.dir.loaded && !p.dir.entries.length && !p.dir.more ? (
            <p className="cw-dim">
              {t("cw_files_empty")} {t("cw_files_empty_next")}
            </p>
          ) : null}
          {p.dir.loaded && sceSys ? <GameBanner key={p.path} folder={p.path} sceSys={sceSys} /> : null}
          <DirList
            entries={shown}
            limit={p.rowLimit}
            onMoreRows={p.onMoreRows}
            selecting={p.selecting}
            selected={p.selected}
            onEntry={p.onEntry}
            onToggleItem={p.onToggleItem}
            onActions={(name) => p.onAsk({ kind: "sheet", names: [name] })}
            label={p.path}
            uploading={p.uploading}
          />
          {p.dir.more ? (
            // Reaching it with the D-pad reads the next page: big folders load as you scroll.
            <button
              type="button"
              className="cw-focusable cw-button cw-more"
              data-action="more"
              onFocus={p.onLoadMore}
              onClick={p.onLoadMore}
            >
              <span>{p.dir.loading ? t("cw_home_loading") : t("cw_files_more")}</span>
            </button>
          ) : null}
        </>
      )}
      {p.conflict ? (
        <ConflictDialog key={p.conflict.conflict.item} conflict={p.conflict.conflict} onAnswer={p.conflict.answer} />
      ) : (
        askView(p)
      )}
    </div>
  );
}

/** Stop: the batch ends before its next item, and a conflict it waits on is answered Skip. */
export function stopBatchAndConflict(): void {
  stopBatch();
  skipPendingConflict();
}

/* ---- the section ---- */

function useVolumes(path: string | null, tick: number): { volumes: Volume[] | null; error: boolean } {
  const [volumes, setVolumes] = useState<Volume[] | null>(null);
  const [error, setError] = useState(false);
  const atDrives = path == null;
  // Read on open and each time the drive list shows again (free space moves).
  useEffect(() => {
    let live = true;
    // A retry shows "Loading…" until the answer, not the old error.
    setError(false);
    call<{ volumes?: unknown }>("/api/volumes").then(
      (r) => {
        if (!live) return;
        setVolumes(Array.isArray(r?.volumes) ? (r.volumes as Volume[]) : []);
        setError(false);
      },
      () => {
        if (live) setError(true);
      },
    );
    return () => {
      live = false;
    };
  }, [atDrives, tick]);
  return { volumes, error };
}

/** The element under `root` whose `attr` is exactly `value` (compared as-is: no CSS escaping of names). */
function byAttr(root: Element, attr: string, value: string): HTMLElement | null {
  const all = root.querySelectorAll<HTMLElement>(`[${attr}]`);
  for (let i = 0; i < all.length; i++) if (all[i].getAttribute(attr) === value) return all[i];
  return null;
}

/** The Files section: drives → folders, with the actions sheet, select, copy/move, viewer. */
export function Files({ ctx }: { ctx: SectionContext }) {
  const wanted = ctx.params?.path ?? null;
  const wantedFocus = ctx.params?.focus ?? "";
  const [path, setPath] = useState<string | null>(wanted);
  const [volumesTick, setVolumesTick] = useState(0);
  const { volumes, error: volumesError } = useVolumes(path, volumesTick);
  const dir = useDir(path);
  const { jobs, uploading } = useJobs();
  const [selecting, setSelecting] = useState(false);
  const [selected, setSelected] = useState<string[]>([]);
  const [view, setView] = useState<ViewPrefs>(loadView);
  const [rowLimit, setRowLimit] = useState(ROW_WINDOW);
  /** Select all was asked while pages remain: select once Load all has finished. */
  const selectAfterLoad = useRef(false);
  const [ask, setAsk] = useState<FilesAsk | null>(null);
  const busy = useBatch();
  const clip = useClipboard();
  const conflict = useConflict();
  const [message, setMessage] = useState<Message | null>(null);
  const [askBusy, setAskBusy] = useState(false);
  const [askError, setAskError] = useState<string | null>(null);
  const { prefs, settled: prefsSettled, pin, unpin, visit, forget } = usePrefs();
  /** Files was moved somewhere (or opened at a place): the last location is not restored any more. */
  const moved = useRef(wanted != null);
  /** The folder restored from the last visit (left for the drive list when it is gone). */
  const restored = useRef<string | null>(null);
  /** The last folder recorded as visited. */
  const visited = useRef<string | null>(null);
  const root = useRef<HTMLDivElement>(null);
  /** False once Files unmounts: a batch that ends later reports to no one (it ran to the end anyway). */
  const mounted = useRef(true);
  /** The folder shown now (a batch that ends later checks it is still the same). */
  const pathRef = useRef(path);
  useEffect(() => {
    pathRef.current = path;
  }, [path]);
  /** Where focus was last in the list (to put it back when that row goes). */
  const lastAnchor = useRef<FocusAnchor | null>(null);
  /** After the next load, focus this row ("" = the first one), then forget it. */
  const focusName = useRef<string | null>(null);
  /** "Show more" had focus when it read the next page: the row count before it. */
  const afterMore = useRef<number | null>(null);
  const { refresh } = dir;
  // Inside a folder, the browser's Back (○) goes up one level.
  useBackLevel(path != null);

  useEffect(() => {
    mounted.current = true;
    return () => {
      mounted.current = false;
      // A paste conflict nobody can answer now: skip it (and the rest), so the batch ends.
      skipPendingConflict();
    };
  }, []);

  useEffect(() => {
    if (!message) return;
    const timer = setTimeout(() => setMessage(null), MESSAGE_MS);
    return () => clearTimeout(timer);
  }, [message]);

  const go = useCallback((next: string | null, focus: string | null = "") => {
    moved.current = true;
    focusName.current = focus;
    lastAnchor.current = null;
    setSelecting(false);
    setSelected([]);
    setView((v) => (v.filter ? { ...v, filter: "" } : v));
    setRowLimit(ROW_WINDOW);
    selectAfterLoad.current = false;
    setPath(next);
  }, []);

  // Home's Browse USB (or any openSection) opens a path: like any folder change,
  // the selection is cleared and focus goes to its first row.
  useEffect(() => {
    if (wanted != null) go(wanted, wantedFocus);
  }, [wanted, wantedFocus, go]);
  // Opened by the PC ("Show on TV"): say so, or that the folder is gone.
  useHandoffNotice(ctx.params, setMessage);
  const uploadingHere = useMemo(() => uploadingNames(path, uploading), [path, uploading]);

  // Opened without a place: back where Files was last (a folder that is gone falls back to the drives).
  useEffect(() => {
    if (!prefsSettled || moved.current) return;
    moved.current = true;
    if (prefs?.last) {
      restored.current = prefs.last;
      go(prefs.last);
    }
  }, [prefsSettled, prefs, go]);
  useEffect(() => {
    if (path == null || path !== restored.current || !(dir.error instanceof ApiError)) return;
    if (dir.error.status !== 404 && dir.error.status !== 403 && dir.error.status !== 400) return;
    restored.current = null;
    setMessage({ tone: "info", text: t("cw_files_last_gone", { path }) });
    go(null);
  }, [path, dir.error, go]);

  // A recent folder that is gone (deleted, renamed, its drive unplugged) leaves Recent.
  useEffect(() => {
    if (path == null || !(dir.error instanceof ApiError) || dir.error.status !== 404) return;
    if (prefs?.recent.includes(cleanPath(path))) forget(cleanPath(path));
  }, [path, dir.error, prefs, forget]);

  // A folder that opened is a visit: recent folders and the last location (on the console).
  useEffect(() => {
    if (path == null || !dir.loaded || dir.error || visited.current === path) return;
    visited.current = path;
    if (restored.current === path) restored.current = null;
    visit(path);
  }, [path, dir.loaded, dir.error, visit]);

  const goUp = useCallback(() => {
    if (path == null) return;
    const up = upFrom(path, volumes ?? []);
    // Focus the folder we came out of (or its drive in the drive list).
    go(up, up == null ? (volumeOf(path, volumes ?? [])?.path ?? "") : baseName(path));
  }, [path, volumes, go]);

  // Focus after a level change, and never lose it when the focused row goes away.
  useEffect(() => {
    const el = root.current;
    if (!el || topModal()) return;
    if (focusName.current != null) {
      if (path != null && !dir.loaded && !dir.error) return; // wait for the first page
      const name = focusName.current;
      focusName.current = null;
      const target =
        (path == null
          ? byAttr(el, "data-drive", name)
          : name
            ? (byAttr(el, "data-entry", name)?.querySelector<HTMLElement>("[data-entry-main]") ?? null)
            : null) ??
        el.querySelector<HTMLElement>("[data-entry-main]") ??
        el.querySelector<HTMLElement>("[data-drive]") ??
        el.querySelector<HTMLElement>('[data-action="up"]');
      if (target) restoreFocus(anchorOf(target));
      return;
    }
    if (afterMore.current != null && !dir.loading) {
      // The page came in under the focused "Show more": carry on down from where
      // the list ended, so holding ↓ keeps scrolling through a big folder.
      const at = afterMore.current;
      afterMore.current = null;
      const rows = el.querySelectorAll<HTMLElement>("[data-entry-main]");
      // ("Show more" itself is gone after the last page: focus was dropped with it.)
      if (rows.length && (focusLost() || document.activeElement?.closest('[data-action="more"]'))) {
        restoreFocus(anchorOf(rows[Math.min(at, rows.length - 1)]));
        return;
      }
    }
    if (lastAnchor.current && focusLost()) restoreFocus(lastAnchor.current);
  }, [path, dir.entries, dir.loaded, dir.loading, dir.error, volumes, clip, conflict]);

  const { loadMore } = dir;
  const onLoadMore = useCallback(() => {
    if (document.activeElement?.closest('[data-action="more"]')) afterMore.current = dir.entries.length;
    loadMore();
  }, [loadMore, dir.entries.length]);

  // Each item of a batch ends before the next starts: show the folder as it is now.
  const batchAt = busy?.at ?? 0;
  useEffect(() => {
    if (batchAt > 1) refresh();
  }, [batchAt, refresh]);
  // The page's own jobs still running (a batch's current item, or one a stopped batch left running).
  const tasks = jobs.filter((j) => j.state === "running" && j.mine);

  const report = useCallback(
    (r: BatchResult, op?: Busy["op"]) => {
      setMessage(batchMessage(r, op));
      refresh();
    },
    [refresh],
  );

  const onEntry = useCallback(
    (e: Entry) => {
      if (path == null) return;
      if (e.kind === "dir") return go(joinPath(path, e.name));
      const type = viewKind(e.name);
      setAsk(type ? { kind: "view", name: e.name, type } : { kind: "sheet", names: [e.name] });
    },
    [path, go],
  );

  const runBatch = useCallback(
    (
      op: Busy["op"],
      names: string[],
      work: (items: string[], opts: BatchOptions) => Promise<BatchResult>,
      destDir?: string,
    ) => {
      if (path == null || currentBatch()) return;
      const items = names.map((n) => joinPath(path, n));
      // The dialog closes at once: the batch runs on (one item at a time) with its progress and Stop
      // in the status line, here and when Files is opened again.
      setAsk(null);
      setMessage(null);
      setSelecting(false);
      setSelected([]);
      const run = startBatch(
        op,
        items.length,
        (ctl) =>
          work(items, { onProgress: ctl.onProgress, stopped: ctl.stopped, waitJob: (id) => waitJob(id, ctl.stopped) }),
        destDir != null ? items.concat(destDir) : items,
      );
      const batchDir = path;
      run?.then(
        (r) => {
          if (!mounted.current) return;
          report(r, op);
          // Still in that folder: what failed stays selected (Task 9 deferred).
          const keep = failedNamesIn(r, batchDir);
          if (keep.length && pathRef.current === batchDir) {
            setSelecting(true);
            setSelected(keep);
          }
        },
        () => undefined,
      );
    },
    [path, report],
  );

  const onConfirmDelete = useCallback(
    (names: string[]) => runBatch("delete", names, (items, opts) => deleteItems(items, opts)),
    [runBatch],
  );

  const onConfirmFixPerm = useCallback(
    (names: string[]) => runBatch("chmod", names, (items, opts) => fixPermItems(items, opts)),
    [runBatch],
  );

  const onPicked = useCallback(
    (op: "copy" | "move", names: string[], destDir: string) => {
      setAsk(null);
      runBatch(op, names, (items, opts) => (op === "copy" ? copyItems : moveItems)(items, destDir, opts), destDir);
    },
    [runBatch],
  );

  const onClip = useCallback(
    (mode: PasteMode, names: string[]) => {
      setAsk(null);
      if (path == null) return;
      const kinds = new Map(dir.entries.map((e) => [e.name, e.kind]));
      setClipboard(
        mode,
        names.map((n) => ({ path: joinPath(path, n), dir: kinds.get(n) === "dir" })),
      );
      setSelecting(false);
      setSelected([]);
    },
    [path, dir.entries],
  );

  const onPaste = useCallback(() => {
    const c = getClipboard();
    if (path == null || !c || currentBatch()) return;
    const dest = path;
    setMessage(null);
    const run = startBatch(
      c.mode === "cut" ? "move" : "copy",
      c.items.length,
      (ctl) =>
        pasteItems(
          c.items,
          dest,
          c.mode,
          { decide: askConflict, volumes: volumes ?? undefined },
          { onProgress: ctl.onProgress, stopped: ctl.stopped, waitJob: (id) => waitJob(id, ctl.stopped) },
        ),
      c.items.map((i) => i.path).concat(dest),
    );
    run?.then(
      (r) => {
        // A cut item that moved (or was skipped on purpose) leaves the clipboard; a copy stays to paste again.
        if (c.mode === "cut") dropFromClipboard(r.done.concat(r.declined));
        if (mounted.current) {
          setMessage(pasteMessage(r));
          refresh();
        }
      },
      () => undefined,
    );
  }, [path, volumes, refresh]);

  const onRename = useCallback(
    (name: string, newName: string) => {
      setAsk(null);
      if (path == null || newName === name) return;
      renameEntry(path, name, newName).then(
        () => {
          focusName.current = newName;
          refresh();
        },
        (e: unknown) => {
          const text = filesErrorText(e);
          if (text) setMessage({ tone: "error", text });
        },
      );
    },
    [path, refresh],
  );

  const onBulkRename = useCallback(
    (rows: RenameRow[]) => {
      if (path == null || currentBatch()) return;
      const dirPath = path;
      setAsk(null);
      setMessage(null);
      setSelecting(false);
      setSelected([]);
      const run = startBatch<RenameResult>(
        "rename",
        rows.length,
        (ctl) => runRenames(dirPath, rows, { onProgress: ctl.onProgress, stopped: ctl.stopped }),
        [dirPath],
      );
      run?.then(
        (r) => {
          if (!mounted.current) return;
          setMessage(renameMessage(r));
          if (r.done.length) focusName.current = r.done[0][1];
          refresh();
        },
        () => undefined,
      );
    },
    [path, refresh],
  );

  const onUnpack = useCallback(
    (name: string, outName: string | null) => {
      if (path == null || currentBatch()) return;
      const src = joinPath(path, name);
      setAsk(null);
      setMessage(null);
      let end: OpEnd | { end: "error"; error: unknown } = { end: "gone" };
      const run = startBatch(
        "unpack",
        1,
        async (ctl) => {
          const r: BatchResult = { started: [], moved: [], failed: [], jobFailed: [], skipped: [] };
          ctl.onProgress(1, 1);
          try {
            const job = await startOp("/api/fs/unpack", outName ? { path: src, name: outName } : { path: src });
            r.started.push(job);
            end = await watchOp(job, { stopped: ctl.stopped, onStatus: (s) => ctl.onBytes(s.bytes_done, s.bytes_total) });
          } catch (error) {
            end = { end: "error", error };
          }
          return r;
        },
        [src, path],
      );
      run?.then(
        () => {
          if (!mounted.current) return;
          setMessage(unpackMessage(end, name));
          refresh();
        },
        () => undefined,
      );
    },
    [path, refresh],
  );

  const onMkdir = useCallback(
    (name: string) => {
      setAsk(null);
      if (path == null) return;
      call("/api/fs/mkdir", { path: joinPath(path, name) }).then(
        () => {
          focusName.current = name;
          refresh();
        },
        (e: unknown) => {
          const text = filesErrorText(e);
          if (text) setMessage({ tone: "error", text });
        },
      );
    },
    [path, refresh],
  );

  const onNewFile = useCallback(
    (name: string) => {
      setAsk(null);
      if (path == null) return;
      createFile(joinPath(path, name)).then(
        () => {
          focusName.current = name;
          refresh();
          setAsk({ kind: "view", name, type: "text", edit: true });
        },
        (e: unknown) => {
          const text = filesErrorText(e);
          if (text) setMessage({ tone: "error", text });
        },
      );
    },
    [path, refresh],
  );

  const onDownload = useCallback((p: string) => {
    setAsk(null);
    downloadItem(p).then(
      (name) => {
        if (mounted.current) setMessage({ tone: "info", text: t("cw_files_download_started", { name }) });
      },
      (e: unknown) => {
        const text = filesErrorText(e);
        if (text && mounted.current) setMessage({ tone: "error", text });
      },
    );
  }, []);
  // Upload and Download only away from the console (its own browser may use the LAN address too).
  const [remote, setRemote] = useState(false);
  useEffect(() => {
    let live = true;
    void loadOnOtherDevice().then((r) => live && setRemote(r));
    return () => {
      live = false;
    };
  }, []);

  const onToggleItem = useCallback((name: string) => {
    setSelected((s) => (s.includes(name) ? s.filter((x) => x !== name) : s.concat(name)));
  }, []);

  const onToggleSelecting = useCallback(() => {
    setSelecting((on) => !on);
    setSelected([]);
  }, []);

  // What shows: hidden files, filter and sort applied (memoized: a 20,000-entry
  // folder is not re-sorted by a focus move).
  const shown = useMemo(() => visibleEntries(dir.entries, view), [dir.entries, view]);
  const status = useMemo(
    () => statusOf(shown, dir.entries.length, selected, dir.entries),
    [shown, dir.entries, selected],
  );
  const onView = useCallback((patch: Partial<ViewPrefs>) => {
    setView((v) => {
      const next = { ...v, ...patch };
      saveView(next);
      return next;
    });
    setRowLimit(ROW_WINDOW);
  }, []);
  const onMoreRows = useCallback(() => setRowLimit((n) => n + ROW_WINDOW), []);
  const { loadAll } = dir;
  const onSelectAll = useCallback(() => {
    if (dir.more && !dir.loading) {
      // Pages remain: read them first so "all" means the whole folder.
      selectAfterLoad.current = true;
      // A load that did not run (busy, folder left) or failed selects nothing later.
      void loadAll().then((r) => {
        if (r !== "ok") selectAfterLoad.current = false;
      });
      return;
    }
    setSelected((s) => selectAllShown(s, shown));
  }, [dir.more, dir.loading, loadAll, shown]);
  useEffect(() => {
    if (!selectAfterLoad.current || dir.loading) return;
    selectAfterLoad.current = false;
    if (!dir.error) setSelected((s) => selectAllShown(s, shown));
  }, [dir.loading, dir.error, shown]);
  const onSelectNone = useCallback(() => setSelected([]), []);
  const onSelectInvert = useCallback(() => setSelected((s) => invertShown(s, shown)), [shown]);

  // ○ up one level, △ actions for the focused row (or the selection), □ select.
  useEffect(
    () =>
      onNavAction((a) => {
        if (a === "refresh") {
          // Create: read the folder again (App's fallback still re-reads the status).
          if (path != null) refresh();
          return false;
        }
        const el = root.current;
        const active = document.activeElement;
        if (!el || !active || !el.contains(active)) return false;
        const row = active.closest("[data-entry]");
        const name = row ? row.getAttribute("data-entry") : null;
        if (a === "back") {
          if (path == null) return false; // the drive list: App takes focus to the rail
          goUp();
          return true;
        }
        if (a === "actions" && path == null) {
          const place = active.closest("[data-place]")?.getAttribute("data-place");
          if (place) {
            setAsk({ kind: "place", path: place });
            return true;
          }
          const vp = active.closest("[data-volume]")?.getAttribute("data-volume");
          const v = vp ? (volumes ?? []).find((x) => x.path === vp) : undefined;
          if (v && canEject(v)) {
            const held = ejectBlocker(v.path, currentBatch(), jobs);
            if (held) setMessage({ tone: "error", text: held });
            else setAsk({ kind: "eject", path: v.path, force: false });
          }
          else if (v && isImage(v)) setAsk({ kind: "unmount", path: v.path });
          return true;
        }
        if (a === "actions" && path != null) {
          if (selecting && selected.length) setAsk({ kind: "sheet", names: selected });
          else if (name != null) setAsk({ kind: "sheet", names: [name] });
          return true;
        }
        if (a === "select" && path != null) {
          if (!selecting) {
            setSelecting(true);
            setSelected(name != null ? [name] : []);
          } else if (name != null) onToggleItem(name);
          else onToggleSelecting();
          return true;
        }
        return false;
      }),
    [path, volumes, jobs, selecting, selected, goUp, onToggleItem, onToggleSelecting, refresh],
  );

  const onDismiss = useCallback(() => {
    setAsk(null);
    setAskBusy(false);
    setAskError(null);
  }, []);

  const onPin = useCallback(
    (p: string) => {
      setAsk(null);
      pin(p).then(
        () => setMessage({ tone: "info", text: t("cw_files_pin_done", { name: placeLabel(p, volumes ?? []) }) }),
        (e: unknown) => {
          const text = volumeErrorText(e);
          if (text) setMessage({ tone: "error", text });
        },
      );
    },
    [pin, volumes],
  );

  const onUnpin = useCallback(
    (p: string) => {
      setAsk(null);
      unpin(p).then(
        () => setMessage({ tone: "info", text: t("cw_files_unpin_done", { name: placeLabel(p, volumes ?? []) }) }),
        (e: unknown) => {
          const text = volumeErrorText(e);
          if (text) setMessage({ tone: "error", text });
        },
      );
    },
    [unpin, volumes],
  );

  /** Eject / Unmount: the dialog stays open (busy) until the helper answers; a failure shows in it. */
  const volumeCall = useCallback(
    (route: string, body: object, done: string, onError?: (e: unknown) => boolean) => {
      setAskBusy(true);
      setAskError(null);
      call(route, body).then(
        () => {
          setAskBusy(false);
          setAsk(null);
          setMessage({ tone: "info", text: done });
          setVolumesTick((n) => n + 1);
        },
        (e: unknown) => {
          setAskBusy(false);
          if (onError?.(e)) return;
          setAskError(volumeErrorText(e));
        },
      );
    },
    [],
  );

  const onEject = useCallback(
    (p: string, force: boolean) => {
      const held = ejectBlocker(p, currentBatch(), jobs);
      if (held) {
        setAskError(held);
        return;
      }
      volumeCall("/api/volumes/eject", { path: p, force }, t("cw_files_eject_done", { path: p }), (e) => {
        // The PS5's own services usually hold a USB drive open: ask once more, to eject anyway.
        if (force || !(e instanceof ApiError) || e.code !== "usb_eject_busy") return false;
        setAsk({ kind: "eject", path: p, force: true });
        return true;
      });
    },
    [volumeCall, jobs],
  );

  const gameRunning = ctx.status?.running != null;
  const onUnmount = useCallback(
    (p: string) => {
      if (isSystemVolume(p)) {
        setAskError(t("cw_files_unmount_system"));
        return;
      }
      if (gameRunning) {
        setAskError(t("cw_files_unmount_game"));
        return;
      }
      volumeCall("/api/volumes/unmount", { path: p }, t("cw_files_unmount_done", { path: p }));
    },
    [volumeCall, gameRunning],
  );

  const onGoTo = useCallback(
    (p: string) => {
      setAsk(null);
      go(p);
    },
    [go],
  );

  /** A search result or a disk usage row: open its folder with it focused. */
  const onReveal = useCallback(
    (dir: string, name: string | null) => {
      setAsk(null);
      go(dir, name ?? "");
    },
    [go],
  );

  return (
    <div
      ref={root}
      onFocus={(e) => {
        const target = e.target as Element;
        if (!target.closest("[data-nav-modal]")) lastAnchor.current = anchorOf(target);
      }}
    >
      <FilesView
        path={path}
        volumes={volumes}
        volumesError={volumesError}
        dir={dir}
        shown={shown}
        view={view}
        status={status}
        rowLimit={rowLimit}
        onMoreRows={onMoreRows}
        onView={onView}
        onLoadAll={loadAll}
        onSelectAll={onSelectAll}
        onSelectNone={onSelectNone}
        onSelectInvert={onSelectInvert}
        selecting={selecting}
        selected={selected}
        ask={ask}
        busy={busy}
        message={message}
        tasks={tasks}
        prefs={prefs}
        askBusy={askBusy}
        askError={askError}
        onPin={onPin}
        onUnpin={onUnpin}
        onShowPc={(x) => {
          setAsk(null);
          void showOnPc(x).then((m) => mounted.current && setMessage(m));
        }}
        uploading={uploadingHere}
        onEject={onEject}
        onUnmount={onUnmount}
        onGoTo={onGoTo}
        gameRunning={gameRunning}
        onStopBatch={stopBatchAndConflict}
        clip={clip}
        conflict={conflict}
        onClip={onClip}
        onPaste={onPaste}
        onClipClear={clearClipboard}
        onOpen={(p) => go(p)}
        onUp={goUp}
        onEntry={onEntry}
        onLoadMore={onLoadMore}
        onRetry={refresh}
        onRetryVolumes={() => setVolumesTick((n) => n + 1)}
        onToggleSelecting={onToggleSelecting}
        onToggleItem={onToggleItem}
        onAsk={(a) => {
          setMessage(null);
          setAskError(null);
          const held = a.kind === "eject" ? ejectBlocker(a.path, currentBatch(), jobs) : null;
          if (held) {
            setMessage({ tone: "error", text: held });
            return;
          }
          setAsk(a);
        }}
        onDismiss={onDismiss}
        onConfirmDelete={onConfirmDelete}
        onConfirmFixPerm={onConfirmFixPerm}
        onRename={onRename}
        onBulkRename={onBulkRename}
        onUnpack={onUnpack}
        onMkdir={onMkdir}
        onNewFile={onNewFile}
        onDownload={remote ? onDownload : undefined}
        remote={remote}
        onPicked={onPicked}
        onReveal={onReveal}
      />
    </div>
  );
}
