import { describe, expect, it, vi } from "vitest";

vi.mock("@tauri-apps/api/core", () => ({ invoke: vi.fn() }));

import { PROBED_PORTS } from "./ps5Snapshot";

describe("bug report port probes", () => {
  it("probe the helper's one AVA1 port, not the retired transfer and management ports", () => {
    const ports = PROBED_PORTS.map((p) => p.port);
    expect(ports).toContain(9120);
    expect(ports).not.toContain(9113);
    expect(ports).not.toContain(9114);
    // The loader and DPI daemon are not ours to drop: their state is the diagnosis.
    expect(ports).toContain(9021);
    expect(ports).toContain(9115);
  });
});
