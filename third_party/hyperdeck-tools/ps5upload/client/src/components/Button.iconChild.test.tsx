import { renderToStaticMarkup } from "react-dom/server";
import { describe, expect, it } from "vitest";

import { Button } from "./Button";

describe("a button with its icon passed as a child", () => {
  it("keeps the icon on the label's line", () => {
    const out = renderToStaticMarkup(
      <Button>
        <svg />
        {"Refresh"}
      </Button>,
    );
    expect(out).toMatch(/<span class="[^"]*\[&amp;&gt;svg\]:inline-block[^"]*"><svg><\/svg>Refresh<\/span>/);
  });
});
