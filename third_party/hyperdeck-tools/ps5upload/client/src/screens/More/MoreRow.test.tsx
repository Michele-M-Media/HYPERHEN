import { renderToStaticMarkup } from "react-dom/server";
import { MemoryRouter } from "react-router";
import { describe, expect, it, vi } from "vitest";

vi.mock("../../state/lang", () => ({
  useTr: () => (key: string, vars?: Record<string, string | number>, fallback?: string) => {
    let out = fallback ?? key;
    for (const [k, v] of Object.entries(vars ?? {})) out = out.replace(`{${k}}`, String(v));
    return out;
  },
}));
vi.mock("../../state/navSidebar", () => ({
  useNavSidebarStore: (selector: (s: { hidden: string[]; toggleHidden: () => void }) => unknown) =>
    selector({ hidden: ["/cheats"], toggleHidden: () => {} }),
}));
vi.mock("../../layout/RosterPicker", () => ({ default: () => null }));
vi.mock("../../layout/NotificationInbox", () => ({ default: () => null }));

import { NAV_ITEMS } from "../../layout/navItems";
import { MoreRow } from "./index";

const row = (to: string) =>
  renderToStaticMarkup(
    <MemoryRouter>
      <ul>
        <MoreRow item={NAV_ITEMS.find((i) => i.to === to)!} errorCount={0} updateAvailable={false} />
      </ul>
    </MemoryRouter>,
  );

describe("More's sidebar switch", () => {
  it("shows a hidden screen as hidden, with the way to bring it back", () => {
    const out = row("/cheats");
    expect(out).toContain('aria-pressed="false"');
    expect(out).toMatch(/aria-label="Show [^"]+ in the sidebar"/);
    expect(out).toContain("Hidden from the sidebar");
  });

  it("shows a visible screen as shown, with the way to hide it", () => {
    const out = row("/games");
    expect(out).toContain('aria-pressed="true"');
    expect(out).toMatch(/aria-label="Hide [^"]+ from the sidebar"/);
    expect(out).not.toContain("Hidden from the sidebar");
  });

  it("offers it only where there is a sidebar", () => {
    // Phones navigate with tabs and this list; the switch would change nothing there.
    expect(row("/games")).toMatch(/<button[^>]*class="[^"]*\bhidden\b[^"]*\bmd:flex\b/);
  });
});
