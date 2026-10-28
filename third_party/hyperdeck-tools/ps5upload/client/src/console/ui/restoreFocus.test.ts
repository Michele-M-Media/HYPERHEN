import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { anchorOf, refocusIfLost, restoreFocus } from "./restoreFocus";

/*
 * No DOM in vitest here: a fake with what restoreFocus touches (closest,
 * querySelector(All) for list items and focusables, isConnected, focus).
 */
class El {
  parent: El | null = null;
  children: El[] = [];
  constructor(
    readonly name: string,
    readonly attrs: Record<string, string> = {},
    readonly tagName = "DIV",
  ) {}
  add(...kids: El[]): this {
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
  get isConnected(): boolean {
    return topOf(this) === root;
  }
  matches(sel: string): boolean {
    if (sel === "[data-nav-list]") return "data-nav-list" in this.attrs;
    if (sel === "[data-nav-item]") return "data-nav-item" in this.attrs;
    if (sel === ".cw-content") return this.attrs.class === "cw-content";
    if (sel === "[data-nav-modal]") return "data-nav-modal" in this.attrs;
    const act = /^\[data-action="([^"]+)"\]$/.exec(sel);
    if (act) return this.attrs["data-action"] === act[1];
    return this.tagName === "BUTTON"; // the focusable selector
  }
  closest(sel: string): El | null {
    return this.matches(sel) ? this : (this.parent?.closest(sel) ?? null);
  }
  descendants(): El[] {
    return this.children.flatMap((c) => [c, ...c.descendants()]);
  }
  querySelectorAll(sel: string): El[] {
    return this.descendants().filter((e) => e.matches(sel));
  }
  querySelector(sel: string): El | null {
    return this.querySelectorAll(sel)[0] ?? null;
  }
  focus(): void {
    doc.activeElement = this;
  }
}

function topOf(e: El): El {
  return e.parent ? topOf(e.parent) : e;
}

const root = new El("root");
const doc = { activeElement: null as El | null, body: root };

let content: El;
let list: El;
let rows: El[];
let up: El;

const row = (i: number) =>
  new El(`row${i}`, { "data-nav-item": "" }, "LI").add(new El(`open${i}`, {}, "BUTTON"), new El(`more${i}`, {}, "BUTTON"));

beforeEach(() => {
  root.children = [];
  rows = [0, 1, 2].map(row);
  list = new El("list", { "data-nav-list": "" }, "UL").add(...rows);
  up = new El("up", {}, "BUTTON");
  content = new El("content", { class: "cw-content" }).add(up, list);
  root.add(content);
  (globalThis as any).document = doc;
});

afterEach(() => {
  delete (globalThis as any).document;
});

const btnOf = (r: El, i = 0) => r.children[i];

describe("ConfirmDialog focus restore", () => {
  it("goes back to the opener while it is still there", () => {
    const a = anchorOf(btnOf(rows[1], 1) as unknown as HTMLElement);
    expect(restoreFocus(a)).toBe(btnOf(rows[1], 1));
  });

  it("a deleted row: the item now at the same place", () => {
    const a = anchorOf(btnOf(rows[1]) as unknown as HTMLElement);
    rows[1].remove();
    restoreFocus(a);
    expect(doc.activeElement).toBe(btnOf(rows[2]));
  });

  it("the last row deleted: the nearest remaining one", () => {
    const a = anchorOf(btnOf(rows[2]) as unknown as HTMLElement);
    rows[2].remove();
    restoreFocus(a);
    expect(doc.activeElement).toBe(btnOf(rows[1]));
  });

  it("the list emptied: the section's first control, never the rail", () => {
    const a = anchorOf(btnOf(rows[0]) as unknown as HTMLElement);
    rows.forEach((r) => r.remove());
    restoreFocus(a);
    expect(doc.activeElement).toBe(up);
  });

  it("nothing to remember: nothing happens", () => {
    expect(restoreFocus(anchorOf(null))).toBeNull();
  });
});

describe("refocusIfLost (a dialog button that removed itself)", () => {
  let modal: El;
  let close: El;
  beforeEach(() => {
    close = new El("close", {}, "BUTTON");
    modal = new El("modal", { "data-nav-modal": "" });
    root.add(modal);
  });

  it("Calculate became Cancel: focus goes to Cancel, not the dialog's first control", () => {
    const calc = new El("calc", { "data-action": "size-start" }, "BUTTON");
    modal.add(close, calc);
    calc.focus();
    calc.remove();
    const cancel = new El("cancel", { "data-action": "size-cancel" }, "BUTTON");
    modal.add(cancel);
    expect(refocusIfLost(modal as unknown as ParentNode, ['[data-action="size-cancel"]', '[data-action="size-start"]'])).toBe(cancel);
    expect(doc.activeElement).toBe(cancel);
  });

  it("Keep both gone: the next preferred control, else the first one", () => {
    const keep = new El("keep", { "data-action": "unpack-keep" }, "BUTTON");
    const go = new El("go", { "data-action": "unpack-go" }, "BUTTON");
    modal.add(close, keep, go);
    keep.focus();
    keep.remove();
    refocusIfLost(modal as unknown as ParentNode, ['[data-action="unpack-go"]']);
    expect(doc.activeElement).toBe(go);
    go.remove();
    refocusIfLost(modal as unknown as ParentNode, ['[data-action="unpack-go"]']);
    expect(doc.activeElement).toBe(close);
  });

  it("focus still on a live control: left alone", () => {
    const go = new El("go", { "data-action": "unpack-go" }, "BUTTON");
    modal.add(close, go);
    close.focus();
    expect(refocusIfLost(modal as unknown as ParentNode, ['[data-action="unpack-go"]'])).toBeNull();
    expect(doc.activeElement).toBe(close);
  });
});
