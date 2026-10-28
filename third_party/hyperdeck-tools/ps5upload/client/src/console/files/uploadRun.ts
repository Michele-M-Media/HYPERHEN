/**
 * An upload of picked files into one folder, one file at a time: folders
 * first, then each file; a name already there asks (Skip / Keep both /
 * Replace, with "the same for the rest"), as a paste does. Pure over its
 * deps: UploadDialog wires the helper calls and the question.
 */
import { ApiError } from "../api";
import { joinPath } from "./move";
import { freeName, type ConflictChoice } from "./paste";
import { uploadFolders, type UploadItem } from "./transfer";

export interface UploadProgress {
  /** Index of the file being sent (or asked about). */
  at: number;
  total: number;
  name: string;
  /** Bytes of this file sent so far, and its size. */
  sent: number;
  size: number;
  /** Bytes of all files finished or skipped, and of all files. */
  doneBytes: number;
  totalBytes: number;
}

export interface UploadSummary {
  uploaded: string[];
  skipped: string[];
  failed: { name: string; error: unknown }[];
  /** Stop came: these were not sent. */
  stopped: string[];
}

export interface UploadDeps {
  mkdirs: (dir: string, folders: string[]) => Promise<void>;
  /** "file" / "dir" / null (nothing there). */
  kindAt: (path: string) => Promise<"file" | "dir" | null>;
  upload: (dest: string, file: Blob, overwrite: boolean, onProgress: (sent: number) => void) => Promise<void>;
  /** A name is taken: what to do (and whether for the rest too). `canReplace` false for a folder there. */
  decide: (name: string, canReplace: boolean, left: number) => Promise<{ choice: ConflictChoice; all: boolean }>;
  onProgress?: (p: UploadProgress) => void;
  stopped?: () => boolean;
}

/** Last part of a relative path. */
function leaf(rel: string): string {
  return rel.slice(rel.lastIndexOf("/") + 1);
}

function dirOf(rel: string): string {
  const i = rel.lastIndexOf("/");
  return i < 0 ? "" : rel.slice(0, i);
}

export async function runUploads(items: UploadItem[], dir: string, deps: UploadDeps): Promise<UploadSummary> {
  const sum: UploadSummary = { uploaded: [], skipped: [], failed: [], stopped: [] };
  const totalBytes = items.reduce((s, it) => s + it.file.size, 0);
  let doneBytes = 0;
  let always: ConflictChoice | null = null;
  const folders = uploadFolders(items);
  if (folders.length) await deps.mkdirs(dir, folders);
  for (let i = 0; i < items.length; i++) {
    const it = items[i];
    if (deps.stopped?.()) {
      sum.stopped.push(...items.slice(i).map((x) => x.rel));
      break;
    }
    const base = dirOf(it.rel) ? joinPath(dir, dirOf(it.rel)) : dir;
    let name = leaf(it.rel);
    const report = (sent: number) =>
      deps.onProgress?.({ at: i, total: items.length, name: it.rel, sent, size: it.file.size, doneBytes, totalBytes });
    report(0);
    try {
      let overwrite = false;
      const there = await deps.kindAt(joinPath(base, name));
      if (there) {
        const canReplace = there === "file";
        let choice = always;
        if (choice === "replace" && !canReplace) choice = null;
        if (!choice) {
          const d = await deps.decide(it.rel, canReplace, items.length - i - 1);
          choice = d.choice;
          if (d.all) always = d.choice;
        }
        if (choice === "skip" || deps.stopped?.()) {
          (choice === "skip" ? sum.skipped : sum.stopped).push(it.rel);
          doneBytes += it.file.size;
          continue;
        }
        if (choice === "keep") name = await freeName(base, name, false, async (p) => (await deps.kindAt(p)) != null);
        else overwrite = true;
      }
      await deps.upload(joinPath(base, name), it.file, overwrite, report);
      sum.uploaded.push(it.rel);
    } catch (error) {
      if (error instanceof ApiError && error.code === "aborted") sum.stopped.push(it.rel);
      else sum.failed.push({ name: it.rel, error });
      if (error instanceof ApiError && error.status === 401) {
        sum.stopped.push(...items.slice(i + 1).map((x) => x.rel));
        break;
      }
    }
    doneBytes += it.file.size;
  }
  return sum;
}
