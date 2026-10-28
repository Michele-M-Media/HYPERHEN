import { describe, expect, it } from "vitest";

import type { PayloadReleaseInfo } from "../api/ps5";
import { isRepoManaged, pmUpdateFor, swapAutoloadKey } from "./pmUpdates";

const rel = (tag: string, url = "https://x/a.elf"): PayloadReleaseInfo => ({
  payload_id: "nanodns",
  tag,
  name: tag,
  body: "",
  published_at: "",
  html_url: "",
  picked_asset_url: url,
  picked_asset_name: "a.elf",
  picked_asset_size: 1,
  cached_age_secs: 0,
});

describe("pmUpdateFor", () => {
  it("offers the newer release only for something this app stored", () => {
    expect(pmUpdateFor({ id: "nanodns", tag: "v0.4" }, rel("v0.5"))?.tag).toBe("v0.5");
    expect(pmUpdateFor({ id: "nanodns", tag: "v0.5" }, rel("v0.5"))).toBeNull();
    expect(pmUpdateFor(undefined, rel("v0.5"))).toBeNull();
    expect(pmUpdateFor({ id: "nanodns", tag: "v0.4" }, rel("v0.5", ""))).toBeNull();
  });
});

describe("swapAutoloadKey", () => {
  it("keeps the boot position and the pauses", () => {
    expect(swapAutoloadKey(["a.elf", "!3000", "old.elf", "!3000", "c.elf"], "old.elf", "new.elf")).toEqual([
      "a.elf",
      "!3000",
      "new.elf",
      "!3000",
      "c.elf",
    ]);
  });
  it("does not list the new one twice", () => {
    expect(swapAutoloadKey(["new.elf", "old.elf"], "old.elf", "new.elf")).toEqual(["new.elf"]);
  });
  it("leaves a list without the old one alone", () => {
    const l = ["a.elf"];
    expect(swapAutoloadKey(l, "old.elf", "new.elf")).toBe(l);
  });
});

describe("isRepoManaged", () => {
  it("knows Payload Manager's own repository installs", () => {
    expect(isRepoManaged({ slot: "", path: "/data/pldmgr/payloads/repo/nanodns@e8cce5d1/nanoDNS_0.4.elf" })).toBe(true);
    expect(isRepoManaged({ slot: "repo/nanodns@x", path: "/data/pldmgr/payloads/repo/x" })).toBe(true);
    expect(isRepoManaged({ slot: "local/nanodns", path: "/data/pldmgr/payloads/local/nanodns/n.elf" })).toBe(false);
  });
});
