import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// The runner against a console that goes into rest mode: the row waits for the wake (no
// failure, no spent recovery attempt) and resumes on the helper-up edge, in both builds.
vi.mock("@tauri-apps/api/core", () => ({ invoke: vi.fn() }));
vi.mock("../lib/materialize", () => ({
  releaseCopy: vi.fn(async () => {}),
  materializeRemote: vi.fn(async (p: string) => p),
}));
vi.mock("../lib/ensurePayloadCurrent", () => ({
  ensurePayloadCurrent: vi.fn(async () => {}),
}));
vi.mock("../api/ps5", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../api/ps5")>();
  return {
    ...actual,
    uploadQueueLoad: vi.fn(async () => ({ items: [], continueOnFailure: false })),
    uploadQueueSave: vi.fn(async () => {}),
    startTransferFile: vi.fn(async () => "job"),
    startTransferDir: vi.fn(async () => "job"),
    startTransferDirReconcile: vi.fn(async () => "job"),
    jobStatus: vi.fn(async () => ({ status: "running" })),
    jobCancel: vi.fn(async () => {}),
    fsMkdir: vi.fn(async () => {}),
    fsDelete: vi.fn(async () => {}),
    powerStandby: vi.fn(async () => ({ ok: true })),
  };
});

import { jobStatus, startTransferFile } from "../api/ps5";
import { ensurePayloadCurrent } from "../lib/ensurePayloadCurrent";
import { useUploadQueueStore, distinctPendingHosts, type AddQueueItem } from "./uploadQueue";
import { useUploadSettingsStore } from "./uploadSettings";
import { useConnectionStore } from "./connection";

const HOST = "192.168.1.10";
const mockedJobStatus = vi.mocked(jobStatus);
const mockedStartFile = vi.mocked(startTransferFile);
const mockedEnsure = vi.mocked(ensurePayloadCurrent);
type Snap = Awaited<ReturnType<typeof jobStatus>>;

function addItem(name: string) {
  const input: AddQueueItem = {
    sourceKind: "file",
    sourcePath: `/src/${name}`,
    displayName: name,
    resolvedDest: `/data/${name}`,
    addr: HOST,
    strategy: "overwrite",
    reconcileMode: "fast",
    excludes: [],
    mountAfterUpload: false,
    mountReadOnly: false,
    registerAfterUpload: false,
  };
  useUploadQueueStore.getState().add(input);
}

const row = () => useUploadQueueStore.getState().items[0];
const setPower = (power: "awake" | "rest" | "unknown") =>
  useConnectionStore.getState().setHostStatus(HOST, { power });

for (const build of ["desktop", "browser"] as const) {
  describe(`uploads wait for the PS5 to wake (${build})`, () => {
    beforeEach(() => {
      const store = new Map<string, string>();
      vi.stubGlobal("localStorage", {
        getItem: (k: string) => store.get(k) ?? null,
        setItem: (k: string, v: string) => void store.set(k, v),
        removeItem: (k: string) => void store.delete(k),
      });
      vi.stubGlobal("window", build === "desktop" ? { isTauri: true } : {});
      vi.useFakeTimers();
      mockedJobStatus.mockReset();
      mockedStartFile.mockReset().mockResolvedValue("job");
      mockedEnsure.mockReset().mockResolvedValue(undefined as never);
      useUploadQueueStore.setState({
        items: [],
        running: false,
        runningHosts: {},
        continueOnFailure: true,
        loaded: true,
        isLeader: true,
      });
      useUploadSettingsStore.setState({ autoResume: true });
      setPower("awake");
    });
    afterEach(() => {
      useUploadQueueStore.getState().stop();
      vi.useRealTimers();
      vi.unstubAllGlobals();
    });

    it("a console_rest_mode failure parks the row without a recovery attempt", async () => {
      addItem("A");
      mockedJobStatus.mockResolvedValue({
        status: "failed",
        error: "The PS5 is in rest mode",
        error_reason: "console_rest_mode",
      } as Snap);

      const p = useUploadQueueStore.getState().start();
      await vi.advanceTimersByTimeAsync(60_000);
      await p;

      expect(row()).toMatchObject({
        status: "pending",
        waitingForWake: true,
        recovering: false,
        error: null,
      });
      // One attempt, no backoff-and-retry, no helper re-send aimed at a sleeping console.
      expect(mockedStartFile).toHaveBeenCalledTimes(1);
      expect(mockedEnsure.mock.calls.length).toBeLessThanOrEqual(1);
      expect(useUploadQueueStore.getState().runningHosts[HOST]).toBeUndefined();
      // Start-all's later passes leave it to the wake (no busy loop on a sleeping console).
      expect(distinctPendingHosts(useUploadQueueStore.getState().items, { includeWaitingForWake: false })).toEqual([]);
    });

    it("a recoverable failure while the console reads rest parks too", async () => {
      addItem("A");
      // helper_not_ava1 is fatal in the browser build on its own; asleep, it is a wait.
      mockedJobStatus.mockResolvedValue({
        status: "failed",
        error: "nothing answers",
        error_reason: build === "browser" ? "helper_not_ava1" : undefined,
      } as Snap);
      mockedStartFile.mockImplementation(async () => {
        setPower("rest");
        return "job";
      });

      const p = useUploadQueueStore.getState().start();
      await vi.advanceTimersByTimeAsync(60_000);
      await p;

      expect(row()).toMatchObject({ status: "pending", waitingForWake: true });
      expect(mockedStartFile).toHaveBeenCalledTimes(1);
    });

    it("a console already asleep parks the row before sending anything", async () => {
      setPower("rest");
      addItem("A");
      const p = useUploadQueueStore.getState().start();
      await vi.advanceTimersByTimeAsync(5_000);
      await p;
      expect(row()).toMatchObject({ status: "pending", waitingForWake: true });
      expect(mockedStartFile).not.toHaveBeenCalled();
      expect(mockedEnsure).not.toHaveBeenCalled();
    });

    it("resumes on the helper-up edge after the wake", async () => {
      setPower("rest");
      addItem("A");
      await useUploadQueueStore.getState().start();
      expect(row().waitingForWake).toBe(true);

      // Awake again, helper back: the poller's up edge calls this.
      setPower("awake");
      mockedJobStatus.mockResolvedValue({ status: "done", bytes_sent: 10 } as Snap);
      const n = useUploadQueueStore.getState().resumeFailedRecoverable(HOST);
      await vi.advanceTimersByTimeAsync(10_000);
      expect(await n).toBe(1);
      expect(row()).toMatchObject({ status: "done", waitingForWake: false });
      expect(mockedStartFile).toHaveBeenCalledTimes(1);
    });
  });
}
