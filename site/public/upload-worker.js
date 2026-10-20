import {encodeP14S} from './upload-codec.js?v=20261020-upload-01';
import {UploadSha256} from './upload-sha256.js?v=20261020-upload-01';
let digest;
self.onmessage=({data:{id,type,left,right,blocks,chunk}})=>{
 try{
  let value;
  if(type==='ready')value=true;
  else if(type==='encode'){value=new Uint8Array(blocks*512);for(let b=0;b<blocks;b++)value.set(encodeP14S(left,right,b*140),b*512);}
  else if(type==='hash-start'){digest=new UploadSha256();value=true;}
  else if(type==='hash-chunk'){digest.update(chunk);value=true;}
  else if(type==='hash-end'){value=digest.hex();digest=null;}
  else throw Error('Unknown upload preparation request.');
  self.postMessage({id,value},value instanceof Uint8Array?[value.buffer]:[]);
 }catch(error){self.postMessage({id,error:error.message});}
};
