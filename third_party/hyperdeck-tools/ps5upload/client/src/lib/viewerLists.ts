// Pure helpers behind the package viewer's Files and Related tabs.

import type { InspectFile } from "../api/gameInspect";

export function filterFiles(files: InspectFile[], query: string): InspectFile[] {
  const q = query.trim().toLowerCase();
  return q ? files.filter((f) => f.path.toLowerCase().includes(q)) : files;
}

/** The rows of a fixed-height list to render: those in view, plus `overscan` either side. */
export function visibleWindow(
  scrollTop: number,
  viewport: number,
  rowHeight: number,
  count: number,
  overscan = 8,
): { start: number; end: number } {
  if (count === 0) return { start: 0, end: 0 };
  const first = Math.floor(scrollTop / rowHeight);
  const inView = Math.ceil(viewport / rowHeight);
  return {
    start: Math.max(0, Math.min(first, count) - overscan),
    end: Math.min(count, first + inView + overscan),
  };
}

export type RelatedKind = "game" | "update" | "dlc" | "other";

export interface RelatedItem {
  name: string;
  version: string | null;
  where: "library" | "queue";
  /** The package's path on the console (library rows), for opening it in the viewer. */
  path: string | null;
  /** A queued install's state. */
  status?: string;
}

interface LibraryLike {
  path: string;
  /** The original file on this computer, when the viewer was opened on it. */
  sourcePath?: string;
  contentId?: string;
  category?: string;
  appVer?: string;
  title?: string;
}

interface QueueLike {
  id: string;
  displayName: string;
  contentId?: string | null;
  category?: string | null;
  status: string;
}

function titleOf(contentId: string | null | undefined): string | null {
  const id = contentId?.split("-")[1]?.split("_")[0] ?? "";
  return /^[A-Z]{4}\d{5}$/.test(id) ? id : null;
}

function kindOf(category: string | null | undefined): RelatedKind {
  const c = (category ?? "").toLowerCase();
  if (c.startsWith("gd")) return "game";
  if (c.startsWith("gp")) return "update";
  if (c.startsWith("ac")) return "dlc";
  return "other";
}

const ORDER: RelatedKind[] = ["game", "update", "dlc", "other"];

/** The title's other packages in the library and the queue, grouped base → update → DLC. */
export function relatedPackages(
  titleId: string,
  currentPath: string | null,
  library: LibraryLike[],
  queue: QueueLike[],
): { kind: RelatedKind; items: RelatedItem[] }[] {
  if (!titleId) return [];
  const byKind = new Map<RelatedKind, RelatedItem[]>();
  const add = (kind: RelatedKind, item: RelatedItem) =>
    byKind.set(kind, [...(byKind.get(kind) ?? []), item]);
  for (const e of library) {
    if (e.path === currentPath || (currentPath && e.sourcePath === currentPath)) continue;
    if (titleOf(e.contentId) !== titleId) continue;
    add(kindOf(e.category), {
      name: e.title || e.path.split("/").pop() || e.path,
      version: e.appVer ?? null,
      where: "library",
      path: e.path,
    });
  }
  for (const q of queue) {
    if (titleOf(q.contentId) !== titleId) continue;
    add(kindOf(q.category), {
      name: q.displayName,
      version: null,
      where: "queue",
      path: null,
      status: q.status,
    });
  }
  return ORDER.filter((k) => byKind.has(k)).map((kind) => ({ kind, items: byKind.get(kind)! }));
}
