import { renderToStaticMarkup } from "react-dom/server";
import { beforeEach, describe, expect, it, vi } from "vitest";

vi.mock("../api", async (importOriginal) => {
  const real = await importOriginal<typeof import("../api")>();
  return { ...real, call: vi.fn(), callRaw: vi.fn() };
});

import { ApiError, call, callRaw } from "../api";
import { t } from "../strings";
import { createFinder, downloadErrorText, FINDER_CLOSED, storeRoute, type FinderState, type StoredFile } from "./finder";
import { FindCheats } from "./FindCheats";
import { CHEAT_REPOS, contentUrls, parseCheatFilename, RepoError, storeFormat, type CheatEntry, type FetchLike, type RepoIndex } from "./repos";

const mockCall = vi.mocked(call);
const mockRaw = vi.mocked(callRaw);
const GAME = "CUSA15438";
const MIX = CHEAT_REPOS[2];

const entry = (filename: string, repoId = "etahen", gameTitle = "Game"): CheatEntry => {
  const [titleId, gameVersion] = parseCheatFilename(filename);
  return { filename, gameTitle, format: storeFormat(filename), repoId, titleId, gameVersion };
};

const INDEX: RepoIndex = {
  entries: [
    entry("CUSA15438_01.00.json"),
    entry("CUSA15438_01.02.shn", "goldhen"),
    entry("CUSA15438_01.03.json", "henmix", ""),
    entry("CUSA99999_01.00.json"),
    entry("CUSA15438_01.02 copy.json"), // a name the helper would refuse: never listed
  ],
  failed: [],
  offline: false,
};

/** The helper: /api/cheats/version answers `version`; /api/cheats/store records what it got. */
function helper(opts: { version?: string | null; versionFails?: boolean; storeError?: ApiError; replaced?: boolean } = {}) {
  const stored: { route: string; body: string }[] = [];
  mockCall.mockImplementation(async (route: string) => {
    if (route === "/api/cheats/version") {
      if (opts.versionFails) throw new ApiError(500, "internal");
      return { version: opts.version ?? null } as never;
    }
    throw new Error(`unexpected ${route}`);
  });
  mockRaw.mockImplementation(async (route: string, body: Blob) => {
    if (opts.storeError) throw opts.storeError;
    stored.push({ route, body: await body.text() });
    return { ok: true, replaced: opts.replaced === true } as never;
  });
  return stored;
}

/** GitHub for the content files: url → body; anything else 404. */
function github(files: Record<string, string>, down = false): FetchLike {
  return async (url) => {
    if (down) throw new TypeError("Load failed");
    return url in files ? new Response(files[url], { status: 200 }) : new Response("404", { status: 404 });
  };
}

function finder(opts: { index?: RepoIndex | Error; fetch?: FetchLike } = {}) {
  const states: FinderState[] = [];
  const stored: [CheatEntry, string][] = [];
  const full: StoredFile[] = [];
  const f = createFinder(
    (s) => states.push(s),
    (x) => {
      stored.push([x.entry, x.title]);
      full.push(x);
    },
    {
      index: async () => {
        if (opts.index instanceof Error) throw opts.index;
        return opts.index ?? INDEX;
      },
      fetch: opts.fetch ?? github({}),
    },
  );
  return { f, states, stored, full };
}

const render = (state: FinderState) =>
  renderToStaticMarkup(<FindCheats state={state} onDownload={() => {}} onRetry={() => {}} onClose={() => {}} />);

beforeEach(() => {
  mockCall.mockReset();
  mockRaw.mockReset();
});

describe("find cheats", () => {
  it("lists_the_running_games_files_installed_version_first", async () => {
    helper({ version: "01.02" });
    const { f } = finder();
    await f.open(GAME);
    const s = f.get();
    expect(s.kind).toBe("list");
    if (s.kind !== "list") return;
    expect(s.version).toBe("01.02");
    expect(s.entries.map((e) => e.filename)).toEqual(["CUSA15438_01.02.shn", "CUSA15438_01.03.json", "CUSA15438_01.00.json"]);
    expect(mockCall).toHaveBeenCalledWith("/api/cheats/version", { title_id: GAME });
    const html = render(s);
    expect(html).toContain(t("cw_cheats_find_title", { title: GAME }));
    expect(html).toContain(t("cw_cheats_find_your_version"));
    expect(html).toContain("is-match");
    expect(html).toContain("GoldHEN/GoldHEN_Cheat_Repository");
    expect(html).toContain("SHN");
    expect(html).toContain(t("cw_cheats_find_version", { v: "01.02" }));
    expect(html).not.toContain("CUSA99999");
    expect(html).not.toContain("copy.json");
    // the best file is where focus starts
    expect(html.indexOf("data-nav-default")).toBeLessThan(html.indexOf("CUSA15438_01.03.json"));
  });

  it("an_unknown_version_lists_all_newest_first", async () => {
    helper({ versionFails: true });
    const { f } = finder();
    await f.open(GAME);
    const s = f.get();
    if (s.kind !== "list") throw new Error(s.kind);
    expect(s.version).toBeNull();
    expect(s.entries.map((e) => e.gameVersion)).toEqual(["01.03", "01.02", "01.00"]);
    const html = render(s);
    expect(html).not.toContain(t("cw_cheats_find_your_version"));
    expect(html).not.toContain(t("cw_cheats_find_no_match", { v: "" }).slice(0, 10));
  });

  it("no_file_for_the_installed_version_warns", async () => {
    helper({ version: "02.00" });
    const { f } = finder();
    await f.open(GAME);
    expect(render(f.get())).toContain(t("cw_cheats_find_no_match", { v: "02.00" }));
  });

  it("nobody_published_any", async () => {
    helper();
    const { f } = finder({ index: { entries: [], failed: ["henmix"], offline: false } });
    await f.open(GAME);
    const html = render(f.get());
    expect(html).toContain(t("cw_cheats_find_none"));
    expect(html).toContain(t("cw_cheats_find_partial"));
  });

  it("github_unreachable_says_what_to_do", async () => {
    helper();
    const { f } = finder({ index: { entries: [], failed: ["etahen", "goldhen", "henmix"], offline: true } });
    await f.open(GAME);
    expect(f.get()).toEqual({ kind: "offline", title: GAME });
    const html = render(f.get());
    expect(t("cw_cheats_find_offline")).toBe(
      "This PS5 can't reach GitHub. Get cheats in the PS5Upload app on your PC, or allow raw.githubusercontent.com in your DNS settings.",
    );
    expect(html).toContain("raw.githubusercontent.com");
    expect(html).toContain(t("cw_cheats_find_retry"));
  });

  it("download_fetches_in_the_browser_and_stores_on_the_console", async () => {
    const stored = helper({ version: "01.03" });
    const e = INDEX.entries[2]; // henmix, nested under cheats/
    const urls = contentUrls(MIX, e.filename);
    const { f, stored: done } = finder({ fetch: github({ [urls[0]]: '{"name":"Game","mods":[]}' }) });
    await f.open(GAME);
    await f.download(e);
    expect(stored).toEqual([
      { route: "/api/cheats/store?format=json&name=CUSA15438_01.03.json", body: '{"name":"Game","mods":[]}' },
    ]);
    expect(done).toEqual([[e, GAME]]);
    const s = f.get();
    if (s.kind !== "list") throw new Error(s.kind);
    expect(s.busy).toBeNull();
    expect(s.done).toEqual([e.filename]);
    expect(s.message).toEqual({ tone: "info", text: t("cw_cheats_find_saved", { name: e.filename }) });
  });

  it("the_store_route_names_the_format_by_extension", () => {
    expect(storeRoute(entry("CUSA15438_01.02.shn"))).toBe("/api/cheats/store?format=shn&name=CUSA15438_01.02.shn");
    expect(storeRoute(entry("CUSA15438_01.02.MC4"))).toBe("/api/cheats/store?format=mc4&name=CUSA15438_01.02.MC4");
  });

  it("one_download_at_a_time", async () => {
    helper();
    let release: () => void = () => {};
    const gate = new Promise<void>((r) => (release = r));
    const fetchSlow: FetchLike = async () => {
      await gate;
      return new Response("x", { status: 200 });
    };
    const { f } = finder({ fetch: fetchSlow });
    await f.open(GAME);
    const a = f.download(INDEX.entries[0]);
    const s = f.get();
    expect(s.kind === "list" && s.busy).toBe(INDEX.entries[0].filename);
    expect(render(s)).toContain(t("cw_cheats_find_downloading"));
    await f.download(INDEX.entries[1]); // ignored
    release();
    await a;
    expect(mockRaw).toHaveBeenCalledTimes(1);
  });

  it("failures_are_one_line_and_keep_the_list", async () => {
    helper({ storeError: new ApiError(500, "io_error") });
    const e = INDEX.entries[0];
    const { f, stored } = finder({ fetch: github({ [contentUrls(CHEAT_REPOS[0], e.filename)[0]]: "{}" }) });
    await f.open(GAME);
    await f.download(e);
    const s = f.get();
    if (s.kind !== "list") throw new Error(s.kind);
    expect(s.message).toEqual({ tone: "error", text: t("cw_cheats_find_err_store", { name: e.filename, code: "io_error" }) });
    expect(s.entries).toHaveLength(3);
    expect(stored).toEqual([]);
    expect(render(s)).toContain('role="alert"');
  });

  it("download_errors_read_well", () => {
    expect(downloadErrorText(new RepoError(true, "unreachable"), "a.json")).toBe(t("cw_cheats_find_offline"));
    expect(downloadErrorText(new RepoError(false, "too_large"), "a.json")).toBe(t("cw_cheats_find_too_large", { name: "a.json" }));
    expect(downloadErrorText(new RepoError(false, "not_found"), "a.json")).toBe(
      t("cw_cheats_find_err_download", { name: "a.json", code: "not_found" }),
    );
    expect(downloadErrorText(new ApiError(401, "token"), "a.json")).toBeNull();
    expect(downloadErrorText(new ApiError(0, "offline"), "a.json")).toBe(t("cw_home_err_offline"));
    expect(downloadErrorText(new ApiError(413, "too_large"), "a.json")).toBe(t("cw_cheats_find_too_large", { name: "a.json" }));
  });

  it("github_down_mid_download_says_unreachable", async () => {
    helper();
    const { f } = finder({ fetch: github({}, true) });
    await f.open(GAME);
    await f.download(INDEX.entries[0]);
    const s = f.get();
    expect(s.kind === "list" && s.message?.text).toBe(t("cw_cheats_find_offline"));
    expect(mockRaw).not.toHaveBeenCalled();
  });

  it("closing_drops_a_late_answer", async () => {
    helper();
    let release: (i: RepoIndex) => void = () => {};
    const states: FinderState[] = [];
    const f = createFinder(
      (s) => states.push(s),
      () => {},
      { index: () => new Promise<RepoIndex>((r) => (release = r)) },
    );
    const p = f.open(GAME);
    expect(f.get()).toEqual({ kind: "loading", title: GAME });
    expect(render(f.get())).toContain(t("cw_cheats_find_loading"));
    f.close();
    release(INDEX);
    await p;
    expect(f.get()).toEqual(FINDER_CLOSED);
    expect(render(f.get())).toBe("");
  });

  it("an_index_read_that_throws_reads_as_offline", async () => {
    helper();
    const { f } = finder({ index: new Error("boom") });
    await f.open(GAME);
    expect(f.get().kind).toBe("offline");
  });

  it("a_file_already_there_says_replaced", async () => {
    helper({ replaced: true });
    const e = INDEX.entries[0];
    const { f, full } = finder({ fetch: github({ [contentUrls(CHEAT_REPOS[0], e.filename)[0]]: "{}" }) });
    await f.open(GAME);
    await f.download(e);
    const s = f.get();
    expect(s.kind === "list" && s.message?.text).toBe(t("cw_cheats_find_replaced", { name: e.filename }));
    expect(t("cw_cheats_find_replaced", { name: "x" })).toBe("x was replaced on the console. Its cheats are in the list now.");
    expect(full[0]).toMatchObject({ replaced: true, current: true, title: GAME });
  });

  it("a_download_finishing_after_the_finder_was_reopened_is_not_current", async () => {
    helper();
    let release: () => void = () => {};
    const gate = new Promise<void>((r) => (release = r));
    const slow: FetchLike = async () => {
      await gate;
      return new Response("x", { status: 200 });
    };
    const { f, full } = finder({ fetch: slow });
    await f.open(GAME);
    const p = f.download(INDEX.entries[0]);
    f.close();
    await f.open("CUSA99999");
    release();
    await p;
    expect(full).toHaveLength(1);
    expect(full[0]).toMatchObject({ title: GAME, current: false });
    expect(f.get()).toMatchObject({ kind: "list", title: "CUSA99999" });
  });
});
