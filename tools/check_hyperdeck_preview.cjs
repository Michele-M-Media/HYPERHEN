/* Browser checks run only against the local fixture preview. */
const { chromium } = require('playwright');
const path = require('node:path');
const assert = require('node:assert/strict');
const fs = require('node:fs');
(async () => {
  const browser = await chromium.launch({
    executablePath: process.env.CHROME_BIN || '/usr/bin/chromium',
    timeout: 25000,
    env: { ...process.env, XDG_CONFIG_HOME: '/tmp/hyperdeck-browser-config', XDG_CACHE_HOME: '/tmp/hyperdeck-browser-cache' },
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--disable-breakpad', '--disable-crash-reporter']
  });
  try {
    const page = await browser.newPage({ viewport: { width: 1640, height: 1140 }, reducedMotion: 'reduce' });
    const errors=[];
    page.on('pageerror',e=>errors.push(e.message));
    const root=path.resolve(__dirname,'../assets/previews');
    // Browser policy blocks file:// navigation; inject the authorized local
    // fixture directly so this test requires no HTTP server or external host.
    const frames=fs.readFileSync(path.join(root,'hyperdeck-frames.js'),'utf8');
    const html=fs.readFileSync(path.join(root,'hyperdeck.html'),'utf8')
      .replace('<script src="hyperdeck-frames.js"></script>','<script>'+frames+'</script>');
    await page.setContent(html,{waitUntil:'load',timeout:15000});
    await page.locator('.canvas .label').first().waitFor();
    assert.match(await page.locator('#canvas').innerText(),/Your console/);
    await page.screenshot({path:path.join(root,'hyperdeck-overview.png'),fullPage:true});
    await page.locator('[data-tab="1"]').click();
    await page.selectOption('#profile','1');
    assert.match(await page.locator('#canvas').innerText(),/BYEPERVISOR/);
    await page.screenshot({path:path.join(root,'hyperdeck-reactor.png'),fullPage:true});
    await page.selectOption('#scenario','1');
    assert.match(await page.locator('#canvas').innerText(),/FAILED/);
    await page.selectOption('#scenario','2');
    assert.match(await page.locator('#canvas').innerText(),/NO FRESH SAMPLE/);
    await page.selectOption('#scenario','0');
    await page.locator('[data-tab="2"]').click();
    assert.match(await page.locator('#canvas').innerText(),/TCP 9025/);
    await page.locator('[data-tab="3"]').click();
    await page.keyboard.press('ArrowRight'); await page.keyboard.press('Enter');
    assert.match(await page.locator('#notice').innerText(),/Tema applicato/);
    await page.keyboard.press('ArrowDown'); await page.keyboard.press('ArrowDown');
    await page.keyboard.press('Enter');
    assert.match(await page.locator('#notice').innerText(),/Movimento ridotto: attivo/);
    assert.match(await page.locator('#canvas').innerText(),/ON/);
    await page.setViewportSize({width:390,height:844});
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth));
    await page.screenshot({path:path.join(root,'hyperdeck-mobile.png'),fullPage:true});
    assert.deepEqual(errors,[]);
    console.log('HyperDeck preview: desktop, mobile, firmware routes, failure/stale states and theme controls passed.');
  } finally { await browser.close(); }
})().catch(e=>{console.error(e.message);process.exitCode=1;});
