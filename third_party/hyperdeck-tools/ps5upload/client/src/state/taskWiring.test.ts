import { describe, expect, it } from "vitest";
import { speedAndEta } from "./taskWiring";

describe("speedAndEta (#444)", () => {
  it("gives a running transfer's task its speed and time left", () => {
    expect(speedAndEta(25, 125, 10)).toEqual({ rate: { bytesPerSec: 10 }, eta: 10 });
    expect(speedAndEta(5, 0, 10)).toEqual({ rate: { bytesPerSec: 10 } });
    expect(speedAndEta(5, 100, 0)).toEqual({});
  });
});
