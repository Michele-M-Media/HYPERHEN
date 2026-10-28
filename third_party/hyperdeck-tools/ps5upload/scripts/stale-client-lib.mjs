// What `make run-client` may clean up before starting the dev app (see kill-stale-client.mjs).
//
// Only an engine holding the desktop app's own port is stale. A blanket
// `pkill -f ps5upload-engine` also killed engines that had nothing to do with the
// dev app — a standalone one on another port running a 70-minute conversion
// died mid-build. An engine whose app is gone exits by itself (its parent watch),
// so the port holder is the only one that can be in the way.

/** The engine's port the desktop app binds first. */
export const APP_ENGINE_PORT = 19113;

/** The executable is the engine itself, not a command that merely mentions it. */
function isEngine(command) {
  const exe = command.trim().split(/\s+(?=-)/)[0];
  return /(^|[\\/])ps5upload-engine(\.exe)?$/i.test(exe);
}

/** Of the processes listening on the app's port, the pids that are a ps5upload engine. */
export function staleEnginePids(listeners) {
  return listeners.filter((l) => isEngine(l.command)).map((l) => l.pid);
}
