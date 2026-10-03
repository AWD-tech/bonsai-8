import {renderMatched} from './match-dsp.js';
import {analyzeAudio} from './match-analysis.js';
self.onmessage=event=>{
 const {id,type,payload}=event.data||{};
 try{
  let previous=-1;
  const progress=value=>{if(value-previous>=.02||value===1){previous=value;self.postMessage({id,progress:value});}};
  const result=type==='analyze'?analyzeAudio(payload):type==='render'?renderMatched(payload,progress):null;
  if(!result)throw Error('Unknown matching operation.');
  self.postMessage({id,result},type==='render'?result.map(channel=>channel.buffer):[]);
 }catch(error){self.postMessage({id,error:error.message||'Audio preparation failed.'});}
};
