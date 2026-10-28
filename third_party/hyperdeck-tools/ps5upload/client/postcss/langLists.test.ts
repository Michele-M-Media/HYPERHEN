import { describe, expect, it } from "vitest";

import { splitLangLists } from "./langLists.mjs";

const LIST = "ae,ar,arc,fa,he";

describe("splitLangLists", () => {
  it("turns a negated language list into one check per shipped right-to-left language", () => {
    const out = splitLangLists(`.rounded-s-full:not(:lang(${LIST})){border-top-left-radius:9px}`);
    expect(out).toBe(
      ".rounded-s-full:not(:lang(ar)):not(:lang(fa)){border-top-left-radius:9px}",
    );
  });

  it("gives a matching language list one selector per right-to-left language the app ships", () => {
    const out = splitLangLists(`.end-3:lang(${LIST}){left:1rem}`);
    expect(out).toBe(".end-3:lang(ar),.end-3:lang(fa){left:1rem}");
  });

  it("keeps the other selectors of the rule and leaves single languages alone", () => {
    const out = splitLangLists(`.a:lang(${LIST}),.b{x:1}:root:lang(ar) .c{y:2}`);
    expect(out).toBe(".a:lang(ar),.a:lang(fa),.b{x:1}:root:lang(ar) .c{y:2}");
  });

  it("drops a matching rule whose list holds none of the shipped languages", () => {
    expect(splitLangLists(".x:lang(he,yi){a:1}.y{b:2}")).toBe(".y{b:2}");
  });
});

describe("SHIPPED_RTL", () => {
  it("names every right-to-left language the app can be set to", async () => {
    const { SHIPPED_RTL } = await import("./langLists.mjs");
    const { RTL_LANGS } = await import("../src/state/lang");
    expect([...SHIPPED_RTL].sort()).toEqual([...RTL_LANGS].sort());
  });
});
