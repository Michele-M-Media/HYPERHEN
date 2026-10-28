import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

vi.mock("../api/ava1", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../api/ava1")>();
  return { ...actual, fetchRemoteSends: vi.fn() };
});

import { fetchRemoteSends, parseRemoteSends, type RemoteSend } from "../api/ava1";
import {
  REMOTE_POLL_MS,
  actionsFor,
  adoptItem,
  useRemoteSends,
  visibleRemoteSends,
  type ShownSend,
} from "./remoteSends";

const TX = (n: number) => n.toString(16).padStart(32, "0");

function send(over: Partial<RemoteSend> = {}): RemoteSend {
  return {
    txId: TX(1),
    from: "192.168.1.100",
    to: "192.168.1.99",
    src: "/data/homebrew/GAME",
    dest: "/data/homebrew/GAME",
    route: "auto",
    directAddr: null,
    ownerName: "Phone",
    mine: false,
    cls: "running_elsewhere",
    confirmed: true,
    liveRoute: "direct",
    bytesDone: 10,
    bytesTotal: 100,
    updatedMs: 1,
    error: null,
    canWatch: true,
    canTakeOver: true,
    ...over,
  };
}

describe("the engine's list of sends", () => {
  it("is read defensively: unknown classes and bad ids are dropped", () => {
    const r = parseRemoteSends({
      shared_jobs: true,
      sends: [
        {
          tx_id: TX(1),
          from: "a",
          to: "b",
          src: "/s",
          dest: "/d",
          route: "direct",
          direct_addr: "127.0.0.1:19120",
          owner_name: "Phone",
          mine: false,
          class: "paused",
          confirmed: true,
          live_route: "relay",
          bytes_done: 5,
          bytes_total: 9,
          updated_ms: 7,
          error: null,
          can_watch: false,
          can_take_over: true,
        },
        { tx_id: TX(2), class: "exploded" },
        { tx_id: "nothex", class: "paused" },
        null,
      ],
    });
    expect(r.sharedJobs).toBe(true);
    expect(r.sends).toHaveLength(1);
    expect(r.sends[0]).toMatchObject({
      cls: "paused",
      route: "direct",
      directAddr: "127.0.0.1:19120",
      liveRoute: "relay",
      bytesDone: 5,
      canTakeOver: true,
    });
    expect(parseRemoteSends(undefined)).toEqual({ sends: [], sharedJobs: null });
  });
});

describe("what Tasks shows", () => {
  it("hides this app's own sends and anything already in its queue", () => {
    const byConsole = {
      "192.168.1.99": {
        sends: [
          send({ txId: TX(1), updatedMs: 1 }),
          send({ txId: TX(2), cls: "mine" }),
          send({ txId: TX(3), cls: "paused", updatedMs: 5 }),
        ],
        sharedJobs: true,
        error: null,
      },
      "192.168.1.98": { sends: [send({ txId: TX(4), updatedMs: 3 })], sharedJobs: null, error: null },
    };
    const shown = visibleRemoteSends(byConsole, new Set([TX(4)]));
    expect(shown.map((s) => s.txId)).toEqual([TX(3), TX(1)]);
    expect(shown[0].console).toBe("192.168.1.99");
  });

  it("offers watch only for a direct send, and resume for a stopped one", () => {
    expect(actionsFor(send())).toEqual(["watch", "take"]);
    expect(actionsFor(send({ liveRoute: "relay", canWatch: false }))).toEqual(["take"]);
    // Older helpers: a running send can be neither watched nor taken over from here.
    expect(actionsFor(send({ canWatch: false, canTakeOver: false }))).toEqual([]);
    expect(actionsFor(send({ cls: "paused" }))).toEqual(["resume"]);
    expect(actionsFor(send({ cls: "interrupted", confirmed: false }))).toEqual(["resume"]);
    expect(actionsFor(send({ cls: "mine" }))).toEqual([]);
  });

  it("adopts a send under its own job id, on the console it was found on", () => {
    const s: ShownSend = { ...send({ route: "direct", directAddr: "127.0.0.1:19120" }), console: "192.168.1.99" };
    const take = adoptItem(s, "take", ["192.168.1.100:9120"]);
    expect(take).toMatchObject({
      sourceKind: "ps5",
      fromConsole: "192.168.1.100",
      addr: "192.168.1.99",
      sourcePath: "/data/homebrew/GAME",
      resolvedDest: "/data/homebrew/GAME",
      displayName: "GAME",
      route: "direct",
      directAddr: "127.0.0.1:19120",
      txIdHex: TX(1),
      adopt: "take",
    });
    expect(adoptItem(s, "resume").adopt).toBe("take");
    expect(adoptItem(s, "watch").adopt).toBe("watch");
  });
});

describe("polling", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    useRemoteSends.setState({ byConsole: {} });
    vi.mocked(fetchRemoteSends).mockReset();
  });
  afterEach(() => vi.useRealTimers());

  it("asks every console now and every 15 s while Tasks is open, and stops after", async () => {
    vi.mocked(fetchRemoteSends).mockResolvedValue({ sends: [send()], sharedJobs: true });
    const stop = useRemoteSends.getState().watch(() => ["192.168.1.99", "192.168.1.98:9120", "192.168.1.99"]);
    await vi.advanceTimersByTimeAsync(0);
    expect(vi.mocked(fetchRemoteSends).mock.calls.map((c) => c[0]).sort()).toEqual([
      "192.168.1.98",
      "192.168.1.99",
    ]);
    expect(useRemoteSends.getState().byConsole["192.168.1.99"].sends).toHaveLength(1);
    await vi.advanceTimersByTimeAsync(REMOTE_POLL_MS);
    expect(vi.mocked(fetchRemoteSends)).toHaveBeenCalledTimes(4);
    stop();
    await vi.advanceTimersByTimeAsync(REMOTE_POLL_MS * 3);
    expect(vi.mocked(fetchRemoteSends)).toHaveBeenCalledTimes(4);
  });

  it("keeps what an unreachable console last said", async () => {
    vi.mocked(fetchRemoteSends).mockResolvedValueOnce({ sends: [send()], sharedJobs: true });
    await useRemoteSends.getState().refresh(["192.168.1.99"]);
    vi.mocked(fetchRemoteSends).mockRejectedValueOnce(new Error("offline"));
    await useRemoteSends.getState().refresh(["192.168.1.99"]);
    const c = useRemoteSends.getState().byConsole["192.168.1.99"];
    expect(c.sends).toHaveLength(1);
    expect(c.error).toBe("offline");
  });
});
