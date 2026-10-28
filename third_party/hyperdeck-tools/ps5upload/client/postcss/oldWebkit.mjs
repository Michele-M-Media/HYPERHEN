// Fallbacks for the old WebKit in macOS Big Sur (Safari 14 / early 15).
//
// The desktop app renders in the system WKWebView, which on Big Sur can be Safari 14.0. The
// built stylesheet uses three things that WebKit doesn't know, and the minifier adds no
// fallback for them (it even drops the hand-written `height:100%` before `height:100dvh`):
//
//   * dynamic viewport units (`100dvh`, Safari 15.4): `html, body, #root` lost their height,
//     so the app shell grew to its content and nothing could scroll;
//   * `padding-inline` / `margin-inline` / `padding-block` / `margin-block` (Safari 14.1):
//     every px-*, mx-auto, py-* … did nothing, so rows sat flush against the left edge;
//   * the `inset` shorthand (Safari 14.1).
//
// Each such declaration gets an older equivalent written just BEFORE it, so newer engines
// still use the original (the later declaration wins) and old ones keep the fallback.
// Two-value inline shorthands depend on writing direction and are left alone.
// Run on the final CSS (vite.config.ts, generateBundle).
import postcss from "postcss";

const DYN_VH = /(\d*\.?\d+)[dsl]vh\b/g;
const SIDES = {
  "padding-inline": ["padding-left", "padding-right"],
  "margin-inline": ["margin-left", "margin-right"],
  "padding-block": ["padding-top", "padding-bottom"],
  "margin-block": ["margin-top", "margin-bottom"],
};

function before(decl, prop, value) {
  decl.cloneBefore({ prop, value });
}

export function addOldWebkitFallbacks(css) {
  if (!/[dsl]vh\b|(padding|margin)-(inline|block)\s*:|[{;]\s*inset\s*:/.test(css)) return css;
  const root = postcss.parse(css);
  root.walkDecls((decl) => {
    const prop = decl.prop.toLowerCase();
    if (DYN_VH.test(decl.value)) {
      DYN_VH.lastIndex = 0;
      before(decl, decl.prop, decl.value.replace(DYN_VH, "$1vh"));
    }
    DYN_VH.lastIndex = 0;
    const sides = SIDES[prop];
    if (sides) {
      const parts = postcss.list.space(decl.value);
      if (parts.length === 1) {
        for (const side of sides) before(decl, side, decl.value);
      } else if (parts.length === 2 && prop.endsWith("-block")) {
        before(decl, sides[0], parts[0]);
        before(decl, sides[1], parts[1]);
      }
      return;
    }
    if (prop === "inset") {
      const p = postcss.list.space(decl.value);
      const [t, r = t, b = t, l = r] = p;
      if (p.length >= 1 && p.length <= 4) {
        before(decl, "top", t);
        before(decl, "right", r);
        before(decl, "bottom", b);
        before(decl, "left", l);
      }
    }
  });
  return root.toResult({ map: false }).css;
}
