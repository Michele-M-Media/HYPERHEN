import { expect, test } from "@playwright/test";

// The desktop app's Capture button (status bar, bottom right) saves screenshots for bug reports.
// The Bug report page says where that button is, lists every capture to pick from, and a capture
// taken while a report is being written is added to it.

const PNG =
  "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";
const shot = (n: number) => ({
  name: `screenshot-2026100${n}-120000-000.png`,
  path: `/shots/screenshot-2026100${n}-120000-000.png`,
  bytes: 70,
  data_url: `data:image/png;base64,${PNG}`,
});

test("captures are listed to pick from, and a new capture joins the report", async ({ page }) => {
  await page.addInitScript(
    ([a, b, saved]) => {
      const w = window as unknown as Record<string, unknown>;
      let id = 0;
      const list = [a, b];
      w.__TAURI_INTERNALS__ = {
        invoke: async (cmd: string) => {
          if (cmd === "screenshot_list") return list;
          if (cmd === "screenshot_save") {
            list.unshift(saved);
            return saved;
          }
          return null;
        },
        transformCallback: () => ++id,
        unregisterCallback: () => {},
        convertFileSrc: (p: string) => p,
        metadata: { currentWindow: { label: "main" }, currentWebview: { windowLabel: "main", label: "main" } },
        plugins: {},
      };
      w.__TAURI_EVENT_PLUGIN_INTERNALS__ = { unregisterListener: () => {} };
    },
    [shot(1), shot(2), shot(3)],
  );
  await page.route(
    (u) => u.pathname.startsWith("/api/"),
    (route) => route.fulfill({ status: 200, contentType: "application/json", body: "{}" }),
  );

  await page.goto("/bug-report", { waitUntil: "domcontentloaded" });
  const main = page.getByRole("main");
  await expect(main.getByTestId("br-capture-hint")).toContainText("bottom right", { timeout: 30_000 });

  // The include toggles (each capture also has a Preview button).
  const captures = main.getByTestId("br-captures").locator("button[aria-pressed]");
  await expect(captures).toHaveCount(2);
  await expect(captures.first()).toHaveAttribute("aria-pressed", "false");
  await captures.first().click();
  await expect(captures.first()).toHaveAttribute("aria-pressed", "true");

  await page.getByRole("button", { name: "Capture", exact: true }).click();
  await expect(captures).toHaveCount(3);
  await expect(main.getByRole("button", { name: "screenshot-20261003-120000-000.png", exact: true })).toHaveAttribute("aria-pressed", "true");
});

test("captures open large, page with the arrow keys, and delete from this computer", async ({ page }) => {
  await page.addInitScript(
    ([a, b, c]) => {
      const w = window as unknown as Record<string, unknown>;
      let id = 0;
      let list = [a, b, c];
      w.__TAURI_INTERNALS__ = {
        invoke: async (cmd: string, args: { names?: string[] }) => {
          if (cmd === "screenshot_list") return list;
          if (cmd === "screenshot_delete") {
            const names = args?.names ?? [];
            list = list.filter((s) => !names.includes(s.name));
            return names;
          }
          return null;
        },
        transformCallback: () => ++id,
        unregisterCallback: () => {},
        convertFileSrc: (p: string) => p,
        metadata: { currentWindow: { label: "main" }, currentWebview: { windowLabel: "main", label: "main" } },
        plugins: {},
      };
      w.__TAURI_EVENT_PLUGIN_INTERNALS__ = { unregisterListener: () => {} };
    },
    [shot(3), shot(2), shot(1)],
  );
  await page.route(
    (u) => u.pathname.startsWith("/api/"),
    (route) => route.fulfill({ status: 200, contentType: "application/json", body: "{}" }),
  );

  await page.goto("/bug-report", { waitUntil: "domcontentloaded" });
  const main = page.getByRole("main");
  await expect(main.getByTestId("br-captures").locator("button[aria-pressed]")).toHaveCount(3, {
    timeout: 30_000,
  });

  // The status bar opens the viewer on the newest capture.
  await page.getByRole("button", { name: "View captures" }).first().click();
  const viewer = page.getByRole("dialog");
  await expect(viewer).toContainText("1 of 3");
  await page.keyboard.press("ArrowRight");
  await expect(viewer).toContainText("2 of 3");
  await page.keyboard.press("ArrowLeft");
  await page.keyboard.press("ArrowLeft");
  await expect(viewer).toContainText("3 of 3");

  // Delete asks first, then the capture is gone from the viewer and the Bug report.
  await viewer.getByRole("button", { name: "Delete", exact: true }).click();
  await page.getByRole("alertdialog").getByRole("button", { name: "Delete", exact: true }).click();
  await expect(viewer).toContainText("2 of 2");
  await page.keyboard.press("Escape");
  await expect(main.getByTestId("br-captures").locator("button[aria-pressed]")).toHaveCount(2);
});
