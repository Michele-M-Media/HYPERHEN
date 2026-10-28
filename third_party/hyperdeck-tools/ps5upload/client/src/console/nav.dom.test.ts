import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import {
  createEchoGuard,
  DEFAULT_INPUT_MAP,
  dispatchNav,
  focusElement,
  FOCUSABLE,
  installNavigation,
  onNavAction,
} from "./nav";

/*
 * Vitest runs in node (no jsdom in the client): a minimal fake DOM with just
 * what nav.ts touches — querySelectorAll for [data-nav-modal] and focusables,
 * contains/closest, focus/click, getBoundingClientRect, activeElement.
 */
class FakeEl {
  parent: FakeEl | null = null;
  children: FakeEl[] = [];
  clicks = 0;
  constructor(
    readonly name: string,
    readonly attrs: Record<string, string> = {},
    readonly box = { left: 0, top: 0, width: 0, height: 0 },
    readonly tagName = "DIV",
  ) {}
  add(...kids: FakeEl[]): this {
    for (const k of kids) {
      k.parent = this;
      this.children.push(k);
    }
    return this;
  }
  remove(): void {
    if (!this.parent) return;
    this.parent.children = this.parent.children.filter((c) => c !== this);
    this.parent = null;
  }
  contains(o: FakeEl | null): boolean {
    for (let n = o; n; n = n.parent) if (n === this) return true;
    return false;
  }
  descendants(): FakeEl[] {
    return this.children.flatMap((c) => [c, ...c.descendants()]);
  }
  querySelectorAll(sel: string): FakeEl[] {
    if (sel === "[data-nav-modal]") return this.descendants().filter((e) => "data-nav-modal" in e.attrs);
    if (sel === "[data-nav-region]") return this.descendants().filter((e) => "data-nav-region" in e.attrs);
    if (sel === '[data-nav-region="rail"]') return this.descendants().filter((e) => e.attrs["data-nav-region"] === "rail");
    return this.descendants().filter((e) => e.tagName === "BUTTON" || e.tagName === "INPUT"); // the FOCUSABLE selector
  }
  closest(sel: string): FakeEl | null {
    if (sel === FOCUSABLE) {
      if (this.tagName === "BUTTON" || this.tagName === "INPUT") return this;
      return this.parent ? this.parent.closest(sel) : null;
    }
    if (sel !== "[data-nav-region]") return null; // nothing is inert or aria-hidden in these fixtures
    if ("data-nav-region" in this.attrs) return this;
    return this.parent ? this.parent.closest(sel) : null;
  }
  hasAttribute(a: string): boolean {
    return a in this.attrs;
  }
  getAttribute(a: string): string | null {
    return a in this.attrs ? this.attrs[a] : null;
  }
  getBoundingClientRect(): DOMRect {
    const { left, top, width, height } = this.box;
    return { left, top, width, height, right: left + width, bottom: top + height, x: left, y: top } as DOMRect;
  }
  focusOpts: unknown = undefined;
  type = "text";
  scrollTop = 0;
  focus(opts?: unknown): void {
    this.focusOpts = opts;
    fakeDoc.activeElement = this;
  }
  click(): void {
    this.clicks++;
  }
}

const btn = (name: string, left: number, top: number) =>
  new FakeEl(name, {}, { left, top, width: 100, height: 40 }, "BUTTON");

const body = new FakeEl("body");
const fakeDoc = {
  body,
  activeElement: body as FakeEl,
  querySelectorAll: (sel: string) => body.querySelectorAll(sel),
};

let railHome: FakeEl;
let railFiles: FakeEl;
let modal: FakeEl;
let reload: FakeEl;
const offs: (() => void)[] = [];

beforeEach(() => {
  body.children = [];
  railHome = btn("rail-home", 0, 100);
  railFiles = btn("rail-files", 0, 160);
  reload = btn("reload", 600, 100);
  modal = new FakeEl("banner", { "data-nav-modal": "" }).add(reload);
  body.add(railHome, railFiles);
  (globalThis as any).document = fakeDoc;
  // The page's own handler, like App's: ○ / L1 / R1 move focus to the rail.
  offs.push(
    onNavAction((a) => {
      if (a === "back" || a === "prevSection" || a === "nextSection") {
        railFiles.focus();
        return true;
      }
      return false;
    }),
  );
});

afterEach(() => {
  while (offs.length) offs.pop()!();
  delete (globalThis as any).document;
  delete (globalThis as any).window;
  delete (globalThis as any).requestAnimationFrame;
  delete (globalThis as any).cancelAnimationFrame;
  vi.unstubAllGlobals();
  vi.restoreAllMocks();
});

describe("handler order", () => {
  it("a fallback handler runs after handlers registered later than it", () => {
    const seen: string[] = [];
    // App registers its ○ as a fallback; a section (Files: ○ = up one folder)
    // registered before it still runs first.
    offs.push(onNavAction(() => {
      seen.push("section");
      return true;
    }));
    offs.push(onNavAction(() => {
      seen.push("app");
      return true;
    }, undefined, { fallback: true }));
    railHome.focus();
    dispatchNav("back");
    expect(seen).toEqual(["section"]);
  });
});

describe("modal scope", () => {
  it("back, prevSection and nextSection outside the modal do not run while it is open", () => {
    body.add(modal);
    reload.focus();
    for (const a of ["back", "prevSection", "nextSection"] as const) {
      dispatchNav(a);
      expect(fakeDoc.activeElement).toBe(reload);
    }
  });

  it("the page's handlers run again once the modal closes", () => {
    body.add(modal);
    modal.remove();
    railHome.focus();
    dispatchNav("back");
    expect(fakeDoc.activeElement).toBe(railFiles);
  });

  it("a handler scoped to the modal does receive the actions", () => {
    body.add(modal);
    reload.focus();
    const seen: string[] = [];
    offs.push(
      onNavAction(
        (a) => {
          seen.push(a);
          return true;
        },
        () => modal as unknown as Element,
      ),
    );
    dispatchNav("back");
    expect(seen).toEqual(["back"]);
    expect(fakeDoc.activeElement).toBe(reload);
  });

  it("confirm activates the modal's button", () => {
    body.add(modal);
    reload.focus();
    dispatchNav("confirm");
    expect(reload.clicks).toBe(1);
  });

  it("confirm with focus left behind the modal brings focus in, never clicks the page", () => {
    railFiles.focus();
    body.add(modal);
    dispatchNav("confirm");
    expect(railFiles.clicks).toBe(0);
    expect(fakeDoc.activeElement).toBe(reload);
    dispatchNav("confirm");
    expect(reload.clicks).toBe(1);
  });

  it("D-pad moves stay inside the modal", () => {
    body.add(modal);
    reload.focus();
    dispatchNav("left"); // the rail is to the left, behind the modal
    expect(fakeDoc.activeElement).toBe(reload);
    dispatchNav("down");
    expect(fakeDoc.activeElement).toBe(reload);
  });
});

/*
 * The page's two regions, like App: the rail (left) and the content (right).
 * Rail items stack down the left; the content's controls sit to the right.
 */
function regions() {
  const rail = new FakeEl("rail", { "data-nav-region": "rail" }, { left: 0, top: 60, width: 120, height: 600 });
  const content = new FakeEl("content", { "data-nav-region": "content" }, { left: 130, top: 60, width: 900, height: 600 });
  const home = btn("home", 0, 100);
  const files = new FakeEl("files", { "data-nav-entry": "" }, { left: 0, top: 160, width: 100, height: 40 }, "BUTTON");
  const settings = btn("settings", 0, 520);
  rail.add(home, files, settings);
  body.add(rail, content);
  return { rail, content, home, files, settings };
}

describe("rail and content regions", () => {
  it("F1: Left from the content enters the rail on the active section's item, not the nearest", () => {
    const r = regions();
    const low = btn("low", 140, 520); // level with Settings
    r.content.add(btn("top", 140, 100), low);
    low.focus();
    dispatchNav("left");
    expect(fakeDoc.activeElement).toBe(r.files);
  });

  it("F1: Left inside the content still moves within the content when something is that way", () => {
    const r = regions();
    const a = btn("a", 140, 300);
    const b = btn("b", 260, 300);
    r.content.add(a, b);
    b.focus();
    dispatchNav("left");
    expect(fakeDoc.activeElement).toBe(a);
  });

  it("F1: a wide card's Left goes to the rail, not up to a narrower control above it", () => {
    const r = regions();
    const quick = btn("quick", 140, 100);
    const card = new FakeEl("card", {}, { left: 140, top: 300, width: 400, height: 120 }, "BUTTON");
    r.content.add(quick, card);
    card.focus();
    dispatchNav("left");
    expect(fakeDoc.activeElement).toBe(r.files);
  });

  it("F2: Down on the last rail item stays put, never jumps into the content", () => {
    const r = regions();
    r.content.add(btn("card", 140, 600));
    r.settings.focus();
    dispatchNav("down");
    expect(fakeDoc.activeElement).toBe(r.settings);
    dispatchNav("up");
    expect(fakeDoc.activeElement).toBe(r.files);
  });

  it("F2: Down at the end of the content stays put, never jumps to a rail item", () => {
    const r = regions();
    const first = btn("first", 140, 100);
    const last = btn("last", 140, 300);
    r.content.add(first, last);
    last.focus();
    dispatchNav("down"); // Settings (rail, y=520) is below
    expect(fakeDoc.activeElement).toBe(last);
    dispatchNav("up");
    expect(fakeDoc.activeElement).toBe(first);
    dispatchNav("up"); // Home (rail, y=100) is level; nothing above in the content
    expect(fakeDoc.activeElement).toBe(first);
  });

  it("F2: Right from the rail enters the content on its first control, not the nearest", () => {
    const r = regions();
    const first = btn("first", 140, 100);
    const nearSettings = btn("near", 140, 520);
    r.content.add(first, nearSettings);
    r.settings.focus();
    dispatchNav("right");
    expect(fakeDoc.activeElement).toBe(first);
  });

  it("F2: Right from the rail returns to the control last focused in the content", () => {
    const r = regions();
    const first = btn("first", 140, 100);
    const second = btn("second", 140, 300);
    r.content.add(first, second);
    second.focus();
    dispatchNav("left");
    expect(fakeDoc.activeElement).toBe(r.files);
    dispatchNav("down");
    expect(fakeDoc.activeElement).toBe(r.settings);
    dispatchNav("right");
    expect(fakeDoc.activeElement).toBe(second);
  });

  it("F2: the remembered control is gone (section changed) → the content's first control", () => {
    const r = regions();
    const first = btn("first", 140, 100);
    const second = btn("second", 140, 300);
    r.content.add(first, second);
    second.focus();
    dispatchNav("left");
    second.remove();
    dispatchNav("right");
    expect(fakeDoc.activeElement).toBe(first);
  });

  it("○ to the rail (App's handler) also remembers where the content was", () => {
    const r = regions();
    const first = btn("first", 140, 100);
    const second = btn("second", 140, 300);
    r.content.add(first, second);
    second.focus();
    offs.push(
      onNavAction((a) => {
        if (a !== "back") return false;
        focusElement(r.files as unknown as HTMLElement);
        return true;
      }),
    );
    dispatchNav("back");
    expect(fakeDoc.activeElement).toBe(r.files);
    dispatchNav("right");
    expect(fakeDoc.activeElement).toBe(second);
  });

  it("a modal inside the content keeps the D-pad in the modal, regions aside", () => {
    const r = regions();
    const ok = btn("ok", 400, 300);
    r.content.add(btn("behind", 140, 300), new FakeEl("dialog", { "data-nav-modal": "" }).add(ok));
    ok.focus();
    dispatchNav("left");
    expect(fakeDoc.activeElement).toBe(ok);
  });
});

describe("R12 gamepad/keyboard echo", () => {
  it("drops an Arrow keydown within 100 ms after the gamepad moved the same way", () => {
    const g = createEchoGuard();
    expect(g.keyAllowed("down", 0)).toBe(true);
    g.padFired("down", 1000);
    expect(g.keyAllowed("down", 1050)).toBe(false);
    expect(g.keyAllowed("up", 1050)).toBe(true);
    expect(g.keyAllowed("down", 1100)).toBe(true);
  });

  it("installNavigation moves once when one D-pad press arrives as both", () => {
    let keydown: ((e: any) => void) | null = null;
    (globalThis as any).window = {
      addEventListener: (t: string, fn: (e: any) => void) => {
        if (t === "keydown") keydown = fn;
      },
      removeEventListener: () => {},
    };
    let frame: (() => void) | null = null;
    (globalThis as any).requestAnimationFrame = (cb: () => void) => {
      frame = cb;
      return 1;
    };
    (globalThis as any).cancelAnimationFrame = () => {};
    const buttons = Array.from({ length: 17 }, () => ({ pressed: false }));
    vi.stubGlobal("navigator", { getGamepads: () => [{ index: 0, mapping: "standard", buttons }] });
    let now = 1000;
    vi.spyOn(performance, "now").mockImplementation(() => now);

    const below = btn("below", 0, 220);
    body.add(below);
    railHome.focus();
    offs.push(installNavigation(DEFAULT_INPUT_MAP));

    buttons[13].pressed = true; // D-pad down
    frame!();
    expect(fakeDoc.activeElement).toBe(railFiles);
    now = 1030;
    const echo = { key: "ArrowDown", target: null, preventDefault: vi.fn() };
    keydown!(echo);
    expect(echo.preventDefault).toHaveBeenCalled();
    expect(fakeDoc.activeElement).toBe(railFiles);

    buttons[13].pressed = false;
    frame!();
    now = 1300;
    keydown!({ key: "ArrowDown", target: null, preventDefault: vi.fn() });
    expect(fakeDoc.activeElement).toBe(below);
  });
});

/* A window that keeps every listener, so a test can fire keys and pointer events at it. */
function fakeWindow() {
  const listeners: Record<string, ((e: any) => void)[]> = {};
  (globalThis as any).window = {
    innerHeight: 700,
    addEventListener: (t: string, fn: (e: any) => void) => {
      (listeners[t] ??= []).push(fn);
    },
    removeEventListener: (t: string, fn: (e: any) => void) => {
      listeners[t] = (listeners[t] ?? []).filter((f) => f !== fn);
    },
  };
  (globalThis as any).requestAnimationFrame = () => 1;
  (globalThis as any).cancelAnimationFrame = () => {};
  vi.stubGlobal("navigator", {}); // no Gamepad API, like the PS5 browser
  const fire = (type: string, e: Record<string, unknown>) => {
    const ev = { preventDefault: vi.fn(), stopPropagation: vi.fn(), defaultPrevented: false, ...e };
    for (const fn of listeners[type] ?? []) fn(ev);
    return ev;
  };
  return {
    fire,
    key: (key: string, keyCode = 0) => fire("keydown", { key, keyCode, target: fakeDoc.activeElement }),
    move: (target: FakeEl, x: number, y: number) => fire("mousemove", { target, clientX: x, clientY: y }),
  };
}

describe("pointer = focus (the PS5 browser's left stick drives a pointer)", () => {
  it("hovering a control focuses it without scrolling", () => {
    const w = fakeWindow();
    offs.push(installNavigation(DEFAULT_INPUT_MAP));
    railHome.focus();
    w.move(railFiles, 50, 180);
    expect(fakeDoc.activeElement).toBe(railFiles);
    expect(railFiles.focusOpts).toEqual({ preventScroll: true });
  });

  it("hovering inside a control (its icon) focuses the control", () => {
    const w = fakeWindow();
    offs.push(installNavigation(DEFAULT_INPUT_MAP));
    const icon = new FakeEl("icon");
    railFiles.add(icon);
    w.move(icon, 50, 180);
    expect(fakeDoc.activeElement).toBe(railFiles);
  });

  it("hover outside the open modal never moves focus behind it", () => {
    const w = fakeWindow();
    offs.push(installNavigation(DEFAULT_INPUT_MAP));
    body.add(modal);
    reload.focus();
    w.move(railFiles, 50, 180);
    expect(fakeDoc.activeElement).toBe(reload);
  });

  it("Enter activates the focused control (once: the native activation is prevented)", () => {
    const w = fakeWindow();
    offs.push(installNavigation(DEFAULT_INPUT_MAP));
    w.move(railFiles, 50, 180);
    const ev = w.key("Enter", 13);
    expect(railFiles.clicks).toBe(1);
    expect(ev.preventDefault).toHaveBeenCalled();
  });

  it("an arrow after hovering continues from the hovered control", () => {
    const w = fakeWindow();
    offs.push(installNavigation(DEFAULT_INPUT_MAP));
    const below = btn("below", 0, 220);
    body.add(below);
    railHome.focus();
    w.move(railFiles, 50, 180);
    w.key("ArrowDown", 40);
    expect(fakeDoc.activeElement).toBe(below);
  });

  it("after a key move, pointer jitter (stick drift) does not take focus back; a real move does", () => {
    const w = fakeWindow();
    offs.push(installNavigation(DEFAULT_INPUT_MAP));
    w.move(railHome, 50, 120);
    w.key("ArrowDown", 40);
    expect(fakeDoc.activeElement).toBe(railFiles);
    w.move(railHome, 52, 121); // drift, still over Home
    expect(fakeDoc.activeElement).toBe(railFiles);
    w.move(railHome, 50, 120); // a page scroll re-sends the same spot
    expect(fakeDoc.activeElement).toBe(railFiles);
    w.move(railHome, 60, 130); // the stick really moved
    expect(fakeDoc.activeElement).toBe(railHome);
  });

  it("a pointer click after a key move activates the focused control, not the one under the pointer", () => {
    const w = fakeWindow();
    offs.push(installNavigation(DEFAULT_INPUT_MAP));
    w.move(railHome, 50, 120);
    w.key("ArrowDown", 40);
    const down = w.fire("mousedown", { target: railHome, clientX: 50, clientY: 120, button: 0 });
    expect(down.preventDefault).toHaveBeenCalled(); // focus does not jump to the pointer
    const click = w.fire("click", { target: railHome, clientX: 50, clientY: 120, detail: 1, isTrusted: true });
    expect(click.preventDefault).toHaveBeenCalled();
    expect(click.stopPropagation).toHaveBeenCalled();
    expect(railFiles.clicks).toBe(1);
    expect(fakeDoc.activeElement).toBe(railFiles);
  });

  it("a pointer click on the hovered control is left to the browser", () => {
    const w = fakeWindow();
    offs.push(installNavigation(DEFAULT_INPUT_MAP));
    w.move(railFiles, 50, 180);
    const click = w.fire("click", { target: railFiles, clientX: 50, clientY: 180, detail: 1, isTrusted: true });
    expect(click.preventDefault).not.toHaveBeenCalled();
    expect(railFiles.clicks).toBe(0); // the native click runs, not ours
  });

  it("hovering a text field never focuses it (the ring would stay on it, ✕ clicks elsewhere)", () => {
    const w = fakeWindow();
    offs.push(installNavigation(DEFAULT_INPUT_MAP));
    const field = new FakeEl("name", {}, { left: 300, top: 100, width: 300, height: 40 }, "INPUT");
    const ok = btn("ok", 300, 200);
    body.add(field, ok);
    railHome.focus();
    w.move(field, 350, 120);
    expect(fakeDoc.activeElement).toBe(railHome);
    w.move(ok, 350, 220);
    expect(fakeDoc.activeElement).toBe(ok);
  });

  it("a field typed into keeps focus against hover for a moment, then hover moves on", () => {
    const w = fakeWindow();
    let now = 1000;
    vi.spyOn(performance, "now").mockImplementation(() => now);
    offs.push(installNavigation(DEFAULT_INPUT_MAP));
    const field = new FakeEl("name", {}, { left: 300, top: 100, width: 300, height: 40 }, "INPUT");
    const ok = btn("ok", 300, 200);
    body.add(field, ok);
    field.focus(); // by ✕ or a key
    w.fire("keydown", { key: "a", keyCode: 65, target: field });
    now = 1500;
    w.move(ok, 350, 220);
    expect(fakeDoc.activeElement).toBe(field);
    now = 3000;
    w.move(ok, 360, 230);
    expect(fakeDoc.activeElement).toBe(ok);
  });

  it("a focused field nobody typed into lets hover move on", () => {
    const w = fakeWindow();
    offs.push(installNavigation(DEFAULT_INPUT_MAP));
    const field = new FakeEl("name", {}, { left: 300, top: 100, width: 300, height: 40 }, "INPUT");
    const ok = btn("ok", 300, 200);
    body.add(field, ok);
    field.focus();
    w.move(ok, 350, 220);
    expect(fakeDoc.activeElement).toBe(ok);
  });

  it("R18: unverified keys (F5, Home, PageUp/PageDown, ContextMenu) keep their browser default", () => {
    const w = fakeWindow();
    offs.push(installNavigation(DEFAULT_INPUT_MAP));
    const seen: string[] = [];
    offs.push(onNavAction((a) => {
      seen.push(a);
      return true;
    }));
    railHome.focus();
    for (const [k, code] of [["F5", 116], ["Home", 36], ["PageUp", 33], ["PageDown", 34], ["ContextMenu", 93], ["F1", 112]] as const) {
      const ev = w.key(k, code);
      expect(ev.preventDefault, k).not.toHaveBeenCalled();
    }
    expect(seen).toEqual([]);
    expect(fakeDoc.activeElement).toBe(railHome);
  });

  it("verified PageDown is left to a text field", () => {
    const w = fakeWindow();
    const map = DEFAULT_INPUT_MAP.map((b) => (b.button === "r2" ? { ...b, verified: true } : b));
    offs.push(installNavigation(map));
    const field = new FakeEl("name", {}, { left: 300, top: 100, width: 300, height: 40 }, "INPUT");
    body.add(field);
    field.focus();
    const ev = w.key("PageDown", 34);
    expect(ev.preventDefault).not.toHaveBeenCalled();
    expect(fakeDoc.activeElement).toBe(field);
  });

  it("keys are matched by keyCode when the browser names the key Unidentified", () => {
    const w = fakeWindow();
    offs.push(installNavigation(DEFAULT_INPUT_MAP));
    railHome.focus();
    w.key("Unidentified", 13);
    expect(railHome.clicks).toBe(1);
  });

  it("the wheel (right stick) is never taken over", () => {
    const w = fakeWindow();
    offs.push(installNavigation(DEFAULT_INPUT_MAP));
    const ev = w.fire("wheel", { target: railHome, deltaY: 120 });
    expect(ev.preventDefault).not.toHaveBeenCalled();
  });
});

describe("the other DualSense actions", () => {
  it("help, refresh, fullscreen and select reach the handlers", () => {
    const seen: string[] = [];
    offs.push(
      onNavAction((a) => {
        seen.push(a);
        return true;
      }),
    );
    railHome.focus();
    for (const a of ["help", "refresh", "fullscreen", "select", "actions"] as const) dispatchNav(a);
    expect(seen).toEqual(["help", "refresh", "fullscreen", "select", "actions"]);
  });

  it("R3 focuses the section rail at the active section", () => {
    const r = regions();
    const deep = btn("deep", 140, 500);
    r.content.add(btn("first", 140, 100), deep);
    deep.focus();
    dispatchNav("rail");
    expect(fakeDoc.activeElement).toBe(r.files);
  });

  it("L3 jumps to the top of the current region and scrolls it there", () => {
    const r = regions();
    const first = btn("first", 140, 100);
    const deep = btn("deep", 140, 500);
    r.content.add(first, deep);
    r.content.scrollTop = 400;
    deep.focus();
    dispatchNav("top");
    expect(fakeDoc.activeElement).toBe(first);
    expect(r.content.scrollTop).toBe(0);
  });

  it("R2/L2 move focus about a page down/up the current list", () => {
    (globalThis as any).window = { innerHeight: 300 };
    const r = regions();
    const rows = Array.from({ length: 20 }, (_, i) => btn(`row${i}`, 140, 100 + i * 50));
    r.content.add(...rows);
    rows[0].focus();
    dispatchNav("pageDown");
    // A page is 80 % of the 300 px viewport: 240 px, so 5 rows of 50 px.
    expect(fakeDoc.activeElement).toBe(rows[5]);
    dispatchNav("pageUp");
    expect(fakeDoc.activeElement).toBe(rows[0]);
  });

  it("a section handler may take R2 over (it returns true)", () => {
    const r = regions();
    const rows = Array.from({ length: 4 }, (_, i) => btn(`row${i}`, 140, 100 + i * 50));
    r.content.add(...rows);
    rows[0].focus();
    offs.push(onNavAction((a) => a === "pageDown"));
    dispatchNav("pageDown");
    expect(fakeDoc.activeElement).toBe(rows[0]);
  });
});
