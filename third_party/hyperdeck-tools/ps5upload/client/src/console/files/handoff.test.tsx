import { isValidElement, type ReactElement, type ReactNode } from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { beforeEach, describe, expect, it, vi } from "vitest";

vi.mock("../api", async (importOriginal) => {
  const real = await importOriginal<typeof import("../api")>();
  return { ...real, call: vi.fn() };
});

import { ApiError, call } from "../api";
import { fetchJobsAll } from "../home/useJobs";
import { t } from "../strings";
import { ActionsSheet } from "./ActionsSheet";
import { DirList } from "./DirList";
import { FilesView, type FilesViewProps } from "./Files";
import {
  canShowOnPc,
  checkHandoff,
  handoffMessage,
  handoffTarget,
  resetHandoffSupport,
  showOnPc,
  takeHandoff,
  uploadingNames,
} from "./handoff";
import type { DirState, Entry } from "./useDir";

const mockCall = vi.mocked(call);

function elements(node: ReactNode): ReactElement<Record<string, unknown>>[] {
  const out: ReactElement<Record<string, unknown>>[] = [];
  const walk = (n: ReactNode) => {
    if (Array.isArray(n)) return n.forEach(walk);
    if (!isValidElement(n)) return;
    const el = n as ReactElement<Record<string, unknown>>;
    out.push(el);
    walk(el.props.children as ReactNode);
  };
  walk(node);
  return out;
}

const entry = (name: string, kind: Entry["kind"] = "file"): Entry => ({ name, kind, size: 10, mtime: 0 });
const dir = (entries: Entry[]): DirState => ({ entries, more: false, loading: false, loaded: true, error: null });

function props(over: Partial<FilesViewProps> = {}): FilesViewProps {
  return {
    path: "/data",
    volumes: [],
    volumesError: false,
    dir: dir([entry("games", "dir"), entry("a.txt")]),
    selecting: false,
    selected: [],
    ask: null,
    busy: null,
    message: null,
    tasks: [],
    prefs: null,
    onOpen: vi.fn(),
    onUp: vi.fn(),
    onEntry: vi.fn(),
    onLoadMore: vi.fn(),
    onToggleSelecting: vi.fn(),
    onToggleItem: vi.fn(),
    onAsk: vi.fn(),
    onDismiss: vi.fn(),
    onConfirmDelete: vi.fn(),
    onConfirmFixPerm: vi.fn(),
    onRename: vi.fn(),
    onMkdir: vi.fn(),
    onPicked: vi.fn(),
    onShowPc: vi.fn(),
    ...over,
  };
}

beforeEach(() => {
  mockCall.mockReset();
  resetHandoffSupport();
});

describe("Show on TV: the PC's handoff", () => {
  it("takes the waiting folder, and learns the helper has handoffs", async () => {
    mockCall.mockResolvedValueOnce({ path: "/mnt/usb0/games", age_ms: 900 } as never);
    expect(await takeHandoff()).toBe("/mnt/usb0/games");
    expect(mockCall).toHaveBeenCalledWith("/api/files/handoff/take", { for: "console" });
    expect(canShowOnPc()).toBe(true);
    mockCall.mockResolvedValueOnce({} as never);
    expect(await takeHandoff()).toBeNull();
  });

  it("a helper without the routes (404): no handoffs, no Show on PC", async () => {
    mockCall.mockRejectedValueOnce(new ApiError(404, "not_found"));
    await expect(takeHandoff()).rejects.toBeInstanceOf(ApiError);
    expect(canShowOnPc()).toBe(false);
  });

  it("opens a folder as it is, a file in its folder with the file focused", async () => {
    mockCall.mockResolvedValueOnce({ kind: "dir" } as never);
    expect(await handoffTarget("/data/homebrew")).toEqual({ path: "/data/homebrew", handoff: "pc" });
    mockCall.mockResolvedValueOnce({ kind: "file" } as never);
    expect(await handoffTarget("/data/homebrew/eboot.bin")).toEqual({
      path: "/data/homebrew",
      focus: "eboot.bin",
      handoff: "pc",
    });
    mockCall.mockResolvedValueOnce({ kind: "file" } as never);
    expect(await handoffTarget("/top.txt")).toEqual({ path: "/", focus: "top.txt", handoff: "pc" });
  });

  it("a path that is gone opens no folder and says so", async () => {
    mockCall.mockRejectedValueOnce(new ApiError(404, "io_error"));
    const p = await handoffTarget("/mnt/usb0/gone");
    expect(p).toEqual({ handoff_gone: "/mnt/usb0/gone" });
    expect(handoffMessage(p)).toEqual({ tone: "info", text: t("cw_files_from_pc_gone", { path: "/mnt/usb0/gone" }) });
    expect(handoffMessage({ path: "/data", handoff: "pc" })).toEqual({ tone: "info", text: t("cw_files_from_pc") });
    expect(handoffMessage({ path: "/data" })).toBeNull();
    expect(handoffMessage(null)).toBeNull();
  });

  it("one poll tick: a handoff opens Files at its target; nothing waiting opens nothing", async () => {
    const open = vi.fn();
    mockCall.mockImplementation((async (route: string) => {
      if (route === "/api/files/handoff/take") return { path: "/data/games", age_ms: 10 };
      if (route === "/api/fs/stat") return { kind: "dir" };
      throw new Error(route);
    }) as never);
    expect(await checkHandoff(open)).toBe(true);
    expect(open).toHaveBeenCalledWith({ path: "/data/games", handoff: "pc" });
    mockCall.mockReset();
    mockCall.mockResolvedValue({} as never);
    expect(await checkHandoff(open)).toBe(false);
    expect(open).toHaveBeenCalledTimes(1);
  });

  it("a tick that ends after the page left opens nothing", async () => {
    const open = vi.fn();
    mockCall.mockResolvedValueOnce({ path: "/data", age_ms: 0 } as never).mockResolvedValueOnce({ kind: "dir" } as never);
    expect(await checkHandoff(open, () => false)).toBe(false);
    expect(open).not.toHaveBeenCalled();
  });
});

describe("Show on PC", () => {
  it("is in the sheet only once the helper is known to have handoffs, and hands the item's path on", () => {
    const sheet = (p: FilesViewProps) => elements(FilesView(p)).find((e) => e.type === ActionsSheet)!;
    const before = props({ ask: { kind: "sheet", names: ["games"] } });
    expect(sheet(before).props.actions).not.toContain("showpc");
    resetHandoffSupport(true);
    const p = props({ ask: { kind: "sheet", names: ["a.txt"] } });
    expect(sheet(p).props.actions).toContain("showpc");
    (sheet(p).props.onChoose as (a: string) => void)("showpc");
    expect(p.onShowPc).toHaveBeenCalledWith("/data/a.txt");
    // Several items: not offered (one place at a time).
    const many = props({ ask: { kind: "sheet", names: ["a.txt", "games"] } });
    expect(sheet(many).props.actions).not.toContain("showpc");
  });

  it("the sheet draws it with its label", () => {
    const html = renderToStaticMarkup(
      <ActionsSheet title="x" actions={["showpc"]} busy={false} onChoose={() => {}} onClose={() => {}} />,
    );
    expect(html).toContain(t("cw_files_show_pc"));
  });

  it("puts the handoff for the PC and says so; a refusal says that", async () => {
    mockCall.mockResolvedValueOnce({} as never);
    expect(await showOnPc("/data/games")).toEqual({ tone: "info", text: t("cw_files_show_pc_done") });
    expect(mockCall).toHaveBeenCalledWith("/api/files/handoff/put", { to: "pc", path: "/data/games" });
    mockCall.mockRejectedValueOnce(new ApiError(400, "bad_side"));
    expect(await showOnPc("/data/games")).toEqual({ tone: "error", text: t("cw_files_show_pc_failed") });
  });
});

describe("Uploading badge", () => {
  it("names the entries a running upload writes into", () => {
    const roots = ["/data/homebrew/Game", "/data/homebrew/Other/deep/x", "/mnt/usb0/a", "/data/homebrewX/y"];
    expect([...uploadingNames("/data/homebrew", roots)].sort()).toEqual(["Game", "Other"]);
    expect([...uploadingNames("/data/homebrew/", roots)].sort()).toEqual(["Game", "Other"]);
    expect([...uploadingNames("/", roots)].sort()).toEqual(["data", "mnt"]);
    expect(uploadingNames("/data/homebrew/Game", roots).size).toBe(0); // the root itself is shown one level up
    expect(uploadingNames(null, roots).size).toBe(0);
  });

  it("draws the badge on those rows only", () => {
    const html = renderToStaticMarkup(
      <DirList
        entries={[entry("Game", "dir"), entry("Done", "dir")]}
        selecting={false}
        selected={[]}
        onEntry={() => {}}
        onToggleItem={() => {}}
        onActions={() => {}}
        uploading={new Set(["Game"])}
      />,
    );
    expect(html.match(/data-uploading/g)?.length).toBe(1);
    expect(html).toContain(t("cw_files_uploading"));
    const game = html.slice(html.indexOf('data-entry="Game"'), html.indexOf('data-entry="Done"'));
    expect(game).toContain("data-uploading");
  });

  it("FilesView passes the badges to the list", () => {
    const html = renderToStaticMarkup(<FilesView {...props({ uploading: new Set(["games"]) })} />);
    expect(html).toContain("data-uploading");
  });

  it("/api/jobs' uploading list is read alongside the jobs (bad entries dropped)", async () => {
    mockCall.mockResolvedValueOnce({ jobs: [], uploading: ["/data/x", 3, "rel"] } as never);
    expect(await fetchJobsAll()).toEqual({ jobs: [], uploading: ["/data/x"] });
    mockCall.mockResolvedValueOnce({ jobs: [] } as never);
    expect(await fetchJobsAll()).toEqual({ jobs: [], uploading: [] });
  });
});
