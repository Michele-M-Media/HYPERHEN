// The sidebar's lists as settings.json mirrors them (`nav.hidden`, `nav.closed_sections`).

export interface NavSettings {
  hidden?: unknown;
  closed_sections?: unknown;
  /** Written by builds before hiding existed; no longer read. */
  favorites?: unknown;
}

const strings = (v: unknown): string[] =>
  Array.isArray(v) ? v.filter((x): x is string => typeof x === "string") : [];

/** The lists to hydrate from, or null when the file predates them (nothing to apply). */
export function navListsFromSettings(
  nav: NavSettings | undefined,
): { hidden: string[]; closedSections: string[] } | null {
  if (!nav || (!Array.isArray(nav.hidden) && !Array.isArray(nav.closed_sections))) return null;
  return { hidden: strings(nav.hidden), closedSections: strings(nav.closed_sections) };
}
