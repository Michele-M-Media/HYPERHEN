/**
 * Drag a file from Files onto this computer's desktop or file manager.
 *
 * The OS drag can only carry a file that exists here, so the first drag of a
 * file downloads it into a staging folder (commands/dragout.rs) and, if the
 * mouse button is still held when the download finishes, starts the OS drag
 * with that copy. If the button was released first, the copy is kept and a
 * notice says to drag again: the second drag is immediate. Desktop only.
 */
import { invoke, Channel } from "@tauri-apps/api/core";

import { jobStatus, startTransferDownload } from "../api/ps5";
import { pollFsDownload } from "../state/fsDownloadPoll";
import { pushNotification } from "../state/notifications";
import { log } from "../state/logs";
import type { Translator } from "../state/lang";
import { isTauriEnv } from "./tauriEnv";
import { isAndroid, isIOS } from "./platform";

/** Files above this are not staged for a drag; the right-click Download is for those. */
export const DRAG_OUT_MAX_BYTES = 4 * 1024 * 1024 * 1024;

/** The app icon, shown under the cursor while dragging. */
const DRAG_IMAGE =
  "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAADeElEQVR42s1XXUsUURh+ztkZ3dU1W/zC9asolcCIbryLgi7LCgy8KOwu6B/0I4KugqArL4OEjCKyC6ErQxLCQF0DoxU/tnV1cd1dd3bm9J5xRmZH92NWKV8YzuzsOed53u9zFFQghmEwVCGcc1FuTtGNhRA+cwJjOo4h5fZhRbRlxN44SQvQHlzycVuFucC5DUzvd4j1I3rto0eVi0tZzK24NVejJ0JWGKN937kxCghILSU7y2Sv6RnGyco4PSPSFTaWm4DJjAi8scA1639+TGDDsoi04jgRuO+0ArMDxWImzT5hgavwYG+U94+5J2Hdle6wMQu0s3yOSn1Nm0A3hDmZOYiUyjgHxn6gulKkz/m9dG0Qch18nOHVosBEdB8hX5wFd2LYmIodEFaqqZWnGMPv1SSi8RyefmlBqFVgqJNBYWXTRXViKkXcWVbmIjGsbWzhw7cUaraC4P5abGQZ2gPSNVLDsiFzpKlL+t4Q+2sjy3HEt1MYvBrGwuwsgvFfuB7MmeB5im3OKq++3lLM4p7Jamg8UwNNM5BIbGN3YwOxZNrMN4XSW3ipltUkdk7LI5POyroKf/85+NJxcFXBjlZFua6GgK7ryOY0GLoB8SeBzjaV/K7Cp+seqnUVBLjl3OnvUUhDJ1NZdCg7CPX0YufnEoJ+w1PD8ETADsCv81v4NDWLgJLDWQK8eO02Pk4u4GZ43cxiIYQnCyiV9/V91WZWGOp6BxHZbMCzySTeTkzj3kAKo8MjVTUK7okAya0rlGv+MF6+j4HFl/DicRPGno+iM9xias+pAHixAnNWJZJ5+tZvdTDubu4F+Wjs0YxaE0zTdCiKTx5AynVFOWGR1lwqVgmP1FxWtdnlDLZSOur8PmQ1hu5QHkE1jYZgELlcDsH6wAHj1dgmwq1NVcVAUduNT60gnhKor2VEQKArsIpQTQLR9W3sEYGOtmaks3tI7WbQ3dGGJw+HTOuwwzVZlCJwaLZcLzPgwY0mtDTW4vPMGgI1HCprRjKxh9aQjvNd7dSWDeymM9DyOi70dFRcipmrv8/RMOCOgRMSe88fZJXLBVlgH51ln3FMPnT4ENY5QFrEHs3KSNqbv83ROPh+BIEDDBuTu0DGisWC9CWzqqFMNXuU4qPoN3+bIz/4fpTvHRg4PYfSU3Es/+8Xk1NxNTsVl9N/fT3/C/bj8sGHN4+TAAAAAElFTkSuQmCC";

/** Whether this build can drag files out of the window at all. */
export function dragOutAvailable(): boolean {
  return isTauriEnv() && !isAndroid() && !isIOS();
}

type Staged = { local: string } | { promise: Promise<string | null> };
const staged = new Map<string, Staged>();

/** Is the primary mouse button down right now? Tracked so a finished download
 *  knows whether the user is still holding the drag. */
let mouseHeld = false;
if (typeof window !== "undefined") {
  window.addEventListener("mousedown", (e) => { if (e.button === 0) mouseHeld = true; }, true);
  window.addEventListener("mouseup", () => { mouseHeld = false; }, true);
  window.addEventListener("blur", () => { mouseHeld = false; });
}

function stageKey(addr: string, remotePath: string): string {
  return `${addr}|${remotePath}`;
}

/** Start the OS drag with a local file. Resolves when the drop finishes or is cancelled. */
async function startOsDrag(local: string): Promise<void> {
  await invoke("plugin:drag|start_drag", {
    item: [local],
    image: DRAG_IMAGE,
    onEvent: new Channel<unknown>(),
  });
}

/** Download `remotePath` (a file) into the staging folder; resolves to the local path. */
async function stage(
  addr: string,
  remotePath: string,
  unsafeRead: boolean,
): Promise<string | null> {
  const dir = remotePath.slice(0, remotePath.lastIndexOf("/")) || "/";
  const name = remotePath.slice(remotePath.lastIndexOf("/") + 1);
  const destDir = await invoke<string>("dragout_stage_dir", { key: stageKey(addr, dir) });
  const jobId = await startTransferDownload(remotePath, destDir, addr, "file", unsafeRead);
  let failed: string | null = null;
  await pollFsDownload(
    jobId,
    {
      isLive: () => true,
      onProgress: () => {},
      onLostContact: () => {},
      onDone: () => {},
      onFailed: (e) => { failed = e; },
    },
    { jobStatus: (id) => jobStatus(id) },
  );
  if (failed) throw new Error(failed);
  const sep = destDir.includes("\\") ? "\\" : "/";
  return destDir + sep + name;
}

/**
 * Called from a row's dragstart (after preventDefault). Never throws: every
 * outcome is told through a notification.
 */
export function dragOut(
  input: { addr: string; remotePath: string; size: number; unsafeRead: boolean },
  tr: Translator,
): void {
  if (!dragOutAvailable()) return;
  const name = input.remotePath.slice(input.remotePath.lastIndexOf("/") + 1);
  if (input.size > DRAG_OUT_MAX_BYTES) {
    pushNotification(
      "info",
      tr("fs_dragout_too_big", { name }, `"${name}" is too large to drag out`),
      { body: tr("fs_dragout_too_big_body", undefined, "Files over 4 GB: right-click → Download instead.") },
    );
    return;
  }
  const key = stageKey(input.addr, input.remotePath);
  const have = staged.get(key);
  if (have && "local" in have) {
    void startOsDrag(have.local).catch((e) => {
      staged.delete(key);
      log.warn("files", `drag out of ${name} failed: ${e instanceof Error ? e.message : String(e)}`);
    });
    return;
  }
  if (have) return; // already being fetched for a previous drag
  pushNotification(
    "info",
    tr("fs_dragout_preparing", { name }, `Fetching "${name}" for the drag…`),
    { body: tr("fs_dragout_preparing_body", undefined, "Keep holding the mouse button, or drag it again once it is ready.") },
  );
  const promise = stage(input.addr, input.remotePath, input.unsafeRead)
    .then(async (local) => {
      if (!local) return null;
      staged.set(key, { local });
      if (mouseHeld) {
        await startOsDrag(local);
      } else {
        pushNotification(
          "success",
          tr("fs_dragout_ready", { name }, `"${name}" is ready — drag it again`),
        );
      }
      return local;
    })
    .catch((e) => {
      staged.delete(key);
      const msg = e instanceof Error ? e.message : String(e);
      log.warn("files", `drag out of ${name} failed: ${msg}`);
      pushNotification(
        "error",
        tr("fs_dragout_failed", { name }, `Couldn't fetch "${name}" for the drag`),
        { body: msg },
      );
      return null;
    });
  staged.set(key, { promise });
}
