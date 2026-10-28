import { describe, expect, it } from "vitest";

import { rarPasswordProblem } from "./rarPassword";

describe("rar password failures", () => {
  it("tells a missing password from a wrong one, by reason or message", () => {
    expect(rarPasswordProblem("ava1_rar_password_required")).toBe("required");
    expect(rarPasswordProblem("rar_password_required")).toBe("required");
    expect(rarPasswordProblem("password_needed")).toBe("required");
    expect(rarPasswordProblem(null, "open rar: rar_password_wrong")).toBe("wrong");
    expect(rarPasswordProblem("ava1_rar_password_wrong")).toBe("wrong");
  });
  it("is null for any other failure", () => {
    expect(rarPasswordProblem("ava1_rar_corrupt")).toBeNull();
    expect(rarPasswordProblem(null, "connection reset")).toBeNull();
    expect(rarPasswordProblem(undefined, undefined)).toBeNull();
  });
});
