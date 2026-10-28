import { describe, expect, it } from "vitest";
import { firmwareCheck, installedCheck, spaceCheck, versionCompare } from "./packageChecks";

describe("package checks", () => {
  it("firmware: bad when the console is older than the package needs", () => {
    expect(firmwareCheck("12.00", "5.10").verdict).toBe("bad");
    expect(firmwareCheck("5.10", "9.60").verdict).toBe("ok");
    expect(firmwareCheck("5.10", "5.10").verdict).toBe("ok");
    expect(firmwareCheck(null, "9.60").verdict).toBe("unknown");
    expect(firmwareCheck("5.10", null).verdict).toBe("unknown");
  });
  it("versions compare numerically across 2- and 3-part forms", () => {
    expect(versionCompare("01.04", "01.02")).toBeGreaterThan(0);
    expect(versionCompare("01.002.000", "01.002")).toBe(0);
    expect(versionCompare("01.10", "01.9")).toBeGreaterThan(0);
  });
  it("installed: newer / older / same / not installed", () => {
    expect(installedCheck("01.04", null).relation).toBe("not-installed");
    expect(installedCheck("01.04", "01.02").relation).toBe("newer");
    expect(installedCheck("01.02", "01.04").relation).toBe("older");
    expect(installedCheck("01.04", "01.04").relation).toBe("same");
  });
  it("space: bad with the shortfall when it clearly doesn't fit; unknown without a reading", () => {
    expect(spaceCheck(100, 40)).toEqual({ verdict: "bad", shortBy: 60 });
    expect(spaceCheck(100, 400).verdict).toBe("ok");
    expect(spaceCheck(100, null).verdict).toBe("unknown");
  });
});

import { installedFromPreflight, largestFreeBytes, platformFirmwareCheck } from "./packageChecks";

describe("review fixes", () => {
  it("never compares a PS4 package's firmware with the PS5's", () => {
    expect(platformFirmwareCheck("ps4", "9.00", "5.10").verdict).toBe("unknown");
    expect(platformFirmwareCheck("ps5", "12.00", "5.10").verdict).toBe("bad");
    expect(platformFirmwareCheck("", "5.10", "9.60").verdict).toBe("unknown");
  });

  it("an unknown or failed preflight is no verdict, never 'not installed'", () => {
    expect(installedFromPreflight("01.04", null)).toBeNull();
    expect(installedFromPreflight("01.04", { state: "unknown", installedVersion: null })).toBeNull();
  });

  it("installed without a version reads as installed", () => {
    expect(installedFromPreflight("01.04", { state: "installed", installedVersion: null })).toMatchObject({
      relation: "installed",
    });
  });

  it("an update or DLC whose base is missing says so", () => {
    expect(installedFromPreflight("01.04", { state: "base_missing", installedVersion: null })).toMatchObject({
      verdict: "bad",
      relation: "base-missing",
    });
  });

  it("a PS5 update whose base is missing is only a warning: the PS5 decides", () => {
    // A YouTube update package installed on a console without YouTube.
    const pre = { state: "base_missing", installedVersion: null };
    expect(installedFromPreflight("01.000.030", pre, "ps5")).toMatchObject({
      verdict: "warn",
      relation: "base-missing",
    });
    expect(installedFromPreflight("01.04", pre, "ps4")).toMatchObject({ verdict: "bad" });
  });

  it("an empty package version is not compared", () => {
    expect(installedFromPreflight("", { state: "installed", installedVersion: "01.04" })).toMatchObject({
      relation: "installed",
      installedVer: "01.04",
    });
  });

  it("versions compare when both are known", () => {
    expect(
      installedFromPreflight("01.04", { state: "different_version_installed", installedVersion: "01.02" }),
    ).toMatchObject({ relation: "newer" });
    expect(installedFromPreflight("01.04", { state: "not_installed", installedVersion: null })).toMatchObject({
      relation: "not-installed",
    });
  });

  it("space is measured against the roomiest single drive, not the sum", () => {
    const v = (path: string, free: number, extra: Record<string, unknown> = {}) =>
      ({ path, free_bytes: free, total_bytes: free * 2, writable: true, fs_type: "x", ...extra }) as never;
    expect(largestFreeBytes([v("/data", 100), v("/mnt/ext0", 300)])).toBe(300);
    expect(largestFreeBytes([v("/data", 100), v("/mnt/x", 900, { is_placeholder: true })])).toBe(100);
    expect(largestFreeBytes([])).toBeNull();
  });
});
