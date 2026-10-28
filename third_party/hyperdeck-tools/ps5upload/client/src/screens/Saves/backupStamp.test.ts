import { describe, expect, it } from "vitest";
import { backupStamp } from "./backupStamp";

describe("backupStamp", () => {
  it("names a backup by its local date and minute, sortable", () => {
    expect(backupStamp(new Date(2026, 9, 4, 8, 5))).toBe("20261004-0805");
    expect(backupStamp(new Date(2026, 11, 31, 23, 59))).toBe("20261231-2359");
  });
});
