import assert from "node:assert/strict";
import { test } from "node:test";

import { staleEnginePids } from "./stale-client-lib.mjs";

test("only the engine holding the app's port is stale", () => {
  const listeners = [
    { pid: 10, command: "/Users/me/Library/Application Support/com.phantomptr.ps5upload/engine/ps5upload-engine" },
    { pid: 11, command: "/usr/bin/python3 -m http.server 19113" },
  ];
  assert.deepEqual(staleEnginePids(listeners), [10]);
});

test("an engine elsewhere — a conversion on another port — is never in the list to kill", () => {
  // The caller passes only what listens on the app's port; nothing else can come back.
  assert.deepEqual(staleEnginePids([]), []);
});

test("a name that merely contains the words is not the engine", () => {
  assert.deepEqual(staleEnginePids([{ pid: 12, command: "vim notes-about-ps5upload-engine.txt" }]), []);
});
