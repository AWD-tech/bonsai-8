import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readFileSync} from 'node:fs';
import * as THREE from '../public/vendor/three.module.js';
import {heightSettings,CONTROL_HEIGHTS} from '../public/control-height.js';

// Build the real control geometry without starting a renderer or drawing rear lettering.
const context=vm.createContext({THREE,heightSettings});
vm.runInContext(readFileSync(new URL('../public/device.js',import.meta.url),'utf8').replace(/^import .*;\n/gm,'').replace('export class Device','class Device')+'\nthis.Device=Device;',context);
function model(){
 const d=Object.create(context.Device.prototype);
 Object.assign(d,{body:new THREE.Group(),knobs:[],leds:[],statusLeds:[],buttons:{},buttonDown:{},materials:Object.fromEntries(['shell','plastic','recess','dark','silver'].map(k=>[k,new THREE.MeshStandardMaterial()])),backPanel(){}});
 d.build();d.configureHeights();return d;
}
function extent(mesh,axis){mesh.updateWorldMatrix(true,false);mesh.geometry.computeBoundingBox();const box=mesh.geometry.boundingBox.clone().applyMatrix4(mesh.matrixWorld);return [box.min[axis],box.max[axis]];}
for(const c of CONTROL_HEIGHTS)test(`${c.label} grows from its seated base without moving the whole control`,()=>{
 const d=model(),key=c.key;
 const mesh=key==='wheel'?d.wheel.children[0]:key.startsWith('slider')?d.knobs[Number(key.slice(6))].children[1]:d.buttons[key];
 const axis=key==='wheel'||['play','function'].includes(key)?'x':key.startsWith('volume')?'y':'z';
 d.setControlHeight(key,100);const before=extent(mesh,axis),position=mesh.position.clone(),base=key==='wheel'?1:0;
 for(const value of [300,40,104,100]){
  d.setControlHeight(key,value);const after=extent(mesh,axis);
  assert.ok(Math.abs(after[base]-before[base])<1e-5,`${key} base moved out of its socket at ${value}%`);
  assert.ok(Math.abs((after[1]-after[0])/(before[1]-before[0])-value/100)<1e-5,'actual part length must change');
  assert.deepEqual(mesh.position,position,'whole control must not translate');
 }
 if(key in d.buttons){d.setControlHeight(key,300);d.setButton(key,true);d.setButton(key,false);assert.ok(Math.abs(extent(mesh,axis)[base]-before[base])<1e-5,'release must keep its original seat');}
});
