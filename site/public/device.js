import * as THREE from 'three';
import { OrbitControls } from './vendor/OrbitControls.js';
import { RoomEnvironment } from './vendor/RoomEnvironment.js';
import { clamp } from './core.js';
import { heightSettings } from './control-height.js?v=20260930-audio-status-07';

export class Device {
  constructor(container, events) {
    this.events=events; this.container=container; this.knobs=[]; this.leds=[]; this.statusLeds=[]; this.buttons={}; this.targets=[1,1,1,1]; this.buttonDown={};
    this.scene=new THREE.Scene(); this.camera=new THREE.PerspectiveCamera(34,1,.1,100); this.camera.position.set(0,1,21);
    this.renderer=new THREE.WebGLRenderer({antialias:true,alpha:true,powerPreference:'high-performance'});
    this.renderer.setPixelRatio(Math.min(devicePixelRatio,2)); this.renderer.shadowMap.enabled=true; this.renderer.shadowMap.type=THREE.PCFSoftShadowMap;
    this.renderer.outputColorSpace=THREE.SRGBColorSpace; this.renderer.toneMapping=THREE.ACESFilmicToneMapping; this.renderer.toneMappingExposure=1.0;
    const studio=new RoomEnvironment(), pmrem=new THREE.PMREMGenerator(this.renderer);
    this.scene.environment=pmrem.fromScene(studio,.035).texture; this.scene.environmentIntensity=.55; studio.dispose(); pmrem.dispose();
    container.appendChild(this.renderer.domElement);
    this.renderer.domElement.setAttribute('aria-label','Interactive 3D SP-1. Drag the faders to mix, click the wheel to change pitch, or drag the player to see every side.'); this.renderer.domElement.setAttribute('role','img');
    this.scene.add(new THREE.HemisphereLight(0xfffcf5,0x89908a,1.2));
    const light=new THREE.DirectionalLight(0xfffcf6,2.0); light.position.set(-5,9,14); light.castShadow=true; light.shadow.mapSize.set(2048,2048); Object.assign(light.shadow.camera,{left:-9,right:9,top:9,bottom:-9,near:.5,far:40}); light.shadow.bias=-.00015; light.shadow.normalBias=.018; this.scene.add(light);
    const rim=new THREE.DirectionalLight(0xe9edf3,1.3); rim.position.set(7,2,-8); this.scene.add(rim);
    this.body=new THREE.Group(); this.body.rotation.set(.10,-.25,-.09); this.scene.add(this.body);
    this.materials={
      shell:new THREE.MeshStandardMaterial({color:0xbfc2bd,roughness:.49,metalness:.52}),
      plastic:new THREE.MeshStandardMaterial({color:0xd1d0bd,roughness:.72,metalness:0}),
      recess:new THREE.MeshStandardMaterial({color:0x72766a,roughness:.9}),
      dark:new THREE.MeshStandardMaterial({color:0x101510,roughness:.9,metalness:.05}),
      silver:new THREE.MeshStandardMaterial({color:0xc6cbc5,roughness:.29,metalness:.8}),
    };
    this.build();this.configureHeights();
    // A floating object has no backdrop mesh: only the device receives shadows.
    this.controls=new OrbitControls(this.camera,this.renderer.domElement); this.controls.enablePan=false; this.controls.enableDamping=true; this.controls.dampingFactor=.09; this.controls.minDistance=12; this.controls.maxDistance=28; this.controls.minPolarAngle=0; this.controls.maxPolarAngle=Math.PI; this.controls.rotateSpeed=.65; this.controls.addEventListener('start',()=>this.events.orbit?.());
    this.ray=new THREE.Raycaster(); this.pointer=new THREE.Vector2();
    this.bind(); new ResizeObserver(()=>this.resize()).observe(container); this.resize(); this.renderer.setAnimationLoop(()=>this.render());
  }
  outline(w,h,r) {
    const s=new THREE.Shape(), x=-w/2, y=-h/2; r=Math.min(r,w/2,h/2);
    s.moveTo(x+r,y); s.lineTo(x+w-r,y); s.quadraticCurveTo(x+w,y,x+w,y+r); s.lineTo(x+w,y+h-r); s.quadraticCurveTo(x+w,y+h,x+w-r,y+h); s.lineTo(x+r,y+h); s.quadraticCurveTo(x,y+h,x,y+h-r); s.lineTo(x,y+r); s.quadraticCurveTo(x,y,x+r,y); return s;
  }
  hole(shape,w,h,r,x,y) {
    const path=new THREE.Path(this.outline(w,h,r).getPoints(16).map(p=>new THREE.Vector2(p.x+x,p.y+y))); shape.holes.push(path);
  }
  roundHole(shape,r,x,y) {const p=new THREE.Path(); p.absarc(x,y,r,0,Math.PI*2,true); shape.holes.push(p);}
  extrude(shape,d,bevel=.012) {
    const b=Math.min(bevel,d/4); const g=new THREE.ExtrudeGeometry(shape,{depth:d-2*b,bevelEnabled:b>0,bevelSegments:3,steps:1,bevelSize:b,bevelThickness:b,curveSegments:16}); g.translate(0,0,-d/2+b); return g;
  }
  mesh(geometry,mat,x,y,z,key) {
    const mesh=new THREE.Mesh(geometry,mat); mesh.position.set(x,y,z); mesh.castShadow=true; mesh.receiveShadow=true; this.body.add(mesh);
    if(key){mesh.userData.key=key; mesh.userData.home=mesh.position.clone(); this.buttons[key]=mesh;} return mesh;
  }
  block(w,h,d,r,x,y,z,mat,key) {return this.mesh(this.extrude(this.outline(w,h,r),d),mat,x,y,z,key);}
  disc(r,d,x,y,z,mat,key) {const m=this.mesh(new THREE.CylinderGeometry(r,r,d,48),mat,x,y,z,key); m.rotation.x=Math.PI/2; return m;}
  panel(shape,d,x,y,z,rx=0,ry=0) {const p=this.mesh(this.extrude(shape,d,.006),this.materials.shell,x,y,z); p.rotation.set(rx,ry,0); return p;}
  build() {
    const m=this.materials, xs=[-2.5,-1.2,.1,1.4], cornerRadius=.16;
    // The metal shell and plastic spine meet through the full enclosure thickness.
    this.block(1.08,9.2,1.15,cornerRadius,2.755,0,0,m.plastic);
    const front=this.outline(5.48,9.2,cornerRadius);
    xs.forEach(x=>{this.hole(front,.35,1.88,.175,x+.56,.02); this.hole(front,.38,.91,.06,x+.56,-2.93);});
    this.panel(front,.09,-.56,0,.532);
    // Solid inner chassis is below the controls, leaving the openings visibly recessed.
    this.block(5.23,8.92,.56,.1,-.56,0,-.12,m.dark);
    this.block(5.48,9.2,.065,cornerRadius,-.56,0,-.54,m.shell);
    this.backPanel();
    // End the flat walls at the corner tangents so they cannot square off the silhouette.
    const left=this.outline(1.09,9.2-2*cornerRadius,0); this.hole(left,.67,1.48,.17,0,2.85);
    this.panel(left,.08,-3.26,0,0,0,-Math.PI/2);
    const edgeCenter=-.56+cornerRadius/2;
    const top=this.outline(5.48-cornerRadius,1.09,0);
    for(let i=0;i<5;i++)for(const z of [-.105,.105])this.roundHole(top,.057,.2+i*.21-edgeCenter,z);
    for(const x of [-2.26,-1.2])this.roundHole(top,.34,x-edgeCenter,0);
    this.panel(top,.08,edgeCenter,4.556,0,-Math.PI/2);
    const bottom=this.outline(5.48-cornerRadius,1.09,0); this.hole(bottom,1.25,.37,.185,-1.8-edgeCenter,0);
    for(const x of [.1,1.4])this.roundHole(bottom,.257,x-edgeCenter,0);
    this.panel(bottom,.08,edgeCenter,-4.556,0,Math.PI/2);
    // Carry the same front/back outline around both left corners through the body depth.
    const corner=new THREE.Shape(), wall=.08, r=cornerRadius;
    corner.moveTo(0,0); corner.quadraticCurveTo(0,r,r,r);
    corner.lineTo(r,r-wall); corner.quadraticCurveTo(wall,r-wall,wall,0); corner.closePath();
    const cornerGeometry=this.extrude(corner,1.09,.006);
    for(const sign of [-1,1]){
      const turn=this.mesh(cornerGeometry,m.shell,-3.3,sign*(4.6-r),0); turn.scale.y=sign;
    }
    this.bottomPorts();
    xs.forEach((x,i)=>{
      this.block(.345,1.88,.02,.165,x,.02,.41,m.dark,`slider${i}`);
      this.block(.022,1.63,.025,.01,x,.02,.445,m.silver,`slider${i}`);
      const cap=new THREE.Group(); cap.position.set(x,.80,.52); this.body.add(cap);
      const collar=new THREE.Mesh(new THREE.CylinderGeometry(.135,.145,.10,48),m.dark); collar.rotation.x=Math.PI/2; collar.userData.key=`slider${i}`; cap.add(collar);
      const knob=new THREE.Mesh(new THREE.CylinderGeometry(.13,.14,.23,48),m.silver); knob.rotation.x=Math.PI/2; knob.position.z=.10; knob.castShadow=true; knob.receiveShadow=true; knob.userData.key=`slider${i}`; cap.add(knob); this.knobs.push(cap);
      const led=this.disc(.047,.012,x,-1.82,.581,new THREE.MeshStandardMaterial({color:0x4c5646,emissive:0xcdf2ac,emissiveIntensity:.03,roughness:.6})); this.leds.push(led);
      this.block(.36,.89,.025,.055,x,-2.93,.50,m.dark,`track${i}`);
      this.block(.30,.82,.13,.035,x,-2.93,.562,m.silver,`track${i}`);
    });
    const triangle=new THREE.Shape(); triangle.moveTo(-.14,-.12); triangle.lineTo(.14,-.12); triangle.lineTo(0,.13); triangle.closePath();
    this.mesh(new THREE.ShapeGeometry(triangle),new THREE.MeshBasicMaterial({color:0xb74236}),2.76,2.91,.579);
    for(const [key,y] of [['play',2.95],['function',-2.95]]){
      const socket=this.block(.59,1.57,.015,.065,3.302,y,0,m.dark); socket.rotation.y=Math.PI/2;
      const button=this.block(.53,1.47,.17,.06,3.355,y,0,m.plastic,key); button.rotation.y=Math.PI/2;
    }
    for(const y of [-2.86,-3.09])this.disc(.055,.012,2.76,y,.581,m.dark);
    // The status lights and small aperture are on the right edge, not its front.
    const aperture=this.disc(.067,.016,3.308,1.25,0,m.dark); aperture.rotation.set(0,0,-Math.PI/2);
    for(let i=0;i<4;i++){const led=this.disc(.046,.014,3.308,.76-i*.30,0,new THREE.MeshStandardMaterial({color:0x4c5646,emissive:0xcdf2ac,emissiveIntensity:0,roughness:.6})); led.rotation.set(0,0,-Math.PI/2); this.statusLeds.push(led);}
    for(const [i,x] of [-2.26,-1.2].entries()){
      const volume=this.disc(.316,.095,x,4.594,0,m.silver,i?'volumeUp':'volumeDown'); volume.rotation.set(0,0,0);
    }
    this.buildWheel();
    const pin=this.disc(.06,.014,-3.308,.7,0,m.recess); pin.rotation.set(0,0,Math.PI/2);
  }
  buildWheel() {
    const m=this.materials;
    const socket=this.block(.65,1.45,.025,.16,-3.22,2.85,0,m.recess); socket.rotation.y=-Math.PI/2;
    const inset=this.block(.57,1.35,.025,.13,-3.24,2.85,0,m.plastic,'rocker'); inset.rotation.y=-Math.PI/2;
    // Flat central face and angled shoulders, with a small manufactured edge bevel.
    // The rocker still pivots across the thickness of the enclosure.
    this.wheel=new THREE.Group(); this.wheel.position.set(-2.74,2.85,0); this.body.add(this.wheel);
    const profile=new THREE.Shape();
    profile.moveTo(-.645,-.34); profile.lineTo(-.645,.34);
    profile.lineTo(-.55,.49); profile.lineTo(-.40,.55);
    profile.lineTo(-.32,.55); profile.lineTo(-.32,-.55);
    profile.lineTo(-.40,-.55); profile.lineTo(-.55,-.49); profile.closePath();
    const geometry=new THREE.ExtrudeGeometry(profile,{depth:.508,bevelEnabled:true,bevelSegments:1,steps:1,bevelSize:.006,bevelThickness:.006,curveSegments:1}); geometry.translate(0,0,-.254);
    const plastic=m.plastic.clone(); plastic.flatShading=true;
    const rocker=new THREE.Mesh(geometry,plastic); rocker.castShadow=true; rocker.receiveShadow=true; rocker.userData.key='rocker'; this.wheel.add(rocker);
  }
  bottomPorts() {
    const m=this.materials;
    const inner=this.block(1.23,.35,.018,.17,-1.8,-4.48,0,m.dark); inner.rotation.x=Math.PI/2;
    const tongue=this.block(.92,.043,.06,.016,-1.8,-4.515,-.025,m.recess); tongue.rotation.x=Math.PI/2;
    for(const x of [.1,1.4]){
      const tube=new THREE.Mesh(new THREE.CylinderGeometry(.25,.25,.16,48,1,true),m.dark); tube.position.set(x,-4.495,0); tube.material=m.dark.clone(); tube.material.side=THREE.DoubleSide; this.body.add(tube);
      const end=this.disc(.25,.018,x,-4.425,0,m.dark); end.rotation.set(0,0,0);
      const contact=this.disc(.025,.025,x+.205,-4.50,.035,m.silver); contact.rotation.set(0,0,0);
    }
  }
  backPanel() {
    const m=this.materials;
    const seam=this.outline(5.34,9.02,.24); this.hole(seam,5.31,8.99,.23,0,0);
    const border=this.mesh(new THREE.ShapeGeometry(seam),m.recess,-.56,0,-.577); border.rotation.y=Math.PI;
    for(const [x,y] of [[1.78,4.12],[-2.82,-4.12]]){
      this.disc(.15,.02,x,y,-.582,m.recess); this.disc(.122,.027,x,y,-.6,m.silver);
      const a=this.block(.14,.025,.005,.005,x,y,-.618,m.dark); a.rotation.z=.3;
      const b=this.block(.025,.14,.005,.005,x,y,-.618,m.dark); b.rotation.z=.3;
    }
    // Rear markings transcribed from the reference photograph, in back-view coordinates.
    const canvas=document.createElement('canvas'); canvas.width=1024; canvas.height=1600; const c=canvas.getContext('2d'); c.fillStyle='#454941'; c.strokeStyle='#454941';
    c.font='34px Arial'; c.fillText('+',642,80); c.fillText('−',845,80); c.font='28px Arial'; c.fillText('F',922,245); c.fillText('R',922,365);
    c.font='23px Arial'; ['designed and','engineered by','kanye west and','teenage engineering'].forEach((s,i)=>c.fillText(s,60,1220+i*30));
    c.font='25px Arial'; c.fillText('YZY0020SP01 Y4KNFC80',60,1420); c.fillText('contains FCC-ID WAP3027',60,1452);
    c.lineWidth=8; c.beginPath(); c.moveTo(77,1111); c.lineTo(110,1130); c.moveTo(89,1096); c.lineTo(66,1135); c.stroke();
    c.beginPath(); for(let i=0;i<6;i++){const a=i*Math.PI/3; const x=146+21*Math.cos(a),y=1116+21*Math.sin(a); i?c.lineTo(x,y):c.moveTo(x,y);} c.closePath(); c.stroke();
    c.lineWidth=4;
    for(const x of [625,745]){c.beginPath();c.ellipse(x,1270,56,65,0,Math.PI*.35,Math.PI*1.65);c.stroke();} c.beginPath();c.moveTo(690,1270);c.lineTo(754,1270);c.stroke();
    c.strokeRect(846,1213,57,104);c.beginPath();c.moveTo(835,1207);c.lineTo(914,1207);c.moveTo(818,1190);c.lineTo(936,1338);c.moveTo(818,1338);c.lineTo(936,1190);c.stroke();
    c.lineWidth=3;c.strokeRect(145,1520,21,34);c.strokeRect(150,1512,11,7);
    c.beginPath();c.arc(412,1536,20,Math.PI,0);c.moveTo(392,1535);c.lineTo(392,1554);c.moveTo(432,1535);c.lineTo(432,1554);c.stroke();
    c.beginPath();c.moveTo(782,1510);c.lineTo(763,1537);c.lineTo(783,1537);c.lineTo(766,1561);c.stroke();
    const texture=new THREE.CanvasTexture(canvas); texture.colorSpace=THREE.SRGBColorSpace;
    const markings=this.mesh(new THREE.PlaneGeometry(5.15,8.68),new THREE.MeshBasicMaterial({map:texture,transparent:true,depthWrite:false}),-.56,0,-.583); markings.rotation.y=Math.PI; markings.castShadow=false;
  }
  hit(event) {
    const r=this.renderer.domElement.getBoundingClientRect(); this.pointer.set((event.clientX-r.left)/r.width*2-1,-(event.clientY-r.top)/r.height*2+1); this.ray.setFromCamera(this.pointer,this.camera);
    // Testing the shell too prevents grabbing a front control through the back panel.
    const hit=this.ray.intersectObject(this.body,true)[0]; return hit?.object.userData.key?hit:null;
  }
  bind() {
    const canvas=this.renderer.domElement;
    canvas.addEventListener('pointerdown',e=>{
      if(e.button!==0||this.hardwareMode)return; const hit=this.hit(e); if(!hit)return; e.stopImmediatePropagation(); this.controls.enabled=false; canvas.setPointerCapture(e.pointerId);
      let key=hit.object.userData.key;
      if(key==='rocker')key=this.body.worldToLocal(hit.point.clone()).y>=2.85?'forward':'rewind';
      this.drag={key,pointer:e.pointerId};
      if(key.startsWith('slider'))this.slide(e); else{this.setButton(key,true); this.events.down?.(key);}
    },true);
    canvas.addEventListener('pointermove',e=>{if(this.drag?.key.startsWith('slider'))this.slide(e); else if(!this.drag)canvas.style.cursor=this.hit(e)?'pointer':'grab';});
    const up=e=>{if(!this.drag||e.pointerId!==this.drag.pointer)return; if(!this.drag.key.startsWith('slider')){this.setButton(this.drag.key,false); this.events.up?.(this.drag.key);} this.drag=null; this.controls.enabled=true; if(canvas.hasPointerCapture(e.pointerId))canvas.releasePointerCapture(e.pointerId);};
    canvas.addEventListener('pointerup',up); canvas.addEventListener('pointercancel',up); canvas.addEventListener('lostpointercapture',up);
  }
  slide(e) {
    this.hit(e); const plane=new THREE.Plane(new THREE.Vector3(0,0,1),-.62).applyMatrix4(this.body.matrixWorld); const hit=new THREE.Vector3();
    if(this.ray.ray.intersectPlane(plane,hit)){this.body.worldToLocal(hit); this.events.gain(Number(this.drag.key.slice(6)),clamp((hit.y+.76)/1.56));}
  }
  configureHeights(){
    this.heightParts={};
    const part=(key,mesh,axis,sign=1)=>{
      const geometry=mesh.geometry,positions=geometry.attributes.position;
      geometry.computeBoundingBox();
      this.heightParts[key]={mesh,axis,original:positions.array.slice(),anchor:geometry.boundingBox[sign>0?'min':'max'][axis]};
    };
    part('wheel',this.wheel.children[0],'x',-1);
    for(const key of ['play','function'])part(key,this.buttons[key],'z');
    for(const key of ['volumeUp','volumeDown'])part(key,this.buttons[key],'y');
    for(let i=0;i<4;i++){part(`slider${i}`,this.knobs[i].children[1],'y');part(`track${i}`,this.buttons[`track${i}`],'z');}
    this.controlHeights=heightSettings();
    for(const [key,value] of Object.entries(this.controlHeights))this.setControlHeight(key,value);
  }
  setBodyHeight(percent){if(Number.isFinite(percent))this.body.scale.y=Math.max(60,Math.min(140,percent))/100;}
  setControlHeight(key,percent){
    const p=this.heightParts[key];if(!p||!Number.isFinite(percent))return;
    const value=Math.max(25,Math.min(300,percent));this.controlHeights[key]=value;
    // Lengthen the actual geometry from its seated end. Its mount, pivot,
    // fader carriage and button press/release positions stay unchanged.
    const geometry=p.mesh.geometry,positions=geometry.attributes.position,axis={x:0,y:1,z:2}[p.axis];
    for(let i=axis;i<positions.array.length;i+=3)positions.array[i]=p.anchor+(p.original[i]-p.anchor)*value/100;
    positions.needsUpdate=true;geometry.computeVertexNormals();geometry.computeBoundingBox();geometry.computeBoundingSphere();
  }
  setGain(i,v){if(!this.hardwareMode)this.targets[i]=v;}
  setHardwareMode(on){this.hardwareMode=on;if(!on)this.statusLeds.forEach(l=>{l.material.emissiveIntensity=0;l.material.color.setHex(0x4c5646);});}
  setHardwareFrame(frame){
    if(frame.faders){this.targets=[...frame.faders];this.knobs.forEach((k,i)=>k.position.y=-.76+1.56*this.targets[i]);}
    for(const key of ['track0','track1','track2','track3','play','function','forward','rewind','volumeUp','volumeDown'])this.setButton(key,Boolean(frame.buttons[key]));
    this.wheel.rotation.z=(this.buttonDown.forward?-.13:0)+(this.buttonDown.rewind?.13:0);
    for(const [lamps,values] of [[this.leds,frame.trackLeds],[this.statusLeds,frame.statusLeds]])lamps.forEach((lamp,i)=>{
      const value=values[i];lamp.material.emissiveIntensity=value?Math.sqrt(value)*3:0;
      lamp.material.color.setHex(value?0xe1eed7:0x4c5646);
    });
  }
  setButton(key,down){this.buttonDown[key]=down; const b=this.buttons[key]; if(!b?.userData.home)return; b.position.copy(b.userData.home); if(down){if(key.startsWith('track'))b.position.z-=.045;else if(key.startsWith('volume'))b.position.y-=.04;else b.position.x-=.055;}}
  view(which='3d') {
    // Flush orbit inertia so a view change lands precisely and stays there.
    this.controls.enableDamping=false; this.controls.update();
    this.body.rotation.set(0,0,0); this.controls.target.set(0,0,0);
    const positions={front:[0,0,21],back:[0,0,-21],left:[-19,0,8],right:[19,0,8],top:[0,19,7],bottom:[0,-19,7]};
    this.camera.position.set(...(positions[which]||[0,1,21])); if(which==='3d')this.body.rotation.set(.1,-.25,-.09);
    this.controls.update(); this.controls.enableDamping=true;
  }
  perspective(){this.view('3d');}
  front(){this.view('front');}
  resize(){const {width,height}=this.container.getBoundingClientRect(); this.camera.aspect=width/height; this.camera.fov=34; this.camera.updateProjectionMatrix(); this.renderer.setSize(width,height);}
  render(){this.events.frame?.();this.controls.update(); this.knobs.forEach((k,i)=>{k.position.y+=(-.76+1.56*this.targets[i]-k.position.y)*.28;}); const tilt=(this.buttonDown.forward?-.13:0)+(this.buttonDown.rewind ? .13 : 0); this.wheel.rotation.z+=(tilt-this.wheel.rotation.z)*.28; this.renderer.render(this.scene,this.camera);}
  lights(values,mutes){if(this.hardwareMode)return;this.leds.forEach((l,i)=>{const v=mutes[i]?0:values[i];l.material.emissiveIntensity=.03+v*2.5;l.material.color.setHex(v>.04?0xe1eed7:0x4c5646);});}
}
