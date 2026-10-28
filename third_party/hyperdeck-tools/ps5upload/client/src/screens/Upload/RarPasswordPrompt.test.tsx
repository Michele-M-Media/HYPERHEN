import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it, vi } from "vitest";

vi.mock("@tauri-apps/api/core", () => ({ invoke: vi.fn() }));

import { RarPasswordPrompt } from "./RarPasswordPrompt";

describe("rar password prompt", () => {
  it("asks for the password, as a masked field", () => {
    const html = renderToStaticMarkup(
      <RarPasswordPrompt problem="required" onSubmit={() => {}} />,
    );
    expect(html).toContain("This archive needs a password.");
    expect(html).toContain('type="password"');
    expect(html).toContain("Retry with password");
  });
  it("says so when the last password was wrong", () => {
    expect(
      renderToStaticMarkup(<RarPasswordPrompt problem="wrong" onSubmit={() => {}} />),
    ).toContain("That password was wrong");
  });
});
