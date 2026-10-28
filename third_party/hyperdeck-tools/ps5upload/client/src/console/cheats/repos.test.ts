import { beforeEach, describe, expect, it } from "vitest";
import {
  CHEAT_FILE_MAX,
  CHEAT_REPOS,
  contentUrls,
  downloadCheat,
  entriesFor,
  fetchLimited,
  formatFromIndex,
  parseCheatFilename,
  parseIndexLine,
  rankEntries,
  readRepoIndex,
  RepoError,
  repoIndex,
  resetRepoIndex,
  sameVersion,
  storableName,
  storeFormat,
  treeEntryFromPath,
  type CheatEntry,
  type FetchLike,
} from "./repos";

/** A fake network: url → body (string), status number, or "down" (the request throws). */
function net(routes: Record<string, string | number | "down" | ArrayBuffer>) {
  const calls: string[] = [];
  const f: FetchLike = async (url) => {
    calls.push(url);
    const r = routes[url];
    if (r === undefined || r === "down") throw new TypeError("Load failed");
    if (typeof r === "number") return new Response("nope", { status: r });
    return new Response(r, { status: 200 });
  };
  return { f, calls };
}

const ETA = CHEAT_REPOS[0];
const GOLD = CHEAT_REPOS[1];
const MIX = CHEAT_REPOS[2];

describe("cheat file names (cheats.rs parse_cheat_filename)", () => {
  it("reads the title and version from a published name", () => {
    expect(parseCheatFilename("CUSA15438_01.00.json")).toEqual(["CUSA15438", "01.00"]);
    expect(parseCheatFilename("CUSA09193_01.05_2.json")).toEqual(["CUSA09193", "01.05"]);
    expect(parseCheatFilename("PPSA01650_01.000.016.shn")).toEqual(["PPSA01650", "01.000.016"]);
    expect(parseCheatFilename("cusa34394_05.01.mc4")).toEqual(["CUSA34394", "05.01"]);
  });
  it("keeps the title when the version is missing or not a version", () => {
    expect(parseCheatFilename("CUSA15438.json")).toEqual(["CUSA15438", ""]);
    expect(parseCheatFilename("CUSA15438_2.json")).toEqual(["CUSA15438", ""]);
    expect(parseCheatFilename("CUSA15438_v1.00.json")).toEqual(["CUSA15438", ""]);
    expect(parseCheatFilename("CUSA15438_c20ae0e8.json")).toEqual(["CUSA15438", ""]);
  });
  it("gives nothing for a name that breaks the convention", () => {
    expect(parseCheatFilename("README.md")).toEqual(["", ""]);
    expect(parseCheatFilename("CUSA1543_01.00.json")).toEqual(["", ""]);
    expect(parseCheatFilename("12345CUSA_01.00.json")).toEqual(["", ""]);
    expect(parseCheatFilename("")).toEqual(["", ""]);
  });
});

describe("index files (cheats.rs parse_index_line / format_from_index)", () => {
  it("splits filename=title and skips blanks and comments", () => {
    expect(parseIndexLine("CUSA15438_01.00.json=Game name")).toEqual(["CUSA15438_01.00.json", "Game name"]);
    expect(parseIndexLine("  CUSA1_1.0.shn = A = B  ")).toEqual(["CUSA1_1.0.shn", "A = B"]);
    expect(parseIndexLine("x.json=")).toEqual(["x.json", ""]);
    expect(parseIndexLine("")).toBeNull();
    expect(parseIndexLine("   ")).toBeNull();
    expect(parseIndexLine("# comment=x")).toBeNull();
    expect(parseIndexLine("no equals")).toBeNull();
    expect(parseIndexLine("=title only")).toBeNull();
  });
  it("names the format by the index file", () => {
    expect(formatFromIndex("json.txt")).toBe("json");
    expect(formatFromIndex("shn.txt")).toBe("shn");
    expect(formatFromIndex("mc4.txt")).toBe("mc4");
    expect(formatFromIndex("other.txt")).toBe("json");
  });
});

describe("git tree paths (cheats.rs tree_entry_from_path)", () => {
  it("accepts <prefix><format>/<file> with a matching extension", () => {
    expect(treeEntryFromPath("cheats/json/CUSA00002_01.00_c20ae0e8.json", "cheats/")).toEqual(["CUSA00002_01.00_c20ae0e8.json", "json"]);
    expect(treeEntryFromPath("cheats/shn/CUSA123.shn", "cheats/")).toEqual(["CUSA123.shn", "shn"]);
    expect(treeEntryFromPath("cheats/mc4/CUSA123.MC4", "cheats/")).toEqual(["CUSA123.MC4", "mc4"]);
    expect(treeEntryFromPath("json/CUSA1.json", "")).toEqual(["CUSA1.json", "json"]);
  });
  it("refuses anything else", () => {
    expect(treeEntryFromPath("docs/json/a.json", "cheats/")).toBeNull();
    expect(treeEntryFromPath("cheats/misc/a.json", "cheats/")).toBeNull();
    expect(treeEntryFromPath("cheats/json/sub/a.json", "cheats/")).toBeNull();
    expect(treeEntryFromPath("cheats/json/README.md", "cheats/")).toBeNull();
    expect(treeEntryFromPath("cheats/json/noext", "cheats/")).toBeNull();
    expect(treeEntryFromPath("cheats/json/", "cheats/")).toBeNull();
    expect(treeEntryFromPath("cheats/json", "cheats/")).toBeNull();
  });
});

describe("content URLs (cheats.rs cheats_repo_download)", () => {
  it("tries json, shn, mc4, misc under the content base, in that order", () => {
    expect(contentUrls(MIX, "CUSA1_1.00.shn")).toEqual([
      "https://raw.githubusercontent.com/TeeKay87/HEN-Cheats-Collection/master/cheats/json/CUSA1_1.00.shn",
      "https://raw.githubusercontent.com/TeeKay87/HEN-Cheats-Collection/master/cheats/shn/CUSA1_1.00.shn",
      "https://raw.githubusercontent.com/TeeKay87/HEN-Cheats-Collection/master/cheats/mc4/CUSA1_1.00.shn",
      "https://raw.githubusercontent.com/TeeKay87/HEN-Cheats-Collection/master/cheats/misc/CUSA1_1.00.shn",
    ]);
    expect(contentUrls(ETA, "a.json")[0]).toBe("https://raw.githubusercontent.com/etaHEN/PS5_Cheats/main/json/a.json");
  });
  it("matches the engine's repo list", () => {
    expect(CHEAT_REPOS.map((r) => r.id)).toEqual(["etahen", "goldhen", "henmix"]);
    for (const r of CHEAT_REPOS) {
      expect(r.indexFiles.length > 0 || r.treeApi !== "").toBe(true);
      expect(r.contentBase.endsWith("/")).toBe(true);
      for (const u of [r.rawBase, r.contentBase, r.treeApi].filter(Boolean)) {
        expect(u.startsWith("https://raw.githubusercontent.com/") || u.startsWith("https://api.github.com/")).toBe(true);
      }
    }
  });
  it("stores by extension, only names the helper accepts", () => {
    expect(storeFormat("a.SHN")).toBe("shn");
    expect(storeFormat("a.mc4")).toBe("mc4");
    expect(storeFormat("a.json")).toBe("json");
    expect(storableName("CUSA15438_01.00.json")).toBe(true);
    expect(storableName("CUSA15438_01.00 (1).json")).toBe(false);
    expect(storableName("../a.json")).toBe(false);
    expect(storableName(".json")).toBe(false);
    expect(storableName("a..json")).toBe(false);
    expect(storableName("a.txt")).toBe(false);
    expect(storableName(`${"a".repeat(123)}.json`)).toBe(true);
    expect(storableName(`${"a".repeat(124)}.json`)).toBe(false);
  });
});

describe("versions", () => {
  it("compares by number", () => {
    expect(sameVersion("01.000.016", "1.0.16")).toBe(true);
    expect(sameVersion("01.05", "1.5")).toBe(true);
    expect(sameVersion("01.05", "01.10")).toBe(false);
    expect(sameVersion("", "01.00")).toBe(false);
    expect(sameVersion("01.00", null)).toBe(false);
  });
  it("ranks the installed version first, then newest", () => {
    const e = (filename: string): CheatEntry => {
      const [titleId, gameVersion] = parseCheatFilename(filename);
      return { filename, gameTitle: "", format: "json", repoId: "etahen", titleId, gameVersion };
    };
    const list = [e("CUSA00001_01.00.json"), e("CUSA00001_01.10.json"), e("CUSA00001_01.05.json"), e("CUSA00001.json"), e("CUSA00001_01.05_2.json")];
    expect(rankEntries(list, "01.05").map((x) => x.filename)).toEqual([
      "CUSA00001_01.05.json",
      "CUSA00001_01.05_2.json",
      "CUSA00001_01.10.json",
      "CUSA00001_01.00.json",
      "CUSA00001.json",
    ]);
    expect(rankEntries(list, null).map((x) => x.filename)).toEqual([
      "CUSA00001_01.10.json",
      "CUSA00001_01.05.json",
      "CUSA00001_01.05_2.json",
      "CUSA00001_01.00.json",
      "CUSA00001.json",
    ]);
  });
});

const tree = (paths: string[]) => JSON.stringify({ tree: paths.map((path) => ({ type: "blob", path })).concat([{ type: "tree", path: "cheats/json" } as never]) });

describe("reading the repos", () => {
  beforeEach(() => resetRepoIndex());

  it("lists every repo, first repo wins a file name", async () => {
    const { f } = net({
      [`${ETA.rawBase}json.txt`]: "CUSA15438_01.00.json=Game A\nCUSA15438_01.02.json=Game A\n",
      [`${ETA.rawBase}shn.txt`]: "# none\n",
      [`${ETA.rawBase}mc4.txt`]: 404,
      [`${GOLD.rawBase}json.txt`]: "CUSA15438_01.00.json=Game A (gold)\r\nCUSA99999_01.00.json=Other\r\n",
      [`${GOLD.rawBase}shn.txt`]: "CUSA15438_01.02.shn=Game A\n",
      [`${GOLD.rawBase}mc4.txt`]: "CUSA15438_01.02.mc4=Game A\n",
      [MIX.treeApi]: tree(["cheats/json/CUSA15438_01.03.json", "cheats/README.md", "cheats/json/CUSA15438_01.00.json"]),
    });
    const idx = await readRepoIndex(f);
    expect(idx.offline).toBe(false);
    expect(idx.failed).toEqual([]);
    const mine = entriesFor(idx, "cusa15438");
    expect(mine.map((e) => `${e.repoId}:${e.filename}:${e.format}`).sort()).toEqual(
      [
        "etahen:CUSA15438_01.00.json:json",
        "etahen:CUSA15438_01.02.json:json",
        "goldhen:CUSA15438_01.02.shn:shn",
        "goldhen:CUSA15438_01.02.mc4:mc4",
        "henmix:CUSA15438_01.03.json:json",
      ].sort(),
    );
    expect(mine.find((e) => e.filename === "CUSA15438_01.00.json")?.gameTitle).toBe("Game A");
    expect(entriesFor(idx, "CUSA99999")).toHaveLength(1);
  });

  it("a rate-limited tree is that source unavailable, not offline", async () => {
    const { f } = net({
      [`${ETA.rawBase}json.txt`]: "CUSA1_01.00.json=A\n",
      [`${ETA.rawBase}shn.txt`]: 404,
      [`${ETA.rawBase}mc4.txt`]: 404,
      [`${GOLD.rawBase}json.txt`]: "down",
      [`${GOLD.rawBase}shn.txt`]: "down",
      [`${GOLD.rawBase}mc4.txt`]: "down",
      [MIX.treeApi]: 403,
    });
    const idx = await readRepoIndex(f);
    expect(idx.offline).toBe(false);
    expect(idx.failed.sort()).toEqual(["goldhen", "henmix"]);
    expect(idx.entries.map((e) => e.filename)).toEqual(["CUSA1_01.00.json"]);
  });

  it("nothing answering is offline (no internet, or GitHub blocked by DNS)", async () => {
    const { f } = net({});
    const idx = await readRepoIndex(f);
    expect(idx.offline).toBe(true);
    expect(idx.failed).toEqual(["etahen", "goldhen", "henmix"]);
  });

  it("a garbled tree reply is that source failing", async () => {
    const { f } = net({ [`${ETA.rawBase}json.txt`]: "CUSA1_01.00.json=A\n", [MIX.treeApi]: "<html>" });
    const idx = await readRepoIndex(f);
    expect(idx.failed).toContain("henmix");
    expect(idx.offline).toBe(false);
  });

  it("is read once per session, again after a failed source", async () => {
    const ok = net({
      [`${ETA.rawBase}json.txt`]: "CUSA1_01.00.json=A\n",
      [`${ETA.rawBase}shn.txt`]: "",
      [`${ETA.rawBase}mc4.txt`]: "",
      [`${GOLD.rawBase}json.txt`]: "",
      [`${GOLD.rawBase}shn.txt`]: "",
      [`${GOLD.rawBase}mc4.txt`]: "",
      [MIX.treeApi]: tree([]),
    });
    await repoIndex(ok.f);
    const n = ok.calls.length;
    await repoIndex(ok.f);
    expect(ok.calls.length).toBe(n);

    resetRepoIndex();
    const bad = net({ [`${ETA.rawBase}json.txt`]: "CUSA1_01.00.json=A\n" });
    await repoIndex(bad.f);
    await new Promise((r) => setTimeout(r, 0));
    const m = bad.calls.length;
    await repoIndex(bad.f);
    expect(bad.calls.length).toBe(2 * m);
  });
});

describe("downloading a file", () => {
  const entry: CheatEntry = {
    filename: "CUSA1_01.00.shn",
    gameTitle: "",
    format: "shn",
    repoId: "henmix",
    titleId: "CUSA1",
    gameVersion: "01.00",
  };
  it("takes the first content URL that answers", async () => {
    const urls = contentUrls(MIX, entry.filename);
    const { f, calls } = net({ [urls[0]]: 404, [urls[1]]: "<Trainer/>" });
    const buf = await downloadCheat(entry, f);
    expect(new TextDecoder().decode(buf)).toBe("<Trainer/>");
    expect(calls).toEqual(urls.slice(0, 2));
  });
  it("says not found when every URL answered an error", async () => {
    const urls = contentUrls(MIX, entry.filename);
    const { f } = net(Object.fromEntries(urls.map((u) => [u, 404])));
    await expect(downloadCheat(entry, f)).rejects.toMatchObject({ network: false, message: "not_found" });
  });
  it("says unreachable when nothing answered", async () => {
    const { f } = net({});
    const err = await downloadCheat(entry, f).catch((e: unknown) => e);
    expect(err).toBeInstanceOf(RepoError);
    expect((err as RepoError).network).toBe(true);
  });
  it("refuses a file over the helper's limit", async () => {
    const urls = contentUrls(MIX, entry.filename);
    const { f } = net({ [urls[0]]: new ArrayBuffer(CHEAT_FILE_MAX + 1) });
    await expect(downloadCheat(entry, f)).rejects.toMatchObject({ message: "too_large" });
  });
  it("never_fetches_a_name_the_helper_would_refuse", async () => {
    const { f, calls } = net({});
    await expect(downloadCheat({ ...entry, filename: "CUSA1 copy.shn" }, f)).rejects.toMatchObject({ message: "bad_name" });
    expect(calls).toEqual([]);
  });
  it("a_stalled_body_times_out_instead_of_hanging", async () => {
    const urls = contentUrls(MIX, entry.filename);
    const f: FetchLike = async (url) =>
      url === urls[0]
        ? new Response(new ReadableStream({ start: (c) => c.enqueue(new Uint8Array([1, 2])) }), { status: 200 })
        : new Response("404", { status: 404 });
    const err = await downloadCheat(entry, f, 50).catch((e: unknown) => e);
    expect(err).toMatchObject({ network: true, message: "cut_off" });
  });
});

describe("bounded reads (fetchLimited)", () => {
  const chunked = (sizes: number[]) => {
    let pulled = 0;
    const body = new ReadableStream<Uint8Array>({
      pull(c) {
        if (pulled >= sizes.length) return c.close();
        c.enqueue(new Uint8Array(sizes[pulled++]));
      },
    });
    return { body, pulled: () => pulled };
  };
  it("stops reading past the limit", async () => {
    const big = chunked([600, 600, 600, 600, 600]);
    const f: FetchLike = async () => new Response(big.body, { status: 200 });
    await expect(fetchLimited(f, "u", 1000, 1000)).rejects.toMatchObject({ message: "too_large", network: false });
    expect(big.pulled()).toBeLessThan(5);
  });
  it("reads a body in chunks", async () => {
    const f: FetchLike = async () => new Response(chunked([3, 4]).body, { status: 200 });
    const r = await fetchLimited(f, "u", 1000, 7);
    expect(r.ok && r.body.byteLength).toBe(7);
  });
  it("refuses a declared length over the limit without reading", async () => {
    const f: FetchLike = async () => new Response("x".repeat(10), { status: 200, headers: { "Content-Length": "5000" } });
    await expect(fetchLimited(f, "u", 1000, 100)).rejects.toMatchObject({ message: "too_large" });
  });
  it("falls back to arrayBuffer without a body stream", async () => {
    const res = { ok: true, status: 200, headers: new Headers(), body: null, arrayBuffer: async () => new ArrayBuffer(9) } as unknown as Response;
    const r = await fetchLimited(async () => res, "u", 1000, 10);
    expect(r.ok && r.body.byteLength).toBe(9);
    const big = { ...res, arrayBuffer: async () => new ArrayBuffer(11) } as unknown as Response;
    await expect(fetchLimited(async () => big, "u", 1000, 10)).rejects.toMatchObject({ message: "too_large" });
  });
  it("an_http_error_is_not_ok", async () => {
    const r = await fetchLimited(async () => new Response("no", { status: 403 }), "u", 1000, 10);
    expect(r).toEqual({ ok: false, status: 403 });
  });
  it("a_reply_that_never_comes_times_out", async () => {
    const r = fetchLimited(() => new Promise<Response>(() => {}), "u", 30, 10);
    await expect(r).rejects.toMatchObject({ network: true });
  });
});
