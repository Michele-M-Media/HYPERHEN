import { create } from "zustand";
import { safeGetItem, safeSetItem } from "../lib/safeStorage";

/** Four themes, two of each side:
 *    light — the daytime window
 *    peach — a warmer light, cream window and a burnt-peach accent
 *    dark  — the default; the same language at night
 *    oled  — dark on pure black, so an OLED panel's pixels stay off
 *
 *  6.x had "rose" (a pink light theme); a stored "rose" reads as peach.
 *  7.0.0–7.0.1 had only light and dark, and rewrote a stored "oled" to
 *  "dark", so those users pick OLED again once.
 *
 *  Storage key kept at the original "ps5upload.theme" so users don't
 *  lose their setting across the upgrade. */
export type Theme = "dark" | "light" | "oled" | "peach";
export type LightTheme = "light" | "peach";
export type DarkTheme = "dark" | "oled";

/** The light and dark the sun/moon toggle flips between: the last one of
 *  each side the user picked. */
export interface ThemePair {
  light: LightTheme;
  dark: DarkTheme;
}

const STORAGE_KEY = "ps5upload.theme";
/** "<light>,<dark>" — the remembered pair. */
const PAIR_KEY = "ps5upload.theme.pair";
const DEFAULT_PAIR: ThemePair = { light: "light", dark: "dark" };

/** A stored or mirrored theme value → a theme, or null when it isn't one
 *  (the caller picks the default). */
export function normalizeTheme(value: unknown): Theme | null {
  switch (value) {
    case "dark":
    case "light":
    case "oled":
    case "peach":
      return value;
    case "rose":
      return "peach";
    default:
      return null;
  }
}

export function isDarkTheme(theme: Theme): theme is DarkTheme {
  return theme === "dark" || theme === "oled";
}

/** The toggle: from either dark to the remembered light, and back. */
export function nextTheme(current: Theme, pair: ThemePair): Theme {
  return isDarkTheme(current) ? pair.light : pair.dark;
}

/** The stored pair, each side falling back to its plain theme. */
export function parsePair(stored: string | null): ThemePair {
  const [l, d] = (stored ?? "").split(",");
  return {
    light: l === "peach" ? "peach" : "light",
    dark: d === "oled" ? "oled" : "dark",
  };
}

function withTheme(pair: ThemePair, theme: Theme): ThemePair {
  return isDarkTheme(theme) ? { ...pair, dark: theme } : { ...pair, light: theme };
}

/** Read the persisted theme synchronously so the first paint is correct.
 *  Returning "dark" as the fallback keeps parity with the app's historical
 *  look for users who've never toggled. A legacy value is rewritten in place
 *  so the stored key only ever holds a current theme from here on. */
function initialTheme(): Theme {
  if (typeof window === "undefined") return "dark";
  const stored = safeGetItem(STORAGE_KEY);
  const theme = normalizeTheme(stored) ?? "dark";
  if (stored !== null && stored !== theme) safeSetItem(STORAGE_KEY, theme);
  return theme;
}

function initialPair(theme: Theme): ThemePair {
  const pair = typeof window === "undefined" ? DEFAULT_PAIR : parsePair(safeGetItem(PAIR_KEY));
  return withTheme(pair, theme);
}

/** Write the theme attribute onto <html>. `index.css` keys each theme's
 *  tokens off `:root[data-theme="<name>"]`; dark is the attribute-less
 *  default, so we remove the attr rather than set it. */
function applyTheme(theme: Theme) {
  if (typeof document === "undefined") return;
  if (theme === "dark") {
    delete document.documentElement.dataset.theme;
  } else {
    document.documentElement.dataset.theme = theme;
  }
}

interface ThemeState {
  theme: Theme;
  pair: ThemePair;
  setTheme: (theme: Theme) => void;
  /** Flips between the remembered light and dark. */
  toggleTheme: () => void;
}

export const useThemeStore = create<ThemeState>((set, get) => {
  const theme = initialTheme();
  const apply = (next: Theme) => {
    const pair = withTheme(get().pair, next);
    safeSetItem(STORAGE_KEY, next);
    safeSetItem(PAIR_KEY, `${pair.light},${pair.dark}`);
    applyTheme(next);
    set({ theme: next, pair });
  };
  return {
    theme,
    pair: initialPair(theme),
    setTheme: apply,
    toggleTheme: () => apply(nextTheme(get().theme, get().pair)),
  };
});

// Apply the initial theme on module load so the very first paint is
// right — beats waiting for React to mount.
if (typeof document !== "undefined") {
  applyTheme(useThemeStore.getState().theme);
}
