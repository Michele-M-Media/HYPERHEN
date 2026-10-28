/**
 * Images for the announcement posts (release-posts.js, promo-posts.js): screenshots of the
 * live website, ps5upload.phantomptr.com, taken with the client's Playwright.
 *
 *   node scripts/post-images.js          # refresh .release_posts/website/
 *
 * Each post script calls copyImages() to put the ones it wants into its own images/ folder.
 * Taking fresh shots needs the client's node_modules (Playwright + Chromium); without them the
 * shots already in .release_posts/website/ are used, and if there are none the posts are
 * written without images.
 */
const fs = require("fs");
const path = require("path");
const { execFileSync } = require("child_process");

const SITE_URL = "https://ps5upload.phantomptr.com/";
const repoRoot = path.resolve(__dirname, "..");
const SHOTS = path.join(repoRoot, ".release_posts", "website");

/** The shots: a name, and the section to scroll to (null = top of the page). */
const VIEWS = [
  ["site-hero", null],
  ["site-features", "features"],
  ["site-download", "download"],
  ["site-news", "news"],
];

/** Takes fresh website screenshots into .release_posts/website/. Returns whether it did. */
function captureSite() {
  const client = path.join(repoRoot, "client");
  if (!fs.existsSync(path.join(client, "node_modules", "@playwright", "test"))) return false;
  fs.mkdirSync(SHOTS, { recursive: true });
  // A tiny script run from client/ so it finds Playwright there.
  const runner = `
const { chromium } = require("@playwright/test");
(async () => {
  const views = ${JSON.stringify(VIEWS)};
  const browser = await chromium.launch();
  for (const scheme of ["dark", "light"]) {
    const page = await browser.newPage({ viewport: { width: 1440, height: 900 }, deviceScaleFactor: 2, colorScheme: scheme });
    await page.goto(${JSON.stringify(SITE_URL)}, { waitUntil: "networkidle", timeout: 30000 });
    await page.waitForTimeout(1200);
    for (const [name, id] of views) {
      await page.evaluate((i) => {
        const el = i && document.getElementById(i);
        window.scrollTo(0, el ? el.getBoundingClientRect().top + window.scrollY - 76 : 0);
      }, id);
      await page.waitForTimeout(500);
      await page.screenshot({ path: ${JSON.stringify(SHOTS)} + "/" + name + "-" + scheme + ".png" });
    }
    await page.close();
  }
  await browser.close();
})().catch((e) => { console.error(e.message); process.exit(1); });
`;
  try {
    execFileSync(process.execPath, ["-e", runner], { cwd: client, stdio: ["ignore", "ignore", "pipe"], timeout: 120000 });
    return true;
  } catch (e) {
    console.error(`(website screenshots not refreshed: ${String(e.stderr || e.message).trim().split("\n")[0]})`);
    return false;
  }
}

/** Copies the named shots ("site-hero-dark", …) into `dir`; returns the file names copied. */
function copyImages(dir, names) {
  const have = names.filter((n) => fs.existsSync(path.join(SHOTS, `${n}.png`)));
  if (!have.length) return [];
  fs.mkdirSync(dir, { recursive: true });
  for (const n of have) fs.copyFileSync(path.join(SHOTS, `${n}.png`), path.join(dir, `${n}.png`));
  return have.map((n) => `${n}.png`);
}

module.exports = { captureSite, copyImages, SHOTS };

if (require.main === module) {
  console.log(captureSite() ? `Website screenshots in ${path.relative(repoRoot, SHOTS)}` : "Could not take screenshots");
}
