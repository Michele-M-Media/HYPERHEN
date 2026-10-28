/** Previous / next through a folder's images (the order the list shows them). */

/** `names`' neighbours of `name`: null at either end (no wrap: the arrows say where the folder ends). */
export function imageNeighbours(names: string[], name: string): { prev: string | null; next: string | null; at: number; total: number } {
  const at = names.indexOf(name);
  if (at < 0) return { prev: null, next: null, at: -1, total: names.length };
  return {
    prev: at > 0 ? names[at - 1] : null,
    next: at < names.length - 1 ? names[at + 1] : null,
    at,
    total: names.length,
  };
}
