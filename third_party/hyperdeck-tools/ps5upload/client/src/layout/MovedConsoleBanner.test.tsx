import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it, vi } from "vitest";

vi.mock("@tauri-apps/api/core", () => ({ invoke: vi.fn() }));

import { MovedConsoleBannerView } from "./MovedConsoleBanner";

const view = (offer: { from: string; to: string } | null, activeHost = "192.168.137.2") =>
  renderToStaticMarkup(
    <MovedConsoleBannerView
      offer={offer}
      activeHost={activeHost}
      onSwitch={() => {}}
      onDismiss={() => {}}
    />,
  );

describe("moved console banner", () => {
  it("names the new address and offers to switch to it", () => {
    const html = view({ from: "192.168.137.2", to: "192.168.2.36" });
    expect(html).toContain("192.168.2.36");
    expect(html).toContain("192.168.137.2");
    expect(html).toContain("Switch to 192.168.2.36");
    expect(html).toContain("Dismiss");
  });

  it("is silent with no offer, or once the app points elsewhere", () => {
    expect(view(null)).toBe("");
    expect(view({ from: "192.168.137.2", to: "192.168.2.36" }, "192.168.2.36")).toBe("");
  });
});
