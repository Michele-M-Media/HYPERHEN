import { isValidElement, type ReactElement, type ReactNode } from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it, vi } from "vitest";

vi.mock("../api", async (importOriginal) => {
  const real = await importOriginal<typeof import("../api")>();
  return { ...real, call: vi.fn(), callBlob: vi.fn() };
});

import type { Volume } from "../home/Home";
import { t } from "../strings";
import { ActionsSheet } from "./ActionsSheet";
import { FilesView, sheetActions, type FilesViewProps } from "./Files";
import type { DirState, Entry } from "./useDir";
import { GameBanner, GameBannerView } from "./viewer/GameBanner";
import { HexView } from "./viewer/HexView";
import { TextView, findCountText, findTyped, type FindState } from "./viewer/TextView";
import { ROW_WINDOW, toRows } from "./viewer/text";
import { GameSummary, textModel, Viewer } from "./Viewer";

function elements(node: ReactNode): ReactElement<Record<string, unknown>>[] {
  const out: ReactElement<Record<string, unknown>>[] = [];
  const walk = (n: ReactNode) => {
    if (Array.isArray(n)) return n.forEach(walk);
    if (!isValidElement(n)) return;
    const el = n as ReactElement<Record<string, unknown>>;
    out.push(el);
    walk(el.props.children as ReactNode);
  };
  walk(node);
  return out;
}
function find<T>(node: ReactNode, type: T): ReactElement<Record<string, unknown>> {
  const el = elements(node).find((e) => e.type === type);
  if (!el) throw new Error("not rendered");
  return el;
}

const entry = (name: string, kind: Entry["kind"] = "file", size = 10): Entry => ({ name, kind, size, mtime: 0 });
const dir = (entries: Entry[]): DirState => ({ entries, more: false, loading: false, loaded: true, error: null });
const vol = (path: string): Volume => ({ path, mount_from: "/dev/ssd0.user", fs_type: "ufs", total_bytes: 1000, free_bytes: 400, writable: true });
const ENTRIES = [entry("sce_sys", "dir"), entry("b.png"), entry("notes.txt"), entry("a.jpg"), entry("eboot.bin"), entry("c.PNG")];

function props(over: Partial<FilesViewProps> = {}): FilesViewProps {
  return {
    path: "/data/games/Astro",
    volumes: [vol("/data")],
    volumesError: false,
    dir: dir(ENTRIES),
    selecting: false,
    selected: [],
    ask: null,
    busy: null,
    message: null,
    tasks: [],
    onOpen: vi.fn(),
    onUp: vi.fn(),
    onEntry: vi.fn(),
    onLoadMore: vi.fn(),
    onToggleSelecting: vi.fn(),
    onToggleItem: vi.fn(),
    onAsk: vi.fn(),
    onDismiss: vi.fn(),
    onConfirmDelete: vi.fn(),
    onConfirmFixPerm: vi.fn(),
    onRename: vi.fn(),
    onMkdir: vi.fn(),
    onPicked: vi.fn(),
    ...over,
  };
}

describe("viewer in Files", () => {
  it("any file can be viewed as hex; a folder cannot", () => {
    expect(sheetActions(["eboot.bin"], ENTRIES, null)).toContain("hex");
    expect(sheetActions(["sce_sys"], ENTRIES, false)).not.toContain("hex");
    const onAsk = vi.fn();
    const sheet = find(FilesView(props({ onAsk, ask: { kind: "sheet", names: ["eboot.bin"] } })), ActionsSheet);
    (sheet.props.onChoose as (a: string) => void)("hex");
    expect(onAsk).toHaveBeenCalledWith({ kind: "view", name: "eboot.bin", type: "hex" });
  });

  it("an image's previous / next are the folder's images in list order", () => {
    const onAsk = vi.fn();
    const shown = [ENTRIES[0], ENTRIES[5], ENTRIES[1], ENTRIES[2], ENTRIES[3]];
    const v = find(FilesView(props({ onAsk, shown, ask: { kind: "view", name: "b.png", type: "image" } })), Viewer);
    expect(v.props.images).toEqual(["c.PNG", "b.png", "a.jpg"]);
    (v.props.onOpenImage as (n: string) => void)("a.jpg");
    expect(onAsk).toHaveBeenCalledWith({ kind: "view", name: "a.jpg", type: "image" });
    const text = find(FilesView(props({ ask: { kind: "view", name: "notes.txt", type: "text" } })), Viewer);
    expect(text.props.images).toBeUndefined();
  });

  it("a folder with sce_sys shows the game banner; others do not", () => {
    const b = find(FilesView(props()), GameBanner);
    expect(b.props).toMatchObject({ folder: "/data/games/Astro", sceSys: "sce_sys" });
    expect(elements(FilesView(props({ dir: dir([entry("x.txt")]) }))).some((e) => e.type === GameBanner)).toBe(false);
    // not before the listing has loaded
    expect(elements(FilesView(props({ dir: { ...dir(ENTRIES), loaded: false } }))).some((e) => e.type === GameBanner)).toBe(false);
  });

  it("the banner shows title, id, version, firmware and the icon", () => {
    const html = renderToStaticMarkup(
      <GameBannerView
        game={{
          info: { title: "ASTRO BOT", titleId: "PPSA01650", contentId: "EP9000-PPSA01650_00-ASTROBOT00000000", version: "01.002.000", firmware: "9.60" },
          source: "param.json",
          iconPath: "/x/sce_sys/icon0.png",
        }}
        icon="blob:icon"
      />,
    );
    expect(html).toContain("ASTRO BOT");
    expect(html).toContain("PPSA01650 · version 01.002.000 · needs system software 9.60");
    expect(html).toContain('src="blob:icon"');
    expect(html).toContain(t("cw_files_game_ps5"));
    const ps4 = renderToStaticMarkup(
      <GameBannerView
        game={{ info: { title: null, titleId: "CUSA07842", contentId: null, version: null, firmware: null }, source: "param.sfo", iconPath: "" }}
        icon={null}
      />,
    );
    expect(ps4).toContain(t("cw_files_game_ps4"));
    expect(ps4).toContain("CUSA07842");
    expect(ps4).not.toContain("<img");
  });

  it("text: JSON pretty printed, a bad JSON said so, param.json summarized", () => {
    const m = textModel("/d/sce_sys/param.json", "param.json", '{"titleId":"PPSA01650","localizedParameters":{"en-US":{"titleName":"ASTRO BOT"}}}');
    expect(m.rows[0].text).toBe("{");
    expect(m.rows[1].text).toBe('  "titleId": "PPSA01650",');
    expect(m.jsonRaw).toBe(false);
    expect(m.game).toMatchObject({ title: "ASTRO BOT", titleId: "PPSA01650" });
    const bad = textModel("/d/x.json", "x.json", "{nope");
    expect(bad.jsonRaw).toBe(true);
    expect(bad.rows[0].text).toBe("{nope");
    expect(bad.game).toBeNull();
    const txt = textModel("/d/a.log", "a.log", '{"a":1}');
    expect(txt.rows[0].text).toBe('{"a":1}');
    expect(renderToStaticMarkup(<GameSummary info={m.game!} />)).toContain("ASTRO BOT");
  });

  it("text view: numbered lines, on-screen find buttons, wrap toggle; text is never HTML", () => {
    const html = renderToStaticMarkup(<TextView rows={toRows("first\n<b>bold</b>\nthird")} />);
    expect(html).toContain('data-action="find-field"');
    expect(html).toMatch(/data-action="find-prev"[^>]*disabled/);
    expect(html).toMatch(/data-action="find-next"[^>]*disabled/);
    expect(html).toContain('data-action="wrap" aria-pressed="true"');
    expect(html).toContain("&lt;b&gt;bold&lt;/b&gt;");
    expect(html).toMatch(/cw-tv-n"[^>]*>3</);
    expect(findCountText("", 0, -1, false)).toBeNull();
    expect(findCountText("x", 0, -1, false)).toBe(t("cw_files_vw_find_none"));
    expect(findCountText("x", 17, 2, false)).toBe(t("cw_files_vw_find_at", { at: 3, n: "17" }));
    expect(findCountText("x", 10000, 0, true)).toContain(t("cw_files_vw_find_capped"));
  });

  it("typing finds at once: the count shows without Next; Next then steps on", () => {
    const rows = toRows("alpha\nbeta alpha\ngamma\nalpha");
    const s0: FindState = { found: { query: "", matches: [], capped: false }, current: -1, win: { start: 0, end: rows.length } };
    const s1 = findTyped(s0, "alpha", rows);
    expect(s1.found.matches.length).toBe(3);
    expect(s1.current).toBe(0);
    expect(findCountText(s1.found.query, s1.found.matches.length, s1.current, false)).toBe(t("cw_files_vw_find_at", { at: 1, n: "3" }));
    expect(findTyped(s1, "alpha", rows)).toBe(s1); // already found: unchanged
    expect(findTyped(s1, "zzz", rows).found.matches).toEqual([]);
    const cleared = findTyped(s1, "", rows);
    expect(cleared.found.query).toBe("");
    expect(cleared.current).toBe(-1);
  });

  it("a find jump in a 50,000-line file draws a bounded window around the match", () => {
    const lines = Array.from({ length: 50_000 }, (_, i) => (i === 40_000 ? "the needle is here" : `line ${i + 1}`));
    const html = renderToStaticMarkup(<TextView rows={toRows(lines.join("\n"))} initialQuery="needle" />);
    const drawn = html.split('class="cw-tv-row"').length - 1;
    expect(drawn).toBeLessThanOrEqual(ROW_WINDOW + 30);
    expect(drawn).toBeGreaterThan(0);
    expect(html).toContain('<mark data-match="0" class="is-current">needle</mark>');
    expect(html).toMatch(/cw-tv-n"[^>]*>40001</);
    expect(html).not.toMatch(/cw-tv-n"[^>]*>1</);
    expect(html).toContain('data-action="earlier-lines"');
    expect(html).toContain('data-action="more-lines"');
  });

  it("hex view: offset | hex | ASCII, a page at a time", () => {
    const bytes = new Uint8Array(2048).map((_, i) => i & 255);
    const html = renderToStaticMarkup(<HexView bytes={bytes} size={1_000_000} />);
    expect(html).toContain("00000000");
    expect(html).toContain("00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f");
    expect(html).not.toContain("00000400"); // page 2
    expect(html).toContain(t("cw_files_vw_page", { at: 1, n: 2 }));
    expect(html).toMatch(/data-action="hex-prev" aria-disabled="true"/);
    expect(html).toMatch(/data-action="hex-first" aria-disabled="true"/);
    expect(html).not.toMatch(/data-action="hex-next" aria-disabled/);
    expect(html).not.toMatch(/data-action="hex-last" aria-disabled/);
    expect(html).not.toMatch(/data-action="hex-(prev|next|first|last)"[^>]*disabled=""/); // never disabled: focus stays
    expect(html).toContain("First 64 KiB of");
  });

  it("the viewer offers previous / next image arrows (disabled at the ends) and a text file's hex toggle", () => {
    const img = renderToStaticMarkup(
      <Viewer path="/d/a.png" name="a.png" kind="image" images={["a.png", "b.png"]} onOpenImage={vi.fn()} onClose={vi.fn()} />,
    );
    expect(img).toMatch(/data-action="image-prev"[^>]*aria-disabled="true"/);
    expect(img).not.toMatch(/data-action="image-next"[^>]*aria-disabled/);
    expect(img).not.toMatch(/data-action="image-(prev|next)"[^>]*disabled=""/);
    expect(img).toContain(t("cw_files_vw_image_at", { at: 1, n: 2 }));
    const txt = renderToStaticMarkup(<Viewer path="/d/a.txt" name="a.txt" kind="text" onClose={vi.fn()} />);
    expect(txt).toContain('data-action="as-hex"');
  });
});
