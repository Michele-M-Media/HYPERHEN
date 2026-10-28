// The console operations a dump swap (lib/dumpSwap.ts) runs on, bound to one PS5.

import {
  appUnregister,
  fsDelete,
  fsListDir,
  fsMkdir,
  fsMove,
  fsPathExists,
  fsReadPreview,
  fsWriteText,
} from "../api/ps5";
import { pkgLibraryStore } from "../state/pkgLibrary";
import { mgmtAddr, transferAddr } from "./addr";
import type { SwapDeps } from "./dumpSwap";
import { fetchRunningGames } from "./runningGames";

function decodeBase64Text(b64: string): string {
  const bin = atob(b64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return new TextDecoder().decode(bytes);
}

export function consoleSwapDeps(host: string): SwapDeps {
  const tx = transferAddr(host);
  const mgmt = mgmtAddr(host);
  return {
    exists: (p) => fsPathExists(tx, p),
    mkdir: async (p) => {
      if (!(await fsPathExists(tx, p))) await fsMkdir(tx, p);
    },
    move: (a, b) => fsMove(tx, a, b),
    writeText: async (p, t) => {
      const r = await fsWriteText(mgmt, p, t);
      if (!r.ok) throw new Error(`write ${p}: ${r.err ?? "refused"}`);
    },
    readText: async (p) => {
      if (!(await fsPathExists(tx, p))) return null;
      const r = await fsReadPreview(mgmt, p, 256 * 1024);
      return decodeBase64Text(r.base64);
    },
    remove: async (p) => {
      if (await fsPathExists(tx, p)) await fsDelete(tx, p);
    },
    list: async (dir) => (await fsListDir(tx, dir)).map((e) => e.name),
    isRunning: async (titleId) => (await fetchRunningGames(mgmt)).has(titleId),
    // A dump SMP or ps5upload registered has an app folder; appUnregister removes it (and
    // leaves the saves, measured on FW 5.10).
    isRegistered: (titleId) => fsPathExists(tx, `/user/app/${titleId}`),
    unregister: (titleId) => appUnregister(tx, titleId),
    // Through the console queue, like every install; Stream & install is the reliable route.
    install: (packagePath) => pkgLibraryStore(host).getState().installStream(packagePath, host),
    sleep: (ms) => new Promise((r) => setTimeout(r, ms)),
    now: () => Date.now(),
  };
}
