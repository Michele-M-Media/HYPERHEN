import { describe, expect, it, vi } from "vitest";
import { ApiError } from "../api";
import { numberText, planRenames, renamed, replaceAll, runRenames, splitExt, type RenameItem, type RenameMode } from "./bulkRename";

const f = (name: string): RenameItem => ({ name, dir: false });
const d = (name: string): RenameItem => ({ name, dir: true });
const tos = (items: RenameItem[], mode: RenameMode, existing = items.map((i) => i.name)) =>
  planRenames(items, existing, mode).rows.map((r) => r.to);

describe("bulk rename planner", () => {
  it("splits the extension: last dot, never a dot-file's or a folder's", () => {
    expect(splitExt("a.b.png", false)).toEqual(["a.b", ".png"]);
    expect(splitExt(".hidden", false)).toEqual([".hidden", ""]);
    expect(splitExt("README", false)).toEqual(["README", ""]);
    expect(splitExt("v1.2", true)).toEqual(["v1.2", ""]);
  });

  it("find and replace: literal text, every occurrence, case toggle", () => {
    expect(replaceAll("a.a.a", ".", "_", true)).toBe("a_a_a");
    expect(replaceAll("Photo photo PHOTO", "photo", "img", true)).toBe("Photo img PHOTO");
    expect(replaceAll("Photo photo PHOTO", "photo", "img", false)).toBe("img img img");
    // a pattern character and "$&" in the replacement stay literal
    expect(replaceAll("a(1)*.txt", "(1)*", "$&x", false)).toBe("a$&x.txt");
    expect(replaceAll("abc", "", "x", true)).toBe("abc");
    const m: RenameMode = { kind: "replace", find: "IMG_", replace: "Trip ", caseSensitive: true };
    expect(tos([f("IMG_1.jpg"), f("IMG_2.jpg")], m)).toEqual(["Trip 1.jpg", "Trip 2.jpg"]);
  });

  it("prefix goes first; suffix goes before the extension (a folder's dots stay)", () => {
    expect(tos([f("a.png"), d("b.v2")], { kind: "prefix", text: "old_" })).toEqual(["old_a.png", "old_b.v2"]);
    expect(tos([f("a.png"), d("b.v2"), f(".cfg"), f("x")], { kind: "suffix", text: "-bak" })).toEqual([
      "a-bak.png",
      "b.v2-bak",
      ".cfg-bak",
      "x-bak",
    ]);
  });

  it("numbering: name + 01…, a {n} template, same width, the extension kept", () => {
    expect(numberText(0, 3, 1)).toBe("01");
    expect(numberText(9, 120, 1)).toBe("010");
    expect(numberText(0, 2, 99)).toBe("099");
    expect(tos([f("a.png"), f("b.png"), d("c")], { kind: "number", template: "", start: 1 })).toEqual(["a 01.png", "b 02.png", "c 03"]);
    expect(tos([f("x.jpg"), f("y.jpg")], { kind: "number", template: "Trip {n} ({n})", start: 7 })).toEqual([
      "Trip 07 (07).jpg",
      "Trip 08 (08).jpg",
    ]);
    expect(renamed(f("q.txt"), 0, 1, { kind: "number", template: "Save", start: 1 })).toBe("Save 01.txt");
  });

  it("an ok plan has no problem rows", () => {
    const p = planRenames([f("a.txt"), f("b.txt")], ["a.txt", "b.txt", "c"], { kind: "prefix", text: "x" });
    expect(p.ok).toBe(true);
    expect(p.first).toBeNull();
  });

  it("refuses a name already in the folder (without case), unless it is the item itself", () => {
    const p = planRenames([f("a.txt")], ["a.txt", "B.txt"], { kind: "replace", find: "a", replace: "b", caseSensitive: true });
    expect(p.ok).toBe(false);
    expect(p.first).toMatchObject({ from: "a.txt", to: "b.txt", problem: "exists" });
    // a case-only change of the item itself is fine
    const q = planRenames([f("a.txt")], ["a.txt"], { kind: "replace", find: "a", replace: "A", caseSensitive: true });
    expect(q.ok).toBe(true);
    // another selected item's current name counts (the renames run one at a time)
    const r = planRenames([f("x.txt"), f("x 01.txt")], ["x.txt", "x 01.txt"], { kind: "number", template: "x", start: 1 });
    expect(r.rows.map((x) => x.problem)).toEqual(["exists", null]);
  });

  it("refuses two items getting one name", () => {
    const p = planRenames([f("a1.txt"), f("A1.txt")], ["a1.txt", "A1.txt"], { kind: "replace", find: "1", replace: "", caseSensitive: true });
    expect(p.rows.map((x) => x.problem)).toEqual(["twice", "twice"]);
    expect(p.first?.to).toBe("a.txt");
  });

  it("refuses empty, '/', '.', '..', too long and unchanged names", () => {
    const one = (name: string, mode: RenameMode) => planRenames([f(name)], [name], mode).first?.problem;
    expect(one("abc", { kind: "replace", find: "abc", replace: "", caseSensitive: true })).toBe("empty");
    expect(one("a_b", { kind: "replace", find: "_", replace: "/", caseSensitive: true })).toBe("slash");
    expect(one("x", { kind: "replace", find: "x", replace: "..", caseSensitive: true })).toBe("dots");
    expect(one("x", { kind: "prefix", text: "é".repeat(128) })).toBe("long");
    expect(one("x", { kind: "prefix", text: "a".repeat(254) })).toBeUndefined();
    expect(one("abc", { kind: "replace", find: "zz", replace: "y", caseSensitive: true })).toBe("unchanged");
    // one unchanged row refuses the whole plan
    const p = planRenames([f("ab"), f("cd")], ["ab", "cd"], { kind: "replace", find: "a", replace: "x", caseSensitive: true });
    expect(p.ok).toBe(false);
    expect(p.first).toMatchObject({ from: "cd", problem: "unchanged" });
  });

  it("an empty selection is never ok", () => {
    expect(planRenames([], [], { kind: "prefix", text: "x" }).ok).toBe(false);
  });
});

describe("runRenames", () => {
  const rows = [
    { from: "a", to: "x", problem: null },
    { from: "b", to: "y", problem: null },
    { from: "c", to: "z", problem: null },
  ];

  it("renames one at a time, in order", async () => {
    const seen: string[] = [];
    const progress: number[] = [];
    const r = await runRenames("/data/d", rows, { onProgress: (at) => progress.push(at) }, async (dir, from, to) => {
      seen.push(`${dir}:${from}>${to}`);
    });
    expect(seen).toEqual(["/data/d:a>x", "/data/d:b>y", "/data/d:c>z"]);
    expect(progress).toEqual([1, 2, 3]);
    expect(r.done).toEqual([
      ["a", "x"],
      ["b", "y"],
      ["c", "z"],
    ]);
    expect(r.failedRow).toBeNull();
  });

  it("stops at the first failure; the rest are not sent", async () => {
    const rename = vi.fn(async (_d: string, from: string) => {
      if (from === "b") throw new ApiError(409, "exists");
    });
    const r = await runRenames("/d", rows, {}, rename);
    expect(rename).toHaveBeenCalledTimes(2);
    expect(r.done).toEqual([["a", "x"]]);
    expect(r.failedRow?.from).toBe("b");
    expect(r.skipped).toEqual(["c"]);
    expect(r.failed[0].error).toBeInstanceOf(ApiError);
  });

  it("Stop ends it before the next rename", async () => {
    let stop = false;
    const rename = vi.fn(async () => {
      stop = true;
    });
    const r = await runRenames("/d", rows, { stopped: () => stop }, rename);
    expect(rename).toHaveBeenCalledTimes(1);
    expect(r.skipped).toEqual(["b", "c"]);
    expect(r.failedRow).toBeNull();
  });
});
