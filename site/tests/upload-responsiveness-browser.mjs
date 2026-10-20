// Actual preparation/checksum pipeline; isolated Chrome, no physical USB.
// Playwright normally forces pages visible. CDP noDefaults keeps real hidden-tab
// behavior, so the second run proves progress while another tab is selected.
// PLAYWRIGHT_MODULE=/absolute/path/to/playwright/index.mjs PLAYWRIGHT_CHANNEL=chrome node site/tests/upload-responsiveness-browser.mjs
import assert from 'node:assert/strict';
import {readFile,mkdtemp,rm,writeFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {spawn} from 'node:child_process';
const {chromium}=await import(process.env.PLAYWRIGHT_MODULE?pathToFileURL(process.env.PLAYWRIGHT_MODULE).href:'playwright');
const executable=process.env.CHROME_EXECUTABLE||(process.env.PLAYWRIGHT_CHANNEL==='chrome'?(process.platform==='darwin'?'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome':process.platform==='win32'?`${process.env.PROGRAMFILES}/Google/Chrome/Application/chrome.exe`:'/opt/google/chrome/chrome'):chromium.executablePath());
const profile=await mkdtemp(join(tmpdir(),'bonsai-upload-browser-'));
const processChrome=spawn(executable,['--headless=new',...(process.env.CI?['--no-sandbox']:[]),'--remote-debugging-port=0',`--user-data-dir=${profile}`,'--no-first-run','--no-default-browser-check','about:blank'],{stdio:['ignore','ignore','pipe']});
const exited=new Promise(resolve=>processChrome.once('exit',resolve));let browser;
try{
 const endpoint=await new Promise((resolve,reject)=>{let text='';const timeout=setTimeout(()=>reject(Error('Isolated Chrome did not start.')),15000);processChrome.stderr.on('data',chunk=>{text+=chunk;const match=/DevTools listening on (ws:\/\/[^\s]+)/.exec(text);if(match){clearTimeout(timeout);resolve(match[1]);}});processChrome.once('error',error=>{clearTimeout(timeout);reject(error);});processChrome.once('exit',()=>{clearTimeout(timeout);reject(Error('Isolated Chrome exited.'));});});
 browser=await chromium.connectOverCDP(endpoint,{noDefaults:true});
 const context=browser.contexts()[0],page=context.pages()[0],errors=[];
 page.on('pageerror',error=>errors.push(error.message));
 await context.route('https://bonsai-upload.test/**',async route=>{
  const path=new URL(route.request().url()).pathname;
  if(path==='/')return route.fulfill({contentType:'text/html',body:'<!doctype html><title>Isolated upload test</title>'});
  const root=path==='/upload-responsiveness-browser.fixture.js'?'./':'../public/';
  try{return route.fulfill({contentType:'text/javascript',body:await readFile(new URL(`${root}${path.slice(1)}`,import.meta.url),'utf8')});}catch{return route.fulfill({status:404,body:'missing'});}
 });
 await page.goto('https://bonsai-upload.test/');
 const cdp=await context.newCDPSession(page);await cdp.send('Emulation.setCPUThrottlingRate',{rate:4});
 const seconds=Number(process.env.UPLOAD_TEST_SECONDS||120);
 const execute=options=>page.evaluate(async options=>(await import('./upload-responsiveness-browser.fixture.js')).run(options),options);
 const foreground=await execute({seconds});
 const other=await context.newPage();await other.goto('about:blank');await other.bringToFront();
 assert.equal(await page.evaluate(()=>document.visibilityState),'hidden','Background test must use an actually hidden page');
 const background=await execute({seconds});
 const fallback=await execute({seconds:10,withoutWorker:true});
 const result={checkedAt:new Date().toISOString(),browser:browser.version(),cpuThrottling:4,scope:'Real native AudioBuffer/OfflineAudioContext/Worker and actual preparation/upload identity path, no physical serial/USB. Background tab is hidden, not frozen or discarded; OS sleep is not covered.',foreground,background,fallback,errors};
 console.log(JSON.stringify(result,null,2));
 if(process.env.UPLOAD_VALIDATION_REPORT)await writeFile(process.env.UPLOAD_VALIDATION_REPORT,JSON.stringify(result,null,2)+'\n');
 for(const check of [foreground,background,fallback]){assert.equal(check.reachedTransfer,true);assert.equal(check.exactP14SAndIdentity,true);assert.ok(Math.max(0,...check.longTasks)<100,`Upload preparation blocked the tab for ${Math.max(...check.longTasks).toFixed(0)} ms`);assert.ok(check.elapsedMs<Math.max(30000,seconds*150),'Preparation failed to make timely progress');}
 assert.deepEqual(background.visibility,['hidden']);assert.deepEqual(fallback.visibility,['hidden']);assert.deepEqual(errors,[]);
}finally{await browser?.close();processChrome.kill('SIGTERM');await exited;await rm(profile,{recursive:true,force:true,maxRetries:10,retryDelay:200});}
