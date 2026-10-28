import { describe, expect, it, vi } from "vitest";

vi.mock("./tauriEnv", () => ({ isTauriEnv: () => false }));

import { bgStrings, labelEngineJob } from "./bgWork";

describe("bgWork", () => {
  it("sends exactly the texts the host's BgStrings reads", () => {
    // bg_activity.rs ignores unknown keys and falls back to English for missing
    // ones, so a renamed field would silently stop translating.
    // Read through Vite: the client tsconfig ships no node types.
    const rs = (
      import.meta.glob("../../src-tauri/src/bg_activity.rs", {
        query: "?raw",
        import: "default",
        eager: true,
      }) as Record<string, string>
    )["../../src-tauri/src/bg_activity.rs"];
    const body = rs.slice(rs.indexOf("pub struct BgStrings {"));
    const fields = [...body.slice(0, body.indexOf("}")).matchAll(/pub (\w+): String/g)].map(
      (m) => m[1],
    );
    expect(fields.length).toBeGreaterThan(10);
    expect(Object.keys(bgStrings()).sort()).toEqual([...fields].sort());
  });

  it("keeps the placeholders the host fills in", () => {
    const s = bgStrings();
    expect(s.uploading).toContain("{name}");
    expect(s.jobs_running).toContain("{n}");
    expect(s.progress_of).toContain("{done}");
    expect(s.progress_of).toContain("{total}");
  });

  it("names no job outside Tauri (the web UI has no host)", () => {
    const fetchSpy = vi.spyOn(globalThis, "fetch");
    labelEngineJob("job-1", "upload", "Astro Bot");
    expect(fetchSpy).not.toHaveBeenCalled();
    fetchSpy.mockRestore();
  });
});
