import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it, vi } from "vitest";
import { t } from "../strings";
import { ErrorBoundary, resetPageSettings } from "../ui/ErrorBoundary";
import { OfflinePage } from "./OfflinePage";

/** The text as React escapes it in markup ("'" and "&" become entities). */
const esc = (text: string) => renderToStaticMarkup(<>{text}</>);

/** A Storage in memory. */
function memStorage(init: Record<string, string>): Storage {
  const m = new Map(Object.entries(init));
  return {
    get length() {
      return m.size;
    },
    key: (i: number) => Array.from(m.keys())[i] ?? null,
    getItem: (k: string) => m.get(k) ?? null,
    setItem: (k: string, v: string) => void m.set(k, v),
    removeItem: (k: string) => void m.delete(k),
    clear: () => m.clear(),
  } as Storage;
}

describe("offline screen", () => {
  it("says the helper is not running and how to load it, with Try again and Close", () => {
    const html = renderToStaticMarkup(<OfflinePage probe={() => Promise.resolve(false)} onBack={() => {}} />);
    expect(html).toContain(esc(t("cw_offline_title")));
    expect(html).toContain(esc(t("cw_offline_body")));
    expect(html).toContain(esc(t("cw_offline_retry")));
    expect(html).toContain(esc(t("cw_offline_close")));
    expect(html).toContain("data-nav-modal"); // the D-pad stays on its buttons
    expect(t("cw_offline_title")).toBe("PS5Upload Minion isn't running");
    expect(t("cw_offline_body")).toMatch(/PS5Upload app on your PC or from your payload loader/);
  });
});

describe("error boundary", () => {
  it("renders its children while nothing failed", () => {
    expect(renderToStaticMarkup(<ErrorBoundary>{<p data-ok="" />}</ErrorBoundary>)).toBe('<p data-ok=""></p>');
  });

  it("a render error shows Reload and Reset settings & reload instead of a blank page", () => {
    const err = vi.spyOn(console, "error").mockImplementation(() => {});
    const state = ErrorBoundary.getDerivedStateFromError(new Error("boom"));
    const eb = new ErrorBoundary({ children: null });
    eb.state = state;
    const html = renderToStaticMarkup(<>{eb.render()}</>);
    expect(html).toContain(esc(t("cw_error_title")));
    expect(html).toContain(esc(t("cw_error_reload")));
    expect(html).toContain(esc(t("cw_error_reset")));
    expect(html).toContain("boom");
    expect(ErrorBoundary.getDerivedStateFromError("text").error.message).toBe("text");
    err.mockRestore();
  });

  it("reset forgets this page's settings only (and keeps the tour's seen mark)", () => {
    const s = memStorage({
      "ps5upload.console.theme": "light",
      "ps5upload.console.lang": "de",
      "ps5upload.files.view": "{}",
      "ps5upload.console.tourSeen": "1",
      "someone.else": "x",
    });
    expect(resetPageSettings(s)).toBe(3);
    expect(s.getItem("ps5upload.console.theme")).toBeNull();
    expect(s.getItem("ps5upload.files.view")).toBeNull();
    expect(s.getItem("ps5upload.console.tourSeen")).toBe("1");
    expect(s.getItem("someone.else")).toBe("x");
    expect(resetPageSettings(null)).toBe(0);
  });
});
