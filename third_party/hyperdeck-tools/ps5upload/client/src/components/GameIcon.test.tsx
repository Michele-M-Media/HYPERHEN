import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it, vi } from "vitest";

vi.mock("../state/engine", () => ({ getEngineUrl: () => "http://engine.test:19113" }));

import { GameIcon } from "./GameIcon";

describe("GameIcon", () => {
  it("shows a suffixed save folder's icon by its title id", () => {
    const out = renderToStaticMarkup(<GameIcon host="10.0.0.2" titleId="PPSA17221.bak" />);
    expect(out).toContain("title_id=PPSA17221&amp;size=128");
    expect(out).not.toContain("PPSA17221.bak");
  });

  it("asks the engine for a scaled cover sized to the icon (#450)", () => {
    expect(renderToStaticMarkup(<GameIcon host="10.0.0.2" titleId="PPSA17221" size={112} />)).toContain(
      "size=256",
    );
    expect(renderToStaticMarkup(<GameIcon host="10.0.0.2" titleId="PPSA17221" fill />)).toContain("size=384");
  });

  it("asks the console nothing for a name that isn't a title id", () => {
    const out = renderToStaticMarkup(<GameIcon host="10.0.0.2" titleId="sce_sdmemory" />);
    expect(out).not.toContain("app-icon");
  });
});
