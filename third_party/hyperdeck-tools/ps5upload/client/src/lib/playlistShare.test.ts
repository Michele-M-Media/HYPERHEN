import { describe, expect, it } from "vitest";
import type { PayloadInfo } from "../api/ps5";
import type { Playlist } from "./playlistOps";
import { buildPlaylistExport, importedPlaylists, parsePlaylistExport, repoHostOf } from "./playlistShare";

const info = (p: Partial<PayloadInfo>): PayloadInfo =>
  ({ id: "", display_name: "", role: "", description: "", repo_owner: "", repo_name: "", homepage: "", ...p }) as PayloadInfo;

const boot: Playlist = {
  id: "a1",
  name: "Boot",
  steps: [
    { path: "", payloadId: "kstuff-echostretch", payloadLabel: "kstuff", sleepMs: 3000 },
    { path: "", payloadId: "custom-0123abcd", payloadLabel: "my tool", sleepMs: 0 },
    { path: "/Users/me/x.elf", sleepMs: 0, port: 9021 },
  ],
  continueOnFailure: false,
  createdAt: 1,
  updatedAt: 2,
  lastRunAt: 3,
};

describe("playlist export", () => {
  it("carries the playlists, the auto-loader and every custom repo, not when they last ran", () => {
    const catalog = [
      info({ id: "kstuff-echostretch", homepage: "https://github.com/EchoStretch/kstuff-lite" }),
      info({
        id: "custom-0123abcd",
        is_custom: true,
        display_name: "my tool",
        repo_owner: "me",
        repo_name: "tool",
        homepage: "https://git.example.org/me/tool",
      }),
    ];
    const doc = buildPlaylistExport([boot], { enabled: true, playlistId: "a1", bringUpPlaylistId: null }, catalog, 99);
    expect(doc.customRepos).toEqual([
      { id: "custom-0123abcd", host: "git.example.org", owner: "me", name: "tool", displayName: "my tool" },
    ]);
    expect(doc.playlists[0]).not.toHaveProperty("lastRunAt");
    const back = parsePlaylistExport(JSON.stringify(doc));
    expect(back.playlists[0].steps).toEqual(boot.steps);
    expect(back.autoLoader).toEqual({ enabled: true, playlistId: "a1", bringUpPlaylistId: null });
    expect(back.customRepos[0].host).toBe("git.example.org");
  });

  it("refuses a file that is not an export and drops malformed entries", () => {
    expect(() => parsePlaylistExport("{}")).toThrow("not a ps5upload playlists file");
    expect(() => parsePlaylistExport("nope")).toThrow("not JSON");
    const doc = parsePlaylistExport(
      JSON.stringify({
        format: "ps5upload-playlists",
        playlists: [{ id: "x", name: "X", steps: [{ sleepMs: 5 }, { path: "/a.elf", sleepMs: -3 }] }, { name: "no id" }],
        customRepos: [{ owner: "o" }],
      }),
    );
    expect(doc.playlists).toHaveLength(1);
    expect(doc.playlists[0].steps).toEqual([{ path: "/a.elf", sleepMs: 0 }]);
    expect(doc.customRepos).toEqual([]);
  });

  it("imports alongside what is here: new ids, names that do not repeat", () => {
    let n = 0;
    const { playlists, idMap } = importedPlaylists([boot], [boot, { ...boot, id: "b2", name: "Other" }], () => `n${++n}`, 5);
    expect(playlists.map((p) => [p.id, p.name])).toEqual([
      ["n1", "Boot (2)"],
      ["n2", "Other"],
    ]);
    expect(idMap.get("a1")).toBe("n1");
    expect(repoHostOf({ homepage: "not a url" })).toBe("github.com");
  });
});
