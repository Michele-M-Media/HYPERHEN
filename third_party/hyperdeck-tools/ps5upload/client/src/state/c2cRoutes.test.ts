import { describe, expect, it } from "vitest";

import { pairKey, parseRoutes } from "./c2cRoutes";

describe("console-to-console route preferences", () => {
  it("keeps a stored mode and address per pair, and defaults junk to auto", () => {
    const got = parseRoutes(
      JSON.stringify({
        "10.0.0.2>10.0.0.3": { mode: "direct", directAddr: " 127.0.0.1:19120 " },
        "10.0.0.3>10.0.0.2": { mode: "sideways", directAddr: 7 },
        bad: null,
      }),
    );
    expect(got[pairKey("10.0.0.2", "10.0.0.3")]).toEqual({ mode: "direct", directAddr: "127.0.0.1:19120" });
    expect(got[pairKey("10.0.0.3", "10.0.0.2")]).toEqual({ mode: "auto", directAddr: "" });
    expect(got).not.toHaveProperty("bad");
  });

  it("survives nothing stored or junk", () => {
    expect(parseRoutes(null)).toEqual({});
    expect(parseRoutes("{nope")).toEqual({});
    expect(parseRoutes("[]")).toEqual({});
  });
});
