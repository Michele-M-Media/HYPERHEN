import { renderToStaticMarkup } from "react-dom/server";
import { beforeEach, describe, expect, it } from "vitest";
import { t } from "../strings";
import { UpdateBanner } from "./UpdateBanner";
import { _resetUpdateForTests, dismissUpdate, isNewerHelper, markUpdateReady } from "./update";

beforeEach(() => _resetUpdateForTests());

describe("updated page banner", () => {
  it("a helper version other than the page's means a newer page; unknowns are never compared", () => {
    expect(isNewerHelper("7.1.0", "7.0.34")).toBe(true);
    expect(isNewerHelper("7.0.34", "7.0.34")).toBe(false);
    expect(isNewerHelper(undefined, "7.0.34")).toBe(false); // an older helper's reply
    expect(isNewerHelper("", "7.0.34")).toBe(false);
    expect(isNewerHelper("7.1.0", "")).toBe(false); // tests / dev build
  });

  it("shows a one-press Reload (and Later) only once marked, and Later hides it", () => {
    expect(renderToStaticMarkup(<UpdateBanner />)).toBe("");
    markUpdateReady();
    const html = renderToStaticMarkup(<UpdateBanner />);
    expect(html).toContain(t("cw_update_ready"));
    expect(html).toContain(t("cw_error_reload"));
    expect(html).toContain(t("cw_update_later"));
    dismissUpdate();
    expect(renderToStaticMarkup(<UpdateBanner />)).toBe("");
  });
});
