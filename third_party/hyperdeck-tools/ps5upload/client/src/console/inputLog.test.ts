import { describe, expect, it, vi } from "vitest";
import {
  createInputLog,
  describeEvent,
  INPUT_LOG_MAX_BODY,
  INPUT_LOG_MAX_QUEUE,
  takeBatch,
} from "./inputLog";

const target = (tagName: string, attrs: Record<string, string> = {}, className = "") => ({
  tagName,
  className,
  type: attrs.type,
  isContentEditable: false,
  getAttribute: (a: string) => (a in attrs ? attrs[a] : null),
});

/** What the helper accepts in a line (webui_inputlog.c): printable ASCII without quote or backslash. */
const SAFE = /^[\x20-\x7e]+$/;

describe("describeEvent", () => {
  it("logs a key's key, code and keyCode and the element's kind", () => {
    const line = describeEvent(
      { type: "keydown", key: "Enter", code: "Enter", keyCode: 13, target: target("BUTTON", {}, "cw-focusable cw-rail-item") },
      1234,
    );
    expect(line).toBe("1234 keydown k=Enter c=Enter kc=13 el=button.cw-focusable");
  });

  it("never logs what is typed into a text field", () => {
    const line = describeEvent(
      { type: "keydown", key: "s", code: "KeyS", keyCode: 83, target: target("INPUT", { type: "text" }) },
      5,
    );
    expect(line).toBe("5 keydown k=char c=char kc=0 el=input");
  });

  it("logs a pointer's button and position, rounded", () => {
    const line = describeEvent(
      { type: "click", button: 0, clientX: 10.6, clientY: 20.2, detail: 1, target: target("DIV") },
      9,
    );
    expect(line).toBe("9 click b=0 x=11 y=20 d=1 el=div");
  });

  it("logs no file names: data attributes and text are not read", () => {
    const line = describeEvent(
      { type: "mousedown", button: 0, clientX: 1, clientY: 2, detail: 1, target: target("BUTTON", { "data-entry": "secret.pkg", "aria-label": "secret.pkg" }, "cw-file-main") },
      1,
    )!;
    expect(line).not.toContain("secret");
    expect(line).toMatch(SAFE);
  });

  it("odd key names are reduced to the characters the helper accepts", () => {
    const line = describeEvent({ type: "keyup", key: 'A"\\b c', code: "", keyCode: 0, target: null }, 1)!;
    expect(line).toMatch(SAFE);
    expect(line).not.toMatch(/["\\]/);
  });
});

describe("takeBatch", () => {
  it("takes lines from the front while the JSON body stays within 4 KiB", () => {
    const queue = Array.from({ length: 200 }, (_, i) => `${i} keydown k=ArrowDown c=ArrowDown kc=40 el=button.cw-focusable`);
    const batch = takeBatch(queue);
    expect(JSON.stringify({ events: batch }).length).toBeLessThanOrEqual(INPUT_LOG_MAX_BODY);
    expect(batch[0]).toMatch(/^0 /);
    expect(queue.length + batch.length).toBe(200);
    expect(queue[0]).toBe(`${batch.length} keydown k=ArrowDown c=ArrowDown kc=40 el=button.cw-focusable`);
  });
});

describe("createInputLog", () => {
  function setup() {
    vi.useFakeTimers();
    let now = 0;
    const sent: string[][] = [];
    const log = createInputLog({
      send: (lines) => {
        sent.push(lines);
        return Promise.resolve();
      },
      now: () => now,
    });
    return {
      log,
      sent,
      advance: async (ms: number) => {
        now += ms;
        await vi.advanceTimersByTimeAsync(ms);
      },
    };
  }

  it("sends nothing while disabled (the default)", async () => {
    const { log, sent, advance } = setup();
    log.record({ type: "keydown", key: "Enter", code: "Enter", keyCode: 13, target: null });
    await advance(5000);
    expect(sent).toEqual([]);
    vi.useRealTimers();
  });

  it("batches events and sends at most one request a second", async () => {
    const { log, sent, advance } = setup();
    log.setEnabled(true);
    for (let i = 0; i < 3; i++) log.record({ type: "keydown", key: "ArrowDown", code: "ArrowDown", keyCode: 40, target: null });
    await advance(10);
    expect(sent.length).toBe(1);
    expect(sent[0].length).toBe(3);
    log.record({ type: "keyup", key: "ArrowDown", code: "ArrowDown", keyCode: 40, target: null });
    await advance(500);
    expect(sent.length).toBe(1); // too soon after the last request
    await advance(600);
    expect(sent.length).toBe(2);
    log.setEnabled(false);
    log.record({ type: "keyup", key: "x", code: "KeyX", keyCode: 88, target: null });
    await advance(3000);
    expect(sent.length).toBe(2);
    vi.useRealTimers();
  });

  it("keeps a bounded queue and says how many events it dropped", async () => {
    const { log, sent, advance } = setup();
    log.setEnabled(true);
    for (let i = 0; i < INPUT_LOG_MAX_QUEUE + 50; i++)
      log.record({ type: "keydown", key: "ArrowDown", code: "ArrowDown", keyCode: 40, target: null });
    await advance(30_000);
    const lines = sent.flat();
    expect(lines.length).toBe(INPUT_LOG_MAX_QUEUE + 1);
    expect(lines[lines.length - 1]).toMatch(/ dropped n=50$/);
    for (const batch of sent) expect(JSON.stringify({ events: batch }).length).toBeLessThanOrEqual(INPUT_LOG_MAX_BODY);
    vi.useRealTimers();
  });

  it("samples pointer moves (at most four a second)", async () => {
    const { log, sent, advance } = setup();
    log.setEnabled(true);
    for (let i = 0; i < 100; i++) {
      log.record({ type: "mousemove", button: 0, clientX: i, clientY: 0, detail: 0, target: null });
      await advance(10);
    }
    await advance(2000);
    const moves = sent.flat().filter((l) => l.includes(" mousemove "));
    expect(moves.length).toBeLessThanOrEqual(5);
    expect(moves.length).toBeGreaterThanOrEqual(3);
    vi.useRealTimers();
  });

  it("sends an event recorded just before stop (○ closing Controls) as one final batch", async () => {
    vi.useFakeTimers();
    let now = 0;
    const sent: { lines: string[]; final?: boolean }[] = [];
    const log = createInputLog({
      send: (lines, final) => {
        sent.push({ lines, final });
        return Promise.resolve();
      },
      now: () => now,
    });
    log.setEnabled(true);
    log.record({ type: "keydown", key: "Enter", code: "Enter", keyCode: 13, target: null });
    now += 10;
    await vi.advanceTimersByTimeAsync(10);
    expect(sent.length).toBe(1);
    // Within the second after that send: the paced flush would wait, the stop sends at once.
    now += 100;
    log.record({ type: "popstate", target: null });
    log.setEnabled(false);
    expect(sent.length).toBe(2);
    expect(sent[1].final).toBe(true);
    expect(sent[1].lines).toEqual(["110 popstate"]);
    now += 5000;
    await vi.advanceTimersByTimeAsync(5000);
    expect(sent.length).toBe(2);

    // Page hidden while on: the queue goes out now, and logging carries on.
    log.setEnabled(true);
    log.record({ type: "keydown", key: "Escape", code: "Escape", keyCode: 27, target: null });
    log.flushNow();
    expect(sent.length).toBe(3);
    expect(sent[2].final).toBe(true);
    expect(log.enabled()).toBe(true);
    vi.useRealTimers();
  });

  it("the final batch passes the helper's 500 ms gate and never overtakes a regular send", async () => {
    vi.useFakeTimers();
    let now = 0;
    // The helper's rule (webui_inputlog_gate_admit): 500 ms between admitted requests, except one
    // request marked final per 500 ms window.
    const gate = { last: -1e6, lastFinal: -1e6 };
    const admit = (final: boolean) => {
      if (now - gate.last >= 500) {
        gate.last = now;
        return true;
      }
      if (final && now - gate.lastFinal >= 500) {
        gate.last = gate.lastFinal = now;
        return true;
      }
      return false;
    };
    const stored: string[] = [];
    const order: string[] = [];
    let releaseRegular: (() => void) | null = null;
    const log = createInputLog({
      send: (lines, final) => {
        order.push(final ? "final" : "regular");
        const arrive = () => {
          if (admit(!!final)) stored.push(...lines);
        };
        if (final) {
          arrive();
          return Promise.resolve();
        }
        // A regular send that is still on the wire when Controls closes.
        return new Promise<void>((res) => {
          releaseRegular = () => {
            arrive();
            res();
          };
        });
      },
      now: () => now,
    });
    log.setEnabled(true);
    log.record({ type: "keydown", key: "Enter", code: "Enter", keyCode: 13, target: null });
    await vi.advanceTimersByTimeAsync(0);
    expect(order).toEqual(["regular"]);
    now += 120;
    log.record({ type: "popstate", target: null });
    log.setEnabled(false);
    // Not sent alongside: it waits for the regular one.
    expect(order).toEqual(["regular"]);
    now += 30;
    releaseRegular!();
    await vi.advanceTimersByTimeAsync(0);
    expect(order).toEqual(["regular", "final"]);
    // Both admitted although the final came 0 ms after the regular one; ○'s popstate is kept.
    expect(stored.map((l) => l.split(" ")[1])).toEqual(["keydown", "popstate"]);

    // Without the final mark the helper would have refused it (the bug this guards against).
    gate.last = now;
    expect(admit(false)).toBe(false);
    vi.useRealTimers();
  });
});
