import { describe, it, expect } from "vitest";
import { accountIdToChiakiNumeric, accountIdToPairingB64 } from "./remoteplay";

// Ground truth. The console stores the id as 8 bytes, low byte first; the
// base64 is over those bytes and pxplay's number is the same uint64 in
// decimal. ps5-account-tools prints all three from one uint64_t.
//   0x7ed3586971779f76 = "dp93cWlY034=" = 9138745278763343734  (issue report)
//   0x7a356e99a9e2205c = "XCDiqZluNXo=" = 8806066252652093532  (both test consoles)
const REPORTED = { hex: "0x7ed3586971779f76", b64: "dp93cWlY034=", dec: "9138745278763343734" };
const CONSOLES = { hex: "0x7a356e99a9e2205c", b64: "XCDiqZluNXo=", dec: "8806066252652093532" };

describe("accountIdToChiakiNumeric", () => {
  it("returns empty string for empty or unreadable input", () => {
    expect(accountIdToChiakiNumeric("")).toBe("");
    expect(accountIdToChiakiNumeric("!!!")).toBe("");
  });

  it("reads the payload's base64 low byte first, like the console", () => {
    expect(accountIdToChiakiNumeric(REPORTED.b64)).toBe(REPORTED.dec);
    expect(accountIdToChiakiNumeric(CONSOLES.b64)).toBe(CONSOLES.dec);
  });

  it("reads a typed hex id, with or without 0x", () => {
    expect(accountIdToChiakiNumeric(REPORTED.hex)).toBe(REPORTED.dec);
    expect(accountIdToChiakiNumeric("7ed3586971779f76")).toBe(REPORTED.dec);
    expect(accountIdToChiakiNumeric("7ED3586971779F76")).toBe(REPORTED.dec);
  });

  it("passes a pxplay number through", () => {
    expect(accountIdToChiakiNumeric(REPORTED.dec)).toBe(REPORTED.dec);
  });

  it("is exact at the full 64-bit width", () => {
    expect(accountIdToChiakiNumeric("0xffffffffffffffff")).toBe("18446744073709551615");
    expect(accountIdToChiakiNumeric("//////////8=")).toBe("18446744073709551615");
    expect(accountIdToChiakiNumeric("AAAAAAAAAAA=")).toBe("0");
  });

  it("strips surrounding whitespace", () => {
    expect(accountIdToChiakiNumeric(`  ${REPORTED.b64} \n`)).toBe(REPORTED.dec);
  });
});

describe("accountIdToPairingB64", () => {
  it("turns every form the user might paste into the console's base64", () => {
    for (const v of [REPORTED.hex, "7ed3586971779f76", REPORTED.dec, REPORTED.b64]) {
      expect(accountIdToPairingB64(v)).toBe(REPORTED.b64);
    }
    expect(accountIdToPairingB64(CONSOLES.hex)).toBe(CONSOLES.b64);
  });

  it("rejects what is not an account id", () => {
    for (const v of ["", "   ", "xyz", "0x", "0", "0x0", "12 34", "0x00123456789abcdef", "99999999999999999999"]) {
      expect(accountIdToPairingB64(v)).toBeNull();
    }
  });
});
