import { afterEach, describe, expect, it, vi } from "vitest";

import { sleep, wakeSleepers } from "./wakeableSleep";

describe("wakeableSleep", () => {
  afterEach(() => {
    vi.useRealTimers();
  });

  it("resolves on its own timer like a plain sleep", async () => {
    vi.useFakeTimers();
    let done = false;
    void sleep(500).then(() => (done = true));
    await vi.advanceTimersByTimeAsync(499);
    expect(done).toBe(false);
    await vi.advanceTimersByTimeAsync(1);
    expect(done).toBe(true);
  });

  it("a host tick ends a sleep whose time is up even when its timer is held back", async () => {
    // A hidden window's timer can fire a minute late; the host's clock stands in.
    let done = false;
    void sleep(500).then(() => (done = true));
    wakeSleepers(undefined, Date.now() + 100);
    await Promise.resolve();
    expect(done).toBe(false);
    wakeSleepers(undefined, Date.now() + 600);
    await Promise.resolve();
    expect(done).toBe(true);
  });

  it("a finished job ends poll sleeps at once and leaves settle delays alone", async () => {
    let poll = false;
    let settle = false;
    void sleep(500, { poll: true }).then(() => (poll = true));
    void sleep(1500).then(() => (settle = true));
    wakeSleepers({ polls: true });
    await Promise.resolve();
    expect(poll).toBe(true);
    expect(settle).toBe(false);
    wakeSleepers(undefined, Date.now() + 2000);
    await Promise.resolve();
    expect(settle).toBe(true);
  });
});
