// Standalone integration runner: no dev server, device permissions, or user tabs.
// Usage: PLAYWRIGHT_MODULE=/absolute/path/to/playwright/index.mjs node tests/live-audio-browser.mjs
import assert from 'node:assert/strict';
import {readFile, writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {pathToFileURL} from 'node:url';

const modulePath=process.env.PLAYWRIGHT_MODULE;
const {chromium}=await import(modulePath?pathToFileURL(modulePath).href:'playwright');
const origin='https://bonsai-audio-validation.test';
const liveSource=await readFile(new URL('../public/live-audio.js',import.meta.url),'utf8');
const routes=new Map([
 ['/public/live-audio.js',{contentType:'text/javascript',body:liveSource}],
 ['/live-audio-browser.fixture.js',{contentType:'text/javascript',body:await readFile(new URL('./live-audio-browser.fixture.js',import.meta.url),'utf8')}],
 ['/audio.js',{contentType:'text/javascript',body:await readFile(new URL('../public/audio.js',import.meta.url),'utf8')}],
 ['/core.js',{contentType:'text/javascript',body:await readFile(new URL('../public/core.js',import.meta.url),'utf8')}],
 ['/offline',{contentType:'text/html',body:await readFile(new URL('./browser-audio-offline.html',import.meta.url),'utf8')}],
 ['/',{contentType:'text/html',body:`<!doctype html><meta charset="utf-8"><title>Isolated synthetic monitor validation</title>
 <button id="run">Run synthetic monitor checks</button><canvas id="wave" width="320" height="80"></canvas>
 <script type="module">import {run} from './live-audio-browser.fixture.js';
 document.querySelector('#run').onclick=async()=>{try{window.validation=await run();}catch(error){window.validation={pass:false,error:error.stack};}};</script>`}],
]);
const recordingFixture={};
if(process.env.RECORDING_FIXTURE_DIR){
 for(const name of ['meta.bin','x3.bin','audio.bin','expected.pcm']){
  const body=await readFile(`${process.env.RECORDING_FIXTURE_DIR}/${name}`);
  routes.set(`/recording-fixture/${name}`,{contentType:'application/octet-stream',body});
  recordingFixture[name]={bytes:body.length,sha256:createHash('sha256').update(body).digest('hex')};
 }
 for(const name of ['device-library.js','playlists.js'])routes.set(`/${name}`,{contentType:'text/javascript',body:await readFile(new URL(`../public/${name}`,import.meta.url),'utf8')});
 routes.set('/record-export-browser.fixture.js',{contentType:'text/javascript',body:await readFile(new URL('./record-export-browser.fixture.js',import.meta.url),'utf8')});
 routes.set('/recording',{contentType:'text/html',body:`<!doctype html><meta charset="utf-8"><title>Synthetic host recording export</title>
 <script type="module">import {run} from './record-export-browser.fixture.js';
 try{window.validation=await run();}catch(error){window.validation={pass:false,error:error.stack};}</script>`});
}
const browser=await chromium.launch({headless:true,args:['--mute-audio'],...(process.env.PLAYWRIGHT_CHANNEL?{channel:process.env.PLAYWRIGHT_CHANNEL}:{})});
const errors=[],requests=[];
try{
 const context=await browser.newContext({serviceWorkers:'block'});
 await context.route('**/*',async route=>{
  const url=new URL(route.request().url());requests.push(url.href);
  const asset=url.origin===origin?routes.get(url.pathname):null;
  if(asset)await route.fulfill({status:200,...asset});
  else {errors.push(`Unexpected request: ${url.href}`);await route.abort();}
 });
 const page=await context.newPage();
 page.on('pageerror',error=>errors.push(error.message));
 await page.goto(origin);
 await page.locator('#run').click();
 try{await page.waitForFunction(()=>window.validation!==undefined,{},{timeout:90000});}
 catch(error){throw new Error(`Monitor browser timeout: ${await page.evaluate(()=>window.validationProgress)}; ${JSON.stringify(errors)}`,{cause:error});}
 const monitor=await page.evaluate(()=>window.validation);
 await page.goto(`${origin}/offline`);
 await page.locator('#run').click();
 await page.waitForFunction(()=>document.querySelector('#result').dataset.result,{},{timeout:30000});
 const mixer=await page.locator('#result').evaluate(node=>({pass:node.dataset.result==='pass',output:node.textContent}));
 let recordingExport={status:'not-run',reason:'RECORDING_FIXTURE_DIR was not supplied'};
 if(process.env.RECORDING_FIXTURE_DIR){
  await page.goto(`${origin}/recording`);
  await page.waitForFunction(()=>window.validation!==undefined,{},{timeout:30000});
  recordingExport={...await page.evaluate(()=>window.validation),fixture:recordingFixture};
 }
 const result={
  checkedAt:new Date().toISOString(),browser:browser.version(),browserChannel:process.env.PLAYWRIGHT_CHANNEL||'bundled-chromium',node:process.version,
  liveAudioSha256:createHash('sha256').update(liveSource).digest('hex'),
  scope:'Native Chromium Web Audio with synthetic stereo MediaStream and overridden device metadata. No physical USB, microphone, serial, chooser, or speaker audibility test. Chromium output muted.',
  monitor,mixer,recordingExport,errors,requests,
 };
 if(process.env.AUDIO_VALIDATION_REPORT)await writeFile(process.env.AUDIO_VALIDATION_REPORT,`${JSON.stringify(result,null,2)}\n`);
 console.log(JSON.stringify(result,null,2));
 assert.equal(monitor.pass,true,monitor.error||'Live monitor browser checks failed');
 assert.equal(mixer.pass,true,mixer.output);
 if(process.env.RECORDING_FIXTURE_DIR)assert.equal(recordingExport.pass,true,recordingExport.error);
 assert.deepEqual(errors,[],'Unexpected browser errors or external network requests');
 await context.close();
}finally{await browser.close();}
