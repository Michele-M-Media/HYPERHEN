import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it, vi } from "vitest";

vi.mock("@tauri-apps/api/core", () => ({ invoke: vi.fn() }));
vi.mock("react-router", () => ({ useNavigate: () => () => {} }));

import { PowerNotice } from "./ConnectionGate";

const view = (props: Parameters<typeof PowerNotice>[0]) =>
  renderToStaticMarkup(<PowerNotice {...props} />);

describe("rest-mode notice on console screens", () => {
  it("says rest mode calmly, with Wake when this console can be woken", () => {
    const html = view({ power: "rest", canWake: true, helperReturns: true });
    expect(html).toContain("Your PS5 is in rest mode");
    expect(html).toContain("The helper comes back by itself once it is awake.");
    expect(html).toContain(">Wake<");
    expect(html).not.toContain("Helper isn&#x27;t running");
  });

  it("offers Wake & sign in when the session keys are stored", () => {
    expect(view({ power: "rest", canWake: true, canSignIn: true })).toContain("Wake &amp; sign in");
  });

  it("asks for the helper after the wake when reconnect is off", () => {
    const html = view({ power: "rest", canWake: true, helperReturns: false });
    expect(html).toContain("then send the helper.");
  });

  it("has no Wake button without a stored credential", () => {
    const html = view({ power: "rest", canWake: false, helperReturns: true });
    expect(html).toContain("Wake it with the controller. The helper comes back");
    expect(html).not.toContain("<button");
  });

  it("not answering names the rest-mode network setting", () => {
    const html = view({ power: "unreachable" });
    expect(html).toContain("Your PS5 isn&#x27;t answering");
    expect(html).toContain("Features Available in Rest Mode");
  });
});
