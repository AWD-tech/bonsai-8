export const DEFAULT_BODY_HEIGHT=97;
export const CONTROL_HEIGHTS=[
 {key:'wheel',label:'Side wheel',view:'left',default:143},
 {key:'play',label:'Play button',view:'right',default:221},
 {key:'function',label:'Function button',view:'right',default:221},
 ...Array.from({length:4},(_,i)=>({key:`slider${i}`,label:`Fader ${i+1}`,view:'3d',default:100})),
 ...Array.from({length:4},(_,i)=>({key:`track${i}`,label:`Track ${i+1} button`,view:'3d',default:150})),
 {key:'volumeUp',label:'Volume +',view:'top',default:100},
 {key:'volumeDown',label:'Volume −',view:'top',default:100},
];
export function heightSettings(saved={}){return Object.fromEntries(CONTROL_HEIGHTS.map(c=>[c.key,Number.isFinite(saved?.[c.key])?Math.max(25,Math.min(300,saved[c.key])):c.default]));}
