const clamp=(x,a=0,b=1)=>Math.max(a,Math.min(b,x));
export const KEY_NAMES=['C','C sharp','D','E flat','E','F','F sharp','G','A flat','A','B flat','B'];
// Confidence is a heuristic agreement score, not a probability. Constant-tempo
// music is assumed; a single grid cannot follow a live drummer or tempo changes.
export function analyzeTempo(samples,sampleRate){
 const hop=Math.max(1,Math.round(sampleRate*.01)),window=hop*2,count=Math.floor((samples.length-window)/hop),envelope=new Float32Array(Math.max(0,count));
 if(count<400)return {bpm:null,confidence:0,firstBeat:0,candidates:[]};
 const energies=new Float32Array(count);let total=0;
 for(let k=0;k<count;k++){let energy=0;for(let i=k*hop;i<k*hop+window;i++)energy+=samples[i]*samples[i];energies[k]=Math.sqrt(energy/window);total+=energy;}
 if(total/count<1e-9)return {bpm:null,confidence:0,firstBeat:0,candidates:[]};
 let peaks=0,power=0;
 for(let k=1;k<count;k++){let base=0;for(let j=Math.max(0,k-5);j<k;j++)base+=energies[j];base/=Math.min(5,k);const onset=Math.max(0,energies[k]-base);envelope[k]=onset;power+=onset*onset;}
 const rms=Math.sqrt(power/count);for(let k=1;k<count-1;k++)if(envelope[k]>rms*2&&envelope[k]>=envelope[k-1]&&envelope[k]>envelope[k+1])peaks++;
 if(peaks<4||rms<1e-5)return {bpm:null,confidence:0,firstBeat:0,candidates:[]};
 const fps=sampleRate/hop,candidates=[];
 for(let lag=Math.round(fps*60/200);lag<=Math.ceil(fps*60/50);lag++){let dot=0,a=0,b=0;for(let k=lag;k<count;k++){dot+=envelope[k]*envelope[k-lag];a+=envelope[k]**2;b+=envelope[k-lag]**2;}const strength=dot/Math.sqrt(a*b||1),bpm=60*fps/lag,prior=1-.12*Math.abs(Math.log2(bpm/120));candidates.push({bpm,strength,score:strength*prior,lag});}
 candidates.sort((a,b)=>b.score-a.score);const best=candidates[0],confidence=clamp(best.strength*Math.min(1,peaks/12));
 if(confidence<.25)return {bpm:null,confidence,firstBeat:0,candidates:candidates.slice(0,3).map(c=>c.bpm)};
 // Refine the coarse autocorrelation lag with a weighted line through onset
 // times. Sub-frame BPM resolution avoids accumulating 10 ms quantization drift.
 const events=[];for(let k=1;k<count-1;k++)if(envelope[k]>rms*2&&envelope[k]>=envelope[k-1]&&envelope[k]>envelope[k+1])events.push({time:(k*hop+window/2)/sampleRate,weight:envelope[k]});
 let period=60/best.bpm,phase=events[0].time;const intervals=[];
 for(let i=0;i<events.length;i++)for(let j=i+1;j<Math.min(events.length,i+9);j++){const diff=events[j].time-events[i].time,n=Math.round(diff/period);if(n>=1&&n<=12&&Math.abs(diff/n-period)<period*.1)intervals.push(diff/n);}
 if(intervals.length){intervals.sort((a,b)=>a-b);period=intervals[Math.floor(intervals.length/2)];}
 for(let iteration=0;iteration<2;iteration++){let sw=0,sx=0,sy=0,sxx=0,sxy=0;for(const e of events){const x=Math.round((e.time-phase)/period);if(Math.abs(e.time-phase-x*period)>period*.2)continue;const w=e.weight;sw+=w;sx+=x*w;sy+=e.time*w;sxx+=x*x*w;sxy+=x*e.time*w;}const denominator=sw*sxx-sx*sx;if(denominator>0){period=(sw*sxy-sx*sy)/denominator;phase=(sy-period*sx)/sw;}}
 let agreement=0;for(const e of events)if(Math.abs((e.time-phase)/period-Math.round((e.time-phase)/period))<.12)agreement++;
 const refinedConfidence=confidence*agreement/events.length,bpm=60/period;
 return {bpm:refinedConfidence>=.25?Math.round(bpm*100)/100:null,confidence:refinedConfidence,firstBeat:((phase%period)+period)%period,candidates:candidates.filter((c,i,a)=>i===0||a.slice(0,i).every(p=>Math.abs(c.bpm-p.bpm)>4)).slice(0,3).map(c=>Math.round(c.bpm*100)/100)};
}
function spectrum(input,offset,size){
 const real=new Float64Array(size),imag=new Float64Array(size);for(let i=0;i<size;i++)real[i]=(input[offset+i]||0)*(.5-.5*Math.cos(2*Math.PI*i/(size-1)));
 for(let i=1,j=0;i<size;i++){let bit=size>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)[real[i],real[j]]=[real[j],real[i]];}
 for(let length=2;length<=size;length*=2){const angle=-2*Math.PI/length,wr=Math.cos(angle),wi=Math.sin(angle);for(let start=0;start<size;start+=length){let ar=1,ai=0;for(let j=0;j<length/2;j++){const a=start+j,b=a+length/2,tr=ar*real[b]-ai*imag[b],ti=ar*imag[b]+ai*real[b];real[b]=real[a]-tr;imag[b]=imag[a]-ti;real[a]+=tr;imag[a]+=ti;const next=ar*wr-ai*wi;ai=ar*wi+ai*wr;ar=next;}}}
 const out=new Float64Array(size/2);for(let i=0;i<out.length;i++)out[i]=Math.hypot(real[i],imag[i])/size;return out;
}
function correlation(a,b){const am=a.reduce((s,x)=>s+x,0)/12,bm=b.reduce((s,x)=>s+x,0)/12;let sum=0,aa=0,bb=0;for(let i=0;i<12;i++){sum+=(a[i]-am)*(b[i]-bm);aa+=(a[i]-am)**2;bb+=(b[i]-bm)**2;}return sum/Math.sqrt(aa*bb||1);}
export function analyzeKey(samples,sampleRate){
 const size=4096,chroma=new Float64Array(12),windows=Math.min(96,Math.floor(samples.length/size));let energy=0;
 if(windows<2)return {name:'Unknown',root:null,mode:null,confidence:0};
 for(let w=0;w<windows;w++){const offset=Math.round(w*(samples.length-size)/Math.max(1,windows-1)),spec=spectrum(samples,offset,size);for(let bin=Math.ceil(65*size/sampleRate);bin<Math.min(spec.length-1,Math.floor(2100*size/sampleRate));bin++){const value=spec[bin];if(value<.0001||value<spec[bin-1]||value<spec[bin+1])continue;const midi=69+12*Math.log2(bin*sampleRate/size/440),nearest=Math.round(midi),distance=Math.abs(midi-nearest);if(distance>.4)continue;const weight=value*(1-distance/.5);chroma[((nearest%12)+12)%12]+=weight;energy+=weight;}}
 if(energy<.001)return {name:'Unknown',root:null,mode:null,confidence:0};
 // Simple tonic/third/fifth-weighted major/minor templates. This is an estimate,
 // not a harmonic guarantee; relative keys and ambiguous material are common.
 const profiles={major:[5,0,1,0,3,1,0,4,0,1,0,1],minor:[5,0,1,3,0,1,0,4,1,0,1,0]},scores=[];
 for(const [mode,profile] of Object.entries(profiles))for(let root=0;root<12;root++)scores.push({root,mode,score:correlation(chroma,Array.from({length:12},(_,pc)=>profile[(pc-root+12)%12]))});
 scores.sort((a,b)=>b.score-a.score);const best=scores[0],margin=best.score-scores[1].score,confidence=clamp(Math.max(0,best.score)*Math.min(1,margin/.2));
 if(best.score<.45||confidence<.15)return {name:'Unknown',root:null,mode:null,confidence};
 return {name:`${KEY_NAMES[best.root]} ${best.mode}`,root:best.root,mode:best.mode,confidence};
}
export function analyzeAudio({mono,keyMono=mono,sampleRate}){
 if(!(mono instanceof Float32Array)||!Number.isInteger(sampleRate)||sampleRate<8000||sampleRate>48000||mono.length>sampleRate*120)throw Error('Analysis accepts at most two minutes of mono PCM.');
 if(!(keyMono instanceof Float32Array)||keyMono.length!==mono.length)throw Error('Key analysis must share the song timeline.');
 for(const channel of [mono,keyMono])for(const value of channel)if(!Number.isFinite(value))throw Error('Audio contains invalid samples.');
 return {...analyzeTempo(mono,sampleRate),key:analyzeKey(keyMono,sampleRate),analyzedSeconds:mono.length/sampleRate,edited:false};
}
export function editGrid(analysis,{bpm=analysis?.bpm,firstBeat=analysis?.firstBeat??0}={}){if(!Number.isFinite(bpm)||bpm<20||bpm>300||!Number.isFinite(firstBeat)||firstBeat<0||firstBeat>480)throw Error('Use 20–300 BPM and a first beat between 0 and 480 seconds.');return {...analysis,bpm,firstBeat,edited:true};}
