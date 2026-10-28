import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it, vi } from "vitest";

vi.mock("@tauri-apps/api/core", () => ({ invoke: vi.fn() }));

import { changeNotesText, PackagePanelView, type PanelChecks } from "./PackagePanel";
import type { GameInspection } from "../api/gameInspect";

const g: GameInspection = {
  source: {
    format: "split-pkg",
    location: "local",
    path: "/g/x.pkg",
    size: 3000,
    parts: [
      { path: "/g/x.pkg", size: 2000 },
      { path: "/g/x.pkg.0", size: 1000 },
    ],
  },
  identity: {
    title: "Jak X",
    titles: {},
    title_id: "CUSA07842",
    content_id: "UP9000-CUSA07842_00-SCUS974290000001",
    concept_id: null,
    platform: "ps4",
    category: "gp",
    content_type: "update",
    region: "US",
  },
  specs: {
    app_ver: "01.04",
    master_ver: "01.00",
    min_fw: "5.05",
    sdk_ver: "4.50",
    build_date: "2017-11-01",
    drm: null,
    age_rating: "12",
    languages: [],
    file_count: 12,
  },
  params: [
    { key: "TITLE", value: "Jak X" },
    { key: "ATTRIBUTE", value: "0x00000000" },
  ],
  change_notes:
    '<changeinfo><changes app_ver="01.04">Fixed crashes.</changes></changeinfo>',
  images: [{ name: "icon0.png", size: 10 }],
  authenticity: "fake",
  warnings: [],
  partial: false,
};

const checks: PanelChecks = {
  firmware: { verdict: "bad", minFw: "12.00", consoleFw: "5.10" },
  installed: { verdict: "ok", relation: "newer", installedVer: "01.02" },
  space: { verdict: "ok", shortBy: 0 },
};

const view = (over: Partial<Parameters<typeof PackagePanelView>[0]> = {}) =>
  renderToStaticMarkup(
    <PackagePanelView
      inspection={g}
      coverUrl={null}
      backdropUrl={null}
      checks={checks}
      actions={[]}
      tab="overview"
      onTab={() => {}}
      {...over}
    />,
  );

describe("PackagePanelView", () => {
  it("shows identity, badges, parts and what's new", () => {
    const html = view();
    for (const s of ["Jak X", "CUSA07842", "PS4", "Update", "US", "Fake (FPKG)", "5.05", "x.pkg.0", "Fixed crashes."]) {
      expect(html).toContain(s);
    }
    expect(html).not.toContain("&lt;changes");
  });

  it("says plainly when the console can't run it", () => {
    const html = view();
    expect(html).toContain("Needs 12.00");
    expect(html).toContain("5.10");
  });

  it("[RF 1] a partial inspection still shows header facts and the warning", () => {
    const html = view({
      inspection: {
        ...g,
        partial: true,
        warnings: ["PARAM is encrypted or missing — showing header facts only"],
        specs: { ...g.specs, min_fw: null },
      },
    });
    expect(html).toContain("CUSA07842");
    expect(html).toContain("encrypted or missing");
  });

  it("names a package's entries and a PS4 parental level for what they are", () => {
    const html = view();
    expect(html).toContain("Entries");
    expect(html).toContain("Parental level");
    const folder = view({
      inspection: {
        ...g,
        source: { ...g.source, format: "folder", parts: [] },
        identity: { ...g.identity, platform: "ps5" },
      },
    });
    expect(folder).toContain("Files");
    expect(folder).toContain("Age rating");
    expect(folder).not.toContain("Parental level");
  });

  it("details lists PARAM keys behind Show all", () => {
    const html = view({ tab: "details" });
    expect(html).toContain("TITLE");
    expect(html).toContain("Show all");
  });
});

describe("changeNotesText", () => {
  it("keeps the text inside CDATA sections", () => {
    const xml =
      '<?xml version="1.0"?><changeinfo><changes app_ver="01.02"><![CDATA[Fixed a crash in the hangar.\nBetter frame pacing.]]></changes></changeinfo>';
    expect(changeNotesText(xml)).toBe("Fixed a crash in the hangar.\nBetter frame pacing.");
  });
  it("hides an empty What's new", () => {
    const html = view({ inspection: { ...g, change_notes: "<changeinfo></changeinfo>" } });
    expect(html).not.toContain("What&#x27;s new");
  });
});

describe("installed line", () => {
  it("says when an update's base game is missing", () => {
    const html = view({
      checks: { ...checks, installed: { verdict: "bad", relation: "base-missing", installedVer: null } },
    });
    expect(html).toContain("Base game not installed");
  });
  it("shows no installed line when the console couldn't say", () => {
    const html = view({ checks: { ...checks, installed: null } });
    expect(html).not.toContain("Not installed");
  });
});
