import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it, vi } from "vitest";

vi.mock("@tauri-apps/api/core", () => ({ invoke: vi.fn() }));

import { FilesListView } from "./FilesTab";
import { RelatedTab } from "./RelatedTab";

describe("Files tab", () => {
  const files = Array.from({ length: 5000 }, (_, i) => ({
    path: `data/f${String(i).padStart(4, "0")}.bin`,
    size: 1024,
    encrypted: i === 0,
  }));

  it("renders only the rows in view of a long list, and says how many there are", () => {
    const out = renderToStaticMarkup(
      <FilesListView files={files} truncated={false} query="" onQuery={() => {}} scrollTop={0} onScroll={() => {}} />,
    );
    expect(out).toContain("data/f0000.bin");
    expect(out).not.toContain("data/f4999.bin");
    expect(out).toContain("5,000 files");
    expect(out).toContain('aria-label="Encrypted"');
  });

  it("filters by the search", () => {
    const out = renderToStaticMarkup(
      <FilesListView files={files} truncated query="f4999" onQuery={() => {}} scrollTop={0} onScroll={() => {}} />,
    );
    expect(out).toContain("data/f4999.bin");
    expect(out).toContain("1 files");
    expect(out).toContain("list shortened");
  });
});

describe("Related tab", () => {
  it("lists the title's other packages with where they are and a View action", () => {
    const out = renderToStaticMarkup(
      <RelatedTab
        groups={[{ kind: "update", items: [{ name: "Game", version: "01.010", where: "library", path: "/data/p/u.pkg" }] }]}
        onOpen={() => {}}
      />,
    );
    for (const s of ["Update", "Game", "01.010", "Library", "View"]) expect(out).toContain(s);
  });

  it("says so when there is nothing related", () => {
    expect(renderToStaticMarkup(<RelatedTab groups={[]} />)).toContain("No other packages");
  });
});
