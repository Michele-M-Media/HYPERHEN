import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it, vi } from "vitest";

vi.mock("@tauri-apps/api/core", () => ({ invoke: vi.fn() }));

import { QueueRow, queueItemsForHost, queueSections } from "./QueuePanel";
import en from "../../i18n/locales/en";
import type { QueueItem } from "../../state/uploadQueue";

const base = {
  strategy: "overwrite",
  reconcileMode: "fast",
  excludes: [],
  mountAfterUpload: false,
  mountReadOnly: true,
  registerAfterUpload: false,
  txIdHex: "00",
  bytesSent: 0,
  totalBytes: 0,
  bytesPerSec: 0,
  filesFinalized: 0,
  filesFinalizingTotal: 0,
  mountedAt: null,
  registeredAs: null,
  mountWarnings: [],
  error: null,
  errorReason: null,
  errorDetail: null,
  addedAt: 1,
  startedAt: null,
  completedAt: null,
  resolvedDest: "",
} as const;

const item = (over: Record<string, unknown>) => ({ ...base, ...over }) as unknown as QueueItem;

describe("Queue panel", () => {
  const failedStream = item({
    id: "a",
    sourceKind: "install",
    sourcePath: "pc:/g/a.pkg",
    displayName: "Game A",
    addr: "10.0.0.2:9113",
    install: { via: "stream", source: "/g/a.pkg" },
    status: "failed",
    error: "unreachable",
    fallbackToUpload: true,
  });
  const waitingLibrary = item({
    id: "c",
    sourceKind: "install",
    sourcePath: "ps5:/q.pkg",
    displayName: "Game C",
    addr: "10.0.0.2:9113",
    install: { via: "library", path: "/q.pkg" },
    status: "pending",
  });
  const otherConsole = item({
    id: "b",
    sourceKind: "install",
    sourcePath: "ps5:/p.pkg",
    displayName: "Game B",
    addr: "10.0.0.3:9113",
    install: { via: "library", path: "/p.pkg" },
    status: "pending",
  });
  const noop = () => {};
  const row = (it: QueueItem, position?: number) =>
    renderToStaticMarkup(
      <QueueRow
        item={it}
        position={position}
        onMoveUp={noop}
        onMoveDown={noop}
        onRemove={noop}
        onCancel={noop}
        onRetry={noop}
        onRetryViaUpload={noop}
      />,
    );

  it("shows only the given console's items", () => {
    const items = [failedStream, waitingLibrary, otherConsole];
    expect(queueItemsForHost(items, "10.0.0.2").map((i) => i.id)).toEqual(["a", "c"]);
    expect(queueItemsForHost(items, undefined)).toHaveLength(3);
  });

  it("offers Retry via upload on a failed stream install that asked for it", () => {
    const html = row(failedStream);
    expect(html).toContain("Retry via upload");
    expect(html).toContain("Retry");
  });

  it("says where an install comes from and where it waits in line", () => {
    expect(row(failedStream)).toContain("Stream install from this computer");
    const waiting = row(waitingLibrary, 1);
    expect(waiting).toContain("Install from the PS5");
    expect(waiting).toContain("Queued (#1)");
    expect(waiting).not.toContain("Overwrite");
  });

  const runningInstall = (over: Record<string, unknown> = {}) =>
    item({
      id: "r",
      sourceKind: "install",
      sourcePath: "pc:/g/big.pkg",
      displayName: "Big Game",
      addr: "10.0.0.2:9113",
      install: { via: "stream", source: "/g/big.pkg" },
      status: "running",
      installPhase: "installing",
      ...over,
    });

  it("shows a running install's phase, bytes, rate and time left in its own row", () => {
    const html = row(
      runningInstall({
        installPct: 13,
        installProgress: {
          phase: "install",
          current: 13_000_000_000,
          total: 100_000_000_000,
          bytesPerSec: 100_000_000,
        },
      }),
    );
    expect(html).toContain("Installing on the PS5");
    expect(html).toContain("13%");
    expect(html).toMatch(/of [\d.]+ GiB/);
    expect(html).toMatch(/\/s/);
    expect(html).toContain("left");
  });

  it("names both legs of a link install", () => {
    const html = row(
      runningInstall({
        installPct: 40,
        installProgress: {
          phase: "transfer",
          current: 4e9,
          total: 1e10,
          bytesPerSec: 50e6,
          originBytesPerSec: 90e6,
        },
      }),
    );
    expect(html).toContain("Sending to the PS5");
    expect(html).toContain("downloading");
    expect(html).toContain("sending");
  });

  it("says it is getting ready, with the status note, before numbers arrive", () => {
    const html = row(runningInstall({ installNote: "Waiting for the PS5 to be ready…" }));
    expect(html).toContain("Getting ready…");
    expect(html).toContain("Waiting for the PS5 to be ready…");
    expect(html).not.toContain("0%");
  });

  it("offers reorder only on waiting rows", () => {
    expect(row(waitingLibrary, 1)).toContain("Move up");
    expect(row(runningInstall())).not.toContain("Move up");
    expect(row(failedStream)).not.toContain("Move up");
  });

  it("orders a console's rows: running, then waiting in run order, then finished", () => {
    const done = item({ ...waitingLibrary, id: "d", status: "done", completedAt: 5 });
    const update = item({ ...waitingLibrary, id: "u", category: "gp" });
    const baseGame = item({ ...waitingLibrary, id: "g", category: "gd" });
    const sections = queueSections([done, update, failedStream, runningInstall(), baseGame]);
    expect(sections.map((s) => s.key)).toEqual(["now", "next", "finished"]);
    expect(sections[0].items.map((i) => i.id)).toEqual(["r"]);
    // base before update, whatever the list order
    expect(sections[1].items.map((i) => i.id)).toEqual(["g", "u"]);
    // a failure wants a decision, so it leads the finished rows
    expect(sections[2].items.map((i) => i.id)).toEqual(["a", "d"]);
  });

  it("leaves out empty sections", () => {
    expect(queueSections([waitingLibrary]).map((s) => s.key)).toEqual(["next"]);
  });

  it("titles the panel Queue", () => {
    expect(en.queue_title).toBe("Queue");
  });
});
