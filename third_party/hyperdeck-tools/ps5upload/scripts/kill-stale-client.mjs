#!/usr/bin/env node
// Best-effort cleanup of a stale ps5upload-desktop, an engine still holding the
// app's port (19113), and a Vite dev server holding :1420. Engines elsewhere are
// left alone (see stale-client-lib.mjs). Invoked as a `make run-client`
// dependency so a previously mis-terminated session doesn't fail the next
// launch with "Port 1420 already in use" or two engines fighting for ports.
//
// Cross-platform via native tooling:
//   - Linux/macOS: pkill, lsof, ps, kill (POSIX)
//   - Windows:     taskkill, netstat, PowerShell Get-CimInstance
//
// The POSIX branch uses the `[p]` regex bracket trick to keep pkill from
// matching its own argv. Without it, `pkill -f ps5upload-desktop` matches
// the calling shell's `/proc/<pid>/cmdline` (which contains the literal
// substring) and SIGTERMs the make recipe — see Makefile _kill-stale-client.

import { execSync } from 'node:child_process';

import { APP_ENGINE_PORT, staleEnginePids } from './stale-client-lib.mjs';

const isWin = process.platform === 'win32';
const PORT = 1420;

function run(cmd) {
  return execSync(cmd, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] });
}

function silent(cmd) {
  try { execSync(cmd, { stdio: 'ignore' }); } catch { /* expected: no match */ }
}

// The dev app itself. Its engine exits with it (the engine's parent watch); an
// engine is NOT killed by name here — see stale-client-lib.mjs.
if (isWin) {
  silent('taskkill /IM ps5upload-desktop.exe /F');
} else {
  silent("pkill -f '[p]s5upload-desktop'");
}

function pidsOnPort(port) {
  try {
    if (isWin) {
      const out = run('netstat -ano -p tcp');
      const pids = new Set();
      const re = new RegExp(`\\s\\S+:${port}\\s+\\S+\\s+LISTENING\\s+(\\d+)`);
      for (const line of out.split(/\r?\n/)) {
        const m = line.match(re);
        if (m) pids.add(m[1]);
      }
      return [...pids];
    }
    const out = run(`lsof -ti tcp:${port} -sTCP:LISTEN`);
    return out.trim().split(/\s+/).filter(Boolean);
  } catch {
    return [];
  }
}

function looksLikeOurVite(pid) {
  try {
    if (isWin) {
      const out = run(
        `powershell -NoProfile -Command "(Get-CimInstance Win32_Process -Filter 'ProcessId=${pid}').CommandLine"`,
      );
      return /ps5upload[\s\S]*vite/i.test(out);
    }
    const out = run(`ps -o command= -p ${pid}`);
    return /ps5upload.*node_modules.*vite/.test(out);
  } catch {
    return false;
  }
}

function commandOf(pid) {
  try {
    if (isWin) {
      return run(
        `powershell -NoProfile -Command "(Get-CimInstance Win32_Process -Filter 'ProcessId=${pid}').CommandLine"`,
      ).trim();
    }
    return run(`ps -o command= -p ${pid}`).trim();
  } catch {
    return '';
  }
}

// Only an engine still holding the app's port (an orphan the app can't bind past).
const listeners = pidsOnPort(APP_ENGINE_PORT).map((pid) => ({ pid, command: commandOf(pid) }));
for (const pid of staleEnginePids(listeners)) {
  silent(isWin ? `taskkill /PID ${pid} /F` : `kill ${pid}`);
  console.log(`✓ stopped the stale engine on :${APP_ENGINE_PORT} (pid ${pid})`);
}

for (const pid of pidsOnPort(PORT)) {
  if (looksLikeOurVite(pid)) {
    silent(isWin ? `taskkill /PID ${pid} /F` : `kill -9 ${pid}`);
    console.log(`✓ killed stale Vite on :${PORT} (pid ${pid})`);
  }
}

// Brief settle so freed sockets transition out of TIME_WAIT before tauri dev
// reopens the port. Equivalent to the previous `sleep 1` in the Makefile.
await new Promise((resolve) => setTimeout(resolve, 1000));
