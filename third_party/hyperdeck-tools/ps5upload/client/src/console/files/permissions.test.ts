import { describe, expect, it } from "vitest";
import { hasPerm, keepsPermissions, modeLetters, modeText, parseMode, permMask, togglePerm } from "./permissions";

describe("permissions model", () => {
  it("maps owner/group/other × read/write/run to mode bits", () => {
    expect(permMask("owner", "read")).toBe(0o400);
    expect(permMask("group", "write")).toBe(0o020);
    expect(permMask("other", "exec")).toBe(0o001);
    expect(hasPerm(0o754, "group", "exec")).toBe(true);
    expect(hasPerm(0o754, "other", "write")).toBe(false);
  });
  it("toggles one bit and never keeps setuid/sticky bits", () => {
    expect(togglePerm(0o755, "group", "write")).toBe(0o775);
    expect(togglePerm(0o755, "owner", "read")).toBe(0o355);
    expect(togglePerm(0o4755, "other", "exec")).toBe(0o754);
  });
  it("shows octal and letters", () => {
    expect(modeText(0o755)).toBe("0755");
    expect(modeText(0o100644)).toBe("0644");
    expect(modeLetters(0o750)).toBe("rwxr-x---");
  });
  it("parses typed octal", () => {
    expect(parseMode("755")).toBe(0o755);
    expect(parseMode("0644")).toBe(0o644);
    expect(parseMode("7777")).toBeNull();
    expect(parseMode("89")).toBeNull();
    expect(parseMode("")).toBeNull();
  });
  it("knows exFAT and FAT keep no permissions", () => {
    expect(keepsPermissions("exfat")).toBe(false);
    expect(keepsPermissions("msdosfs")).toBe(false);
    expect(keepsPermissions("ufs")).toBe(true);
    expect(keepsPermissions("pfs")).toBe(true);
    expect(keepsPermissions(undefined)).toBe(true);
  });
});
