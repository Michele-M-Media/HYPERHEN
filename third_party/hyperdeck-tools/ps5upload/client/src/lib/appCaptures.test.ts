import { describe, expect, it } from "vitest";

import { captureDate, withoutNames, stepIndex } from "./appCaptures";

describe("captureDate", () => {
  it("reads the local time out of a capture's file name", () => {
    const d = captureDate("screenshot-20261010-143605-123.png");
    expect(d).not.toBeNull();
    expect([d!.getFullYear(), d!.getMonth(), d!.getDate(), d!.getHours(), d!.getMinutes(), d!.getSeconds()]).toEqual([
      2026, 9, 10, 14, 36, 5,
    ]);
  });
  it("also reads the older name without milliseconds", () => {
    expect(captureDate("screenshot-20260605-120000.png")?.getHours()).toBe(12);
  });
  it("gives null for any other name", () => {
    expect(captureDate("pasted-1.png")).toBeNull();
  });
});

describe("withoutNames", () => {
  it("drops exactly the named captures", () => {
    const list = [{ name: "a" }, { name: "b" }, { name: "c" }];
    expect(withoutNames(list, ["b", "x"]).map((s) => s.name)).toEqual(["a", "c"]);
  });
});

describe("stepIndex", () => {
  it("wraps at both ends", () => {
    expect(stepIndex(0, -1, 3)).toBe(2);
    expect(stepIndex(2, 1, 3)).toBe(0);
    expect(stepIndex(1, 1, 3)).toBe(2);
  });
  it("is 0 for an empty list", () => {
    expect(stepIndex(0, 1, 0)).toBe(0);
  });
});

describe("after a delete", () => {
  it("keeps the viewer on the same position, or the new last one", async () => {
    const { indexAfterDelete } = await import("./appCaptures");
    expect(indexAfterDelete(1, 4)).toBe(1);
    expect(indexAfterDelete(3, 3)).toBe(2);
    expect(indexAfterDelete(0, 0)).toBe(0);
  });
});
