import { describe, expect, it } from "vitest";
import { installMarquee, marqueeSeconds, marqueeShift } from "./marquee";

/** Just enough DOM: a focused button holding one name span. */
function fakeName(scrollWidth: number, clientWidth: number) {
  const props = new Map<string, string>();
  const classes = new Set<string>();
  const name = {
    scrollWidth,
    clientWidth,
    style: {
      setProperty: (k: string, v: string) => void props.set(k, v),
      removeProperty: (k: string) => void props.delete(k),
    },
    classList: { add: (c: string) => void classes.add(c), remove: (c: string) => void classes.delete(c) },
  };
  const button = {
    querySelectorAll: (sel: string) => (sel.includes("cw-marquee") ? (classes.has("cw-marquee") ? [name] : []) : [name]),
  };
  const fns: Record<string, (e: unknown) => void> = {};
  const doc = {
    addEventListener: (type: string, fn: (e: unknown) => void) => void (fns[type] = fn),
    removeEventListener: (type: string) => void delete fns[type],
  };
  return { props, classes, button, doc, fns };
}

describe("marquee for long names", () => {
  it("slides only names that are cut off", () => {
    expect(marqueeShift(300, 300)).toBe(0);
    expect(marqueeShift(301, 300)).toBe(0); // rounding
    expect(marqueeShift(420.4, 300)).toBe(121);
    expect(marqueeSeconds(40)).toBe(3);
    expect(marqueeSeconds(400)).toBe(10);
    expect(marqueeSeconds(4000)).toBe(15);
  });

  it("starts when its row gets focus and stops when it leaves", () => {
    const f = fakeName(500, 300);
    const stop = installMarquee(f.doc as unknown as Document);
    f.fns.focusin({ target: f.button });
    expect(f.classes.has("cw-marquee")).toBe(true);
    expect(f.props.get("--cw-marquee")).toBe("200px");
    expect(f.props.get("--cw-marquee-s")).toBe("5s");
    f.fns.focusout({ target: f.button });
    expect(f.classes.has("cw-marquee")).toBe(false);
    expect(f.props.size).toBe(0);
    stop();
    expect(Object.keys(f.fns)).toEqual([]);
  });

  it("leaves a name that fits alone", () => {
    const f = fakeName(200, 300);
    installMarquee(f.doc as unknown as Document);
    f.fns.focusin({ target: f.button });
    expect(f.classes.size).toBe(0);
  });
});
