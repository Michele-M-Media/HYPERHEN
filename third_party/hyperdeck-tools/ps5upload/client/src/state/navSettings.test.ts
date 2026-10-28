import { describe, expect, it } from "vitest";

import { navListsFromSettings } from "./navSettings";

describe("the sidebar lists in settings.json", () => {
  it("reads the hidden screens and closed sections, dropping junk", () => {
    expect(
      navListsFromSettings({ hidden: ["/shell", 3], closed_sections: ["nav_section_help"] }),
    ).toEqual({ hidden: ["/shell"], closedSections: ["nav_section_help"] });
  });

  it("says nothing for a file from before hiding existed (old favorites are ignored)", () => {
    expect(navListsFromSettings({ favorites: ["/games"] })).toBeNull();
    expect(navListsFromSettings(undefined)).toBeNull();
  });

  it("treats one missing list as empty", () => {
    expect(navListsFromSettings({ hidden: ["/cheats"] })).toEqual({
      hidden: ["/cheats"],
      closedSections: [],
    });
  });
});
