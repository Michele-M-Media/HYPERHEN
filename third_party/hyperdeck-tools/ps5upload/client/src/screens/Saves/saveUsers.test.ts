import { describe, expect, it } from "vitest";

import type { ProfileUser, SaveEntry } from "../../api/ps5";
import {
  backupZipName,
  defaultUserFilter,
  filterSaves,
  latestUsbBackup,
  saveOwners,
  safeName,
  usbUserDir,
  userHex,
  userName,
} from "./saveUsers";

const users: ProfileUser[] = [
  { uid: 0x150c71a1, uid_hex: "0x150C71A1", username: "Alice" },
  { uid: 0x8000_0001 | 0, uid_hex: "0x80000001", username: "Bob" },
];
const save = (user_id: number, title_id: string): SaveEntry => ({
  title_id,
  user_id,
  path: `/user/home/${userHex(user_id)}/savedata_prospero/${title_id}`,
  size: 1,
  mtime: 1,
  kind: "ps5",
});
const saves = [save(0x150c71a1, "PPSA1"), save(0x80000001 | 0, "PPSA1"), save(0x150c71a1, "PPSA2"), save(0x22, "PPSA3")];

describe("save owners", () => {
  it("matches ids by their hex, signed or not", () => {
    expect(userHex(0x80000001 | 0)).toBe("80000001");
    expect(userName(0x80000001 | 0, users)).toBe("Bob");
    expect(userName(0x22, users)).toBeNull();
    expect(saveOwners(saves, users)).toEqual([
      { id: 0x150c71a1, name: "Alice" },
      { id: 0x80000001 | 0, name: "Bob" },
      { id: 0x22, name: null },
    ]);
  });

  it("starts on the user at the console when they have saves, else on everyone", () => {
    expect(defaultUserFilter(saves, 0x150c71a1)).toBe("150c71a1");
    expect(defaultUserFilter(saves, 0)).toBe("all");
    expect(defaultUserFilter(saves, 0x999)).toBe("all");
    expect(filterSaves(saves, "150c71a1").map((s) => s.title_id)).toEqual(["PPSA1", "PPSA2"]);
    expect(filterSaves(saves, "all")).toHaveLength(4);
  });
});

describe("backup names", () => {
  it("labels folders and zips with a file-safe name", () => {
    expect(safeName(' A/b:c* "d" ')).toBe("A_b_c_ _d_");
    expect(usbUserDir(0x150c71a1, "Alice")).toBe("Alice-150c71a1");
    expect(usbUserDir(0x22, null)).toBe("00000022");
    expect(backupZipName("PPSA1", "Alice")).toBe("PPSA1-Alice.zip");
    expect(backupZipName("PPSA1", null, "20261018-1200")).toBe("PPSA1-20261018-1200.zip");
    expect(backupZipName("PPSA1", "Alice", undefined, "Marvel's Wolverine™")).toBe("PPSA1-Marvel's Wolverine-Alice.zip");
    expect(backupZipName("PPSA1", "Alice", "20261018-1200", 'A: "B"/C')).toBe("PPSA1-A_ _B__C-Alice-20261018-1200.zip");
  });
});

describe("latestUsbBackup", () => {
  const tree: Record<string, string[]> = {
    "": ["2026-01-01_120000", "2026-02-01_120000", "Alice-150c71a1", "Bob-80000001"],
    "Alice-150c71a1": ["2026-03-01_120000", "2026-04-01_090000", "notes"],
    "Bob-80000001": [],
  };
  const list = async (rel: string) => tree[rel] ?? [];

  it("takes the user's own newest backup, found by id even after a rename", async () => {
    expect(await latestUsbBackup(list, 0x150c71a1, false)).toBe("Alice-150c71a1/2026-04-01_090000");
  });

  it("uses an old shared backup only when the user is the title's only user", async () => {
    expect(await latestUsbBackup(list, 0x80000001 | 0, false)).toBeNull();
    expect(await latestUsbBackup(list, 0x80000001 | 0, true)).toBe("2026-02-01_120000");
    expect(await latestUsbBackup(async () => [], 0x22, true)).toBeNull();
  });
});
