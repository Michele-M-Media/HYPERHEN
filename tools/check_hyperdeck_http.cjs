/* Production router/catalogue/UI; native launch and mount calls are host doubles. */
const assert=require('node:assert/strict');
const {spawn}=require('node:child_process');
const {once}=require('node:events');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const http=require('node:http');
const {chromium}=require('playwright');
const base='http://127.0.0.1:8088';
const root=fs.mkdtempSync(path.join(os.tmpdir(),'hyperhen-pkg-'));
const name='Homebrew <script>alert(1)</script> build.pkg'.replace(/\//g,'_');
const packagePath=path.join(root,name);
fs.writeFileSync(packagePath,Buffer.from([0x7f,0x43,0x4e,0x54,1,2,3,4]));
fs.writeFileSync(path.join(root,'ignore.elf'),'not a package');
fs.symlinkSync(packagePath,path.join(root,'symlink.pkg'));
const args=p=>'hyperhen-pkg-install.elf '+p.replace(/ /g,'\\ ');
const query=p=>'/hbldr?pipe=1&daemon=1&path='+encodeURIComponent('/data/HyperHEN/bin/hyperhen-pkg-install.elf')+'&args='+encodeURIComponent(args(p));
async function status(route,options){const response=await fetch(base+route,options);await response.arrayBuffer();return response.status;}
async function waitReady(child){for(let i=0;i<100;i++){if(child.exitCode!==null)throw Error('Server exited '+child.exitCode);try{if((await fetch(base+'/api/health')).ok)return;}catch{}await new Promise(r=>setTimeout(r,50));}throw Error('Server not ready');}
async function main(){
  try{await fetch(base+'/api/health');throw Error('Port 8088 occupied; refusing to touch listener');}catch(e){if(e.message.startsWith('Port '))throw e;}
  const server=spawn(process.argv[2],[],{env:{...process.env,HYPERDECK_PKG_TEST_ROOT:root},stdio:['ignore','pipe','pipe']});let browser;
  try{
    await waitReady(server);
    const pageResponse=await fetch(base+'/');assert.equal(pageResponse.status,200);
    assert.match(await pageResponse.text(),/<title>HyperHEN · HyperDeck<\/title>/);
    assert.match(pageResponse.headers.get('content-security-policy'),/frame-ancestors 'none'/);
    assert.equal(pageResponse.headers.get('access-control-allow-origin'),null);
    const pages=[['/installer.html',/Installer PKG Background/]];
    for(const [route,title] of pages){const response=await fetch(base+route);assert.equal(response.status,200);assert.match(await response.text(),title);}
    const snapshot=await(await fetch(base+'/api/installer')).json();assert.match(snapshot.token,/^[0-9a-f]{48}$/);assert.equal(snapshot.scanned,false);
    const headers={'X-HyperDeck-Token':snapshot.token};
    const engines=await(await fetch(base+'/api/engines')).json();assert.equal(engines.token,snapshot.token);assert.deepEqual(engines.engines.map(e=>[e.name,e.port,e.ready]),[['game-compressor',5910,false],['file-manager',8888,false],['cheatrunner',9999,false],['ps5upload',9180,false]]);
    assert.equal((await fetch(base+'/api/engines/start?name=game-compressor',{method:'POST'})).status,403);
    assert.equal((await fetch(base+'/api/engines/start?name=game-compressor',{method:'POST',headers:{...headers,Origin:'https://evil.example'}})).status,403);
    const engineStart=await(await fetch(base+'/api/engines/start?name=game-compressor',{method:'POST',headers})).json();assert.equal(engineStart.ready,true);assert.equal(engineStart.url,'http://127.0.0.1:5910/');
    assert.equal((await fetch(base+'/api/engines/start?name=unknown',{method:'POST',headers})).status,404);
    assert.equal((await fetch(base+'/api/installer/scan',{method:'POST'})).status,403);
    assert.equal((await fetch(base+'/api/installer/scan',{method:'POST',headers:{...headers,Origin:'https://evil.example'}})).status,403);
    const scanned=await(await fetch(base+'/api/installer/scan',{method:'POST',headers})).json();
    assert.equal(scanned.packages.length,1);assert.equal(scanned.packages[0].path,packagePath);
    assert.equal(scanned.volumes[0].available,59000000000);
    assert.equal((await fetch(base+query(packagePath))).status,403);
    assert.equal((await fetch(base+query('/etc/passwd'),{headers})).status,409);
    assert.equal((await fetch(base+query(packagePath).replace('pipe=1','pipe=0'),{headers})).status,400);
    assert.equal((await fetch(base+query(packagePath).replace(encodeURIComponent('/data/HyperHEN/bin/hyperhen-pkg-install.elf'),encodeURIComponent('/data/evil.elf')),{headers})).status,400);
    const dispatch=await fetch(base+query(packagePath),{headers});assert.equal(dispatch.status,200);const dispatchText=await dispatch.text();assert.match(dispatchText,/HOST TEST ONLY: argv\[0\]/);assert.match(dispatchText,/HYPERDECK_PKG phase=install result=0x00000000/);
    for(const route of ['/fs/','/elfldr','/api/inventory?kind=plugins','/api/action','/api/frame','/param.json','/api/gc/games','/api/list','/api/games','/api/control/shutdown'])assert.equal(await status(route),404);
    const wrongHost=await new Promise((resolve,reject)=>http.get(base+'/api/installer',{headers:{Host:'evil.example'}},r=>{r.resume();resolve(r.statusCode);}).on('error',reject));assert.equal(wrongHost,403);
    const duplicate=spawn(process.argv[2],[],{stdio:'ignore'});assert.equal((await once(duplicate,'exit'))[0],2);
    browser=await chromium.launch({executablePath:process.env.CHROME_BIN||'/usr/bin/chromium',args:['--no-sandbox','--disable-dev-shm-usage']});
    const page=await browser.newPage({viewport:{width:1920,height:1200},reducedMotion:'reduce'});const errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.goto(base,{waitUntil:'networkidle'});await page.waitForFunction(()=>document.getElementById('deck-state').classList.contains('online'));
    assert.equal(await page.locator('.deck-card').count(),5);
    assert.deepEqual(await page.locator('.deck-card[data-engine]').evaluateAll(cards=>cards.map(card=>[card.dataset.engine,card.href])),[['game-compressor','http://127.0.0.1:5910/'],['file-manager','http://127.0.0.1:8888/'],['cheatrunner','http://127.0.0.1:9999/'],['ps5upload','http://127.0.0.1:9180/']]);
    const preview=path.resolve(__dirname,'../assets/previews');
    await page.screenshot({path:path.join(preview,'hyperdeck-portal-desktop.png'),fullPage:true});
    await page.setViewportSize({width:390,height:844});
    for(const route of ['/']){
      await page.goto(base+route,{waitUntil:'networkidle'});
      assert(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth));
    }
    await page.goto(base,{waitUntil:'networkidle'});await page.screenshot({path:path.join(preview,'hyperdeck-portal-mobile.png'),fullPage:true});
    await page.setViewportSize({width:1920,height:1200});
    await page.goto(base+'/installer.html',{waitUntil:'networkidle'});await page.waitForFunction(()=>document.getElementById('connection').classList.contains('online'));
    await page.getByRole('button',{name:'Scan drives'}).click();await page.locator('.package-row').waitFor();
    assert.equal(await page.locator('.package-text script').count(),0);assert.equal(await page.locator('.package-text h3').innerText(),name);
    await page.screenshot({path:path.join(preview,'installer-pkg-background-desktop.png'),fullPage:true});
    await page.getByRole('button',{name:'Install '+name,exact:true}).click();assert.equal(await page.locator('#modal').isVisible(),true);assert.equal(await page.locator('#dialog-cancel').evaluate(e=>e===document.activeElement),true);
    await page.keyboard.press('Escape');assert.equal(await page.locator('#modal').isVisible(),false);
    await page.getByRole('button',{name:'Install '+name,exact:true}).click();await page.getByRole('button',{name:'Install package',exact:true}).click();
    await page.waitForFunction(()=>document.getElementById('dispatch-title').textContent==='Request accepted.');
    assert.match(await page.locator('#dispatch-detail').innerText(),/Check Downloads/);assert.equal(await page.locator('#dispatch-meter').isVisible(),false);
    await page.route('**/hbldr?*',route=>route.fulfill({status:200,contentType:'text/x-log; charset=utf-8',body:'HYPERDECK_PKG phase=init result=0x80000001\n'}));
    await page.getByRole('button',{name:'Install '+name,exact:true}).click();await page.getByRole('button',{name:'Install package',exact:true}).click();
    await page.waitForFunction(()=>document.getElementById('dispatch-title').textContent==='Not accepted.');assert.match(await page.locator('#dispatch-detail').innerText(),/80000001/);await page.unroute('**/hbldr?*');
    await page.getByRole('searchbox').fill('no such package');assert.equal(await page.locator('.package-row').count(),0);await page.getByRole('searchbox').fill('');assert.equal(await page.locator('.package-row').count(),1);
    await page.setViewportSize({width:390,height:844});await page.screenshot({path:path.join(preview,'installer-pkg-background-mobile.png'),fullPage:true});assert(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth));
    await page.getByRole('button',{name:'About this installer'}).click();assert.match(await page.locator('#dialog-text').innerText(),/Connect a drive/);await page.keyboard.press('Escape');
    // Same-sized content replacement and stale catalogues must not be submitted.
    fs.writeFileSync(packagePath,'changed!');assert.equal((await fetch(base+query(packagePath),{headers})).status,409);
    fs.unlinkSync(packagePath);fs.symlinkSync('/etc/passwd',packagePath);assert.equal((await fetch(base+query(packagePath),{headers})).status,409);
    assert.deepEqual(errors,[]);
    console.log('PASS: one HyperDeck Media entry, authenticated native-engine launcher on original ports, no engine API proxying, production PKG catalogue, space data, fixed /hbldr -> PKGInstall argv convention, symlinks/stale files, CSRF/Host/origin, confirmation, XSS, desktop/mobile and no fake completion. Native execution is mocked; PS5 untested.');
  }finally{if(browser)await browser.close();if(server.exitCode===null){server.kill('SIGTERM');await once(server,'exit');}}
}
main().catch(e=>{console.error(e);process.exitCode=1;});
