import { beforeEach, describe, expect, it, vi } from "vitest";

vi.mock("../api", async (importOriginal) => {
  const real = await importOriginal<typeof import("../api")>();
  return { ...real, call: vi.fn() };
});

import { ApiError, call } from "../api";
import { createDirLoader, LIST_PAGE, sortEntries, type DirState, type Entry } from "./useDir";

const mockCall = vi.mocked(call);

const file = (name: string, kind: Entry["kind"] = "file"): Entry => ({ name, kind, size: 1, mtime: 0 });

/** A folder of `pages` full pages: page i is offsets i*256…, `more` until the last. */
function serve(pages: number) {
  mockCall.mockImplementation(async (_route: string, body?: object) => {
    const { offset } = body as { offset: number };
    const page = offset / LIST_PAGE;
    const entries = Array.from({ length: LIST_PAGE }, (_, i) => file(`f${String(offset + i).padStart(5, "0")}`));
    return { entries, more: page < pages - 1, next: offset + LIST_PAGE } as never;
  });
}

beforeEach(() => {
  mockCall.mockReset();
});

describe("useDir", () => {
  it("loads_more_pages_until_done", async () => {
    serve(3);
    let last: DirState | null = null;
    const loader = createDirLoader("/data/big", (s) => {
      last = s;
    });
    await loader.loadMore();
    expect(last!.entries).toHaveLength(256);
    expect(last!.more).toBe(true);
    await loader.loadMore();
    await loader.loadMore();
    expect(last!.entries).toHaveLength(768);
    expect(last!.more).toBe(false);
    // Done: another loadMore asks nothing.
    await loader.loadMore();
    expect(mockCall).toHaveBeenCalledTimes(3);
    // `next` is the next page's offset (ruling R9), used as-is.
    expect(mockCall.mock.calls.map((c) => c[1])).toEqual([
      { path: "/data/big", offset: 0, limit: 256 },
      { path: "/data/big", offset: 256, limit: 256 },
      { path: "/data/big", offset: 512, limit: 256 },
    ]);
  });

  it("never asks for two pages at once", async () => {
    serve(3);
    const loader = createDirLoader("/d", () => {});
    await Promise.all([loader.loadMore(), loader.loadMore()]);
    expect(mockCall).toHaveBeenCalledTimes(1);
  });

  it("refresh reads again as many pages as were shown", async () => {
    serve(3);
    let last: DirState | null = null;
    const loader = createDirLoader("/d", (s) => {
      last = s;
    });
    await loader.loadMore();
    await loader.loadMore();
    mockCall.mockClear();
    await loader.refresh();
    expect(mockCall).toHaveBeenCalledTimes(2);
    expect(last!.entries).toHaveLength(512);
    expect(last!.more).toBe(true);
  });

  it("a failed page keeps what was shown and reports the error", async () => {
    serve(3);
    let last: DirState | null = null;
    const loader = createDirLoader("/d", (s) => {
      last = s;
    });
    await loader.loadMore();
    mockCall.mockRejectedValueOnce(new ApiError(403, "forbidden"));
    await loader.loadMore();
    expect(last!.entries).toHaveLength(256);
    expect((last!.error as ApiError).status).toBe(403);
    expect(last!.loading).toBe(false);
  });

  it("an answer for a stopped loader is dropped", async () => {
    serve(1);
    const seen = vi.fn();
    const loader = createDirLoader("/d", seen);
    const p = loader.loadMore();
    loader.stop();
    seen.mockClear();
    await p;
    expect(seen).not.toHaveBeenCalled();
  });

  it("folders first, then names in natural order", () => {
    const sorted = sortEntries([file("b10"), file("Zeta", "dir"), file("b2"), file("alpha", "dir"), file("a")]);
    expect(sorted.map((e) => e.name)).toEqual(["alpha", "Zeta", "a", "b2", "b10"]);
  });
});
