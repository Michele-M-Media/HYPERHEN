import { describe, expect, it } from "vitest";

// @ts-expect-error plain .mjs helper, no type declarations
import { addOldWebkitFallbacks } from "./oldWebkit.mjs";

describe("addOldWebkitFallbacks", () => {
  it("gives dynamic viewport heights a vh fallback first, so Big Sur keeps a fixed app height", () => {
    expect(addOldWebkitFallbacks("html,body,#root{height:100dvh}")).toBe(
      "html,body,#root{height:100vh;height:100dvh}",
    );
    expect(addOldWebkitFallbacks(".m{max-height:calc(100dvh - .5rem)}")).toBe(
      ".m{max-height:calc(100vh - .5rem);max-height:calc(100dvh - .5rem)}",
    );
    expect(addOldWebkitFallbacks(".s{min-height:90svh}")).toBe(".s{min-height:90vh;min-height:90svh}");
  });

  it("writes left/right before a one-value inline padding or margin", () => {
    expect(addOldWebkitFallbacks(".px-4{padding-inline:calc(var(--spacing) * 4)}")).toBe(
      ".px-4{padding-left:calc(var(--spacing) * 4);padding-right:calc(var(--spacing) * 4);padding-inline:calc(var(--spacing) * 4)}",
    );
    expect(addOldWebkitFallbacks(".mx{margin-inline:auto!important}")).toBe(
      ".mx{margin-left:auto!important;margin-right:auto!important;margin-inline:auto!important}",
    );
  });

  it("splits block shorthands and inset, and leaves direction-dependent two-value inline alone", () => {
    expect(addOldWebkitFallbacks(".py{padding-block:1px 2px}")).toBe(
      ".py{padding-top:1px;padding-bottom:2px;padding-block:1px 2px}",
    );
    expect(addOldWebkitFallbacks(".i{inset:0}")).toBe(".i{top:0;right:0;bottom:0;left:0;inset:0}");
    expect(addOldWebkitFallbacks(".x{padding-inline:1px 2px}")).toBe(".x{padding-inline:1px 2px}");
  });

  it("leaves other CSS untouched", () => {
    const css = ".a{color:red}.b{inset-inline-start:0}";
    expect(addOldWebkitFallbacks(css)).toBe(css);
  });
});
