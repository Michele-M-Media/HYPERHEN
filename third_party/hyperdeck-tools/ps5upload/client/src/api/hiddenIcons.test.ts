import { afterEach, describe, expect, it, vi } from "vitest";

vi.mock("../state/engine", () => ({ getEngineUrl: () => "http://engine.test:19113" }));

import { hiddenIcons, setIconHidden } from "./hiddenIcons";

const reply = (status: number, body: unknown) =>
  new Response(JSON.stringify(body), { status });

afterEach(() => vi.unstubAllGlobals());

describe("hidden home-screen icons (#407)", () => {
  it("reads the hidden list from the console by bare host", async () => {
    const f = vi.fn().mockResolvedValue(reply(200, { ok: true, hidden: ["ITEM00001"] }));
    vi.stubGlobal("fetch", f);
    expect(await hiddenIcons("10.0.0.2:9113")).toEqual(["ITEM00001"]);
    expect(f.mock.calls[0][0]).toBe("http://engine.test:19113/api/ps5/apps/hidden?addr=10.0.0.2");
  });

  it("says why it cannot read the list", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(reply(200, { ok: false, error: "cannot read app.db" })));
    await expect(hiddenIcons("10.0.0.2")).rejects.toThrow("cannot read app.db");
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(reply(502, { error: "helper_not_ava1" })));
    await expect(hiddenIcons("10.0.0.2")).rejects.toThrow("helper_not_ava1");
  });

  it("hides and shows with the console's reason on a refusal", async () => {
    const f = vi.fn().mockResolvedValue(reply(200, { ok: true, err: null }));
    vi.stubGlobal("fetch", f);
    await setIconHidden("10.0.0.2", "ITEM00001", true);
    expect(f.mock.calls[0][0]).toBe("http://engine.test:19113/api/ps5/apps/hidden");
    expect(JSON.parse(f.mock.calls[0][1].body)).toEqual({
      addr: "10.0.0.2",
      title_id: "ITEM00001",
      hidden: true,
    });
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue(reply(200, { ok: false, error: "NPXS40000 is a system app and cannot be hidden" })),
    );
    await expect(setIconHidden("10.0.0.2", "NPXS40000", true)).rejects.toThrow("system app");
  });
});
