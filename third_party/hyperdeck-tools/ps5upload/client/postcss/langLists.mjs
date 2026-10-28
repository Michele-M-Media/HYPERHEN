// Split `:lang(a,b,c)` language lists in the built stylesheet.
//
// The build targets old WebKit (safari13), so the minifier rewrites logical properties
// (`rounded-s-full`, `start-3`, `border-s`…) into left/right pairs picked by language:
//
//   .rounded-s-full:not(:lang(ae,ar,…,yi)) { border-top-left-radius: … }
//   .rounded-s-full:lang(ae,ar,…,yi)       { border-top-right-radius: … }
//
// A language LIST inside :lang() is newer than the rest of that output: Chromium before
// 141 (Android System WebView on most phones, older WebView2) drops both rules, which
// left every split button square-cornered and every start/end-positioned control
// misplaced on Android. One language per :lang() works everywhere:
//
//   :not(:lang(ae,ar,fa)) ->  :not(:lang(ar)):not(:lang(fa))
//   .x:lang(ae,ar,fa)   ->  .x:lang(ar), .x:lang(fa)   (only the right-to-left
//                                                       languages the app ships)
//
// Run on the final CSS (vite.config.ts, generateBundle), after the minifier wrote it.
import postcss from "postcss";

/** Right-to-left languages the app has a translation for (client/src/i18n/locales). */
export const SHIPPED_RTL = ["ar", "fa"];

const NOT_LIST = /:not\(:lang\(([a-z-]+(?:,[a-z-]+)+)\)\)/gi;
const LIST = /:lang\(([a-z-]+(?:,[a-z-]+)+)\)/i;

function splitSelector(sel) {
  // Only the languages the app can be set to need excluding; the rest never occur, and
  // each extra :not() would add to the rule's specificity.
  const negated = sel.replace(NOT_LIST, (_, list) =>
    list
      .split(",")
      .filter((l) => SHIPPED_RTL.includes(l))
      .map((l) => `:not(:lang(${l}))`)
      .join(""),
  );
  const m = LIST.exec(negated);
  if (!m) return [negated];
  const langs = m[1].split(",").filter((l) => SHIPPED_RTL.includes(l));
  // Expand one list at a time; a selector with two lists is expanded again.
  return langs.flatMap((l) => splitSelector(negated.replace(m[0], `:lang(${l})`)));
}

export function splitLangLists(css) {
  if (!css.includes(":lang(") || !/:lang\([a-z-]+,/i.test(css)) return css;
  const root = postcss.parse(css);
  root.walkRules((rule) => {
    if (!/:lang\([a-z-]+,/i.test(rule.selector)) return;
    const selectors = rule.selectors.flatMap(splitSelector);
    if (selectors.length === 0) rule.remove();
    else rule.selectors = selectors;
  });
  return root.toResult({ map: false }).css.replace(/,\s*\n\s*/g, ",");
}
