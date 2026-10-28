import { describe, expect, it } from "vitest";

import { activateFocusTrap, trapTabKey, wrapTarget } from "./focusTrap";

// Vitest runs in node here (no jsdom), so the DOM is faked with the few
// members the helper reads: querySelectorAll, contains, focus, activeElement.
type Fake = { name: string; focused: () => boolean; focus: () => void };

function fakePanel(names: string[]) {
  const doc = { activeElement: null as unknown };
  const items = names.map((name) => {
    const el = {
      name,
      focus: () => {
        doc.activeElement = el;
      },
      focused: () => doc.activeElement === el,
      getClientRects: () => [1],
    };
    return el;
  });
  const panel = {
    ownerDocument: doc,
    querySelectorAll: () => items,
    contains: (n: unknown) => n === panel || items.includes(n as (typeof items)[number]),
    focus: () => {
      doc.activeElement = panel;
    },
  };
  return { doc, items: items as Fake[], panel: panel as unknown as HTMLElement };
}

function key(k: string, shiftKey = false) {
  const e = { key: k, shiftKey, prevented: false, preventDefault: () => {} };
  e.preventDefault = () => {
    e.prevented = true;
  };
  return e;
}

describe("wrapTarget", () => {
  const items = ["a", "b", "c"];

  it("wraps from the last item to the first going forwards", () => {
    expect(wrapTarget(items, "c", false, true)).toBe("a");
  });

  it("wraps from the first item to the last going backwards", () => {
    expect(wrapTarget(items, "a", true, true)).toBe("c");
  });

  it("leaves a move between two inner items to the browser", () => {
    expect(wrapTarget(items, "b", false, true)).toBeNull();
    expect(wrapTarget(items, "b", true, true)).toBeNull();
    expect(wrapTarget(items, "a", false, true)).toBeNull();
    expect(wrapTarget(items, "c", true, true)).toBeNull();
  });

  it("pulls focus that is outside the panel back to an end", () => {
    expect(wrapTarget(items, "elsewhere", false, false)).toBe("a");
    expect(wrapTarget(items, "elsewhere", true, false)).toBe("c");
  });

  it("sends Shift+Tab from the panel itself to the last item", () => {
    // The panel (tabIndex -1) holds focus when it has just opened with nothing
    // focusable, or after a click on its padding.
    expect(wrapTarget(items, "panel", true, true)).toBe("c");
    expect(wrapTarget(items, "panel", false, true)).toBeNull();
  });
});

describe("trapTabKey", () => {
  it("ignores every key but Tab", () => {
    const { panel, items } = fakePanel(["a", "b"]);
    items[1].focus();
    const e = key("Enter");
    expect(trapTabKey(e, panel)).toBe(false);
    expect(e.prevented).toBe(false);
    expect(items[1].focused()).toBe(true);
  });

  it("wraps Tab on the last control to the first", () => {
    const { panel, items } = fakePanel(["a", "b", "c"]);
    items[2].focus();
    const e = key("Tab");
    expect(trapTabKey(e, panel)).toBe(true);
    expect(e.prevented).toBe(true);
    expect(items[0].focused()).toBe(true);
  });

  it("wraps Shift+Tab on the first control to the last", () => {
    const { panel, items } = fakePanel(["a", "b", "c"]);
    items[0].focus();
    const e = key("Tab", true);
    expect(trapTabKey(e, panel)).toBe(true);
    expect(items[2].focused()).toBe(true);
  });

  it("does not interfere in the middle of the panel", () => {
    const { panel, items } = fakePanel(["a", "b", "c"]);
    items[1].focus();
    const e = key("Tab");
    expect(trapTabKey(e, panel)).toBe(false);
    expect(e.prevented).toBe(false);
    expect(items[1].focused()).toBe(true);
  });

  it("brings focus back when it has escaped to the page behind", () => {
    const { panel, items, doc } = fakePanel(["a", "b"]);
    doc.activeElement = { name: "behind" };
    const e = key("Tab");
    expect(trapTabKey(e, panel)).toBe(true);
    expect(items[0].focused()).toBe(true);
  });

  it("holds focus on the panel when it has nothing focusable", () => {
    const { panel, doc } = fakePanel([]);
    doc.activeElement = { name: "behind" };
    const e = key("Tab");
    expect(trapTabKey(e, panel)).toBe(true);
    expect(e.prevented).toBe(true);
    expect(doc.activeElement).toBe(panel);
  });
});

describe("activateFocusTrap", () => {
  function fakeWindow() {
    type Fn = (e: KeyboardEvent) => void;
    const listeners = new Set<Fn>();
    return {
      addEventListener: (_: string, fn: Fn) => void listeners.add(fn),
      removeEventListener: (_: string, fn: Fn) => void listeners.delete(fn),
      fire: (e: unknown) => [...listeners].forEach((fn) => fn(e as KeyboardEvent)),
      count: () => listeners.size,
    };
  }

  it("lets only the topmost of two stacked dialogs trap", () => {
    const win = fakeWindow();
    const under = fakePanel(["u1", "u2"]);
    const over = fakePanel(["o1", "o2"]);
    // One shared document, as in the app.
    (over.panel as unknown as { ownerDocument: unknown }).ownerDocument = under.doc;
    const focusIn = (el: Fake) => {
      under.doc.activeElement = el;
    };
    for (const el of over.items) el.focus = () => focusIn(el);

    const releaseUnder = activateFocusTrap(() => under.panel, win);
    const releaseOver = activateFocusTrap(() => over.panel, win);

    // Focus is on the confirm's last button; Tab must wrap inside the confirm,
    // and the modal beneath must not drag focus into itself.
    focusIn(over.items[1]);
    win.fire(key("Tab"));
    expect(under.doc.activeElement).toBe(over.items[0]);

    // Once the confirm closes, the modal beneath traps again.
    releaseOver();
    under.doc.activeElement = { name: "behind" };
    win.fire(key("Tab"));
    expect(under.doc.activeElement).toBe(under.items[0]);

    releaseUnder();
    expect(win.count()).toBe(0);
  });

  it("hands Escape only to the topmost of two stacked dialogs", () => {
    const win = fakeWindow();
    const closed: string[] = [];
    const releaseUnder = activateFocusTrap(() => null, win, {
      onEscape: () => closed.push("modal"),
    });
    const releaseOver = activateFocusTrap(() => null, win, {
      onEscape: () => closed.push("confirm"),
    });

    win.fire({ key: "Escape", defaultPrevented: false });
    expect(closed).toEqual(["confirm"]);

    // The confirm has gone; the next Escape is the modal's.
    releaseOver();
    win.fire({ key: "Escape", defaultPrevented: false });
    expect(closed).toEqual(["confirm", "modal"]);
    releaseUnder();
  });

  it("leaves an Escape that something inside already handled", () => {
    const win = fakeWindow();
    let closed = 0;
    const release = activateFocusTrap(() => null, win, {
      onEscape: () => closed++,
    });
    win.fire({ key: "Escape", defaultPrevented: true });
    expect(closed).toBe(0);
    release();
  });

  it("does nothing while its panel is not mounted", () => {
    const win = fakeWindow();
    const release = activateFocusTrap(() => null, win);
    const e = key("Tab");
    win.fire(e);
    expect(e.prevented).toBe(false);
    release();
  });
});
