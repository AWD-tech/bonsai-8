#!/usr/bin/env python3
"""Validate the linked application and package a reviewable hardware-test release."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import struct
import subprocess
import zipfile
from elftools.elf.elffile import ELFFile
ROOT=Path(__file__).resolve().parents[1]
DIST=ROOT/'dist'
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
 DIST.mkdir(exist_ok=True)
 binary=ROOT/'build/zephyr/zephyr.bin';elf=ROOT/'build/zephyr/zephyr.elf'
 data=binary.read_bytes();sp,reset=struct.unpack_from('<II',data)
 versions=set(re.findall(rb'bonsai-8-(\d+\.\d+(?:\.\d+)?)',data))
 assert len(versions)==1, 'Binary must contain one identifiable Bonsai 8 version'
 version=versions.pop().decode()
 source=(ROOT/'firmware/src/dual_firmware.inc').read_text()
 assert 'bonsai-8-'+version in source, 'Build version does not match current source'
 assert 0x20000000<=sp<=0x20040000 and sp%8==0, 'Invalid initial stack'
 assert reset&1 and 0x20000<=reset-1<0x20000+len(data), 'Invalid reset vector'
 assert len(data)<=0xdf000, 'Application overlaps reserved flash'
 with elf.open('rb') as f:
  e=ELFFile(f);assert e['e_machine']=='EM_ARM'
  symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
  for name in ['main','dd_render','dual_storage_thread','dual_recovery_check','power_off','feed_wdt']:
   assert name in symbols, f'Missing required function {name}'
  for name in ['looper_audio_block','streamer_thread','trk','g_rring']:
   assert name not in symbols, f'Unexpected upstream engine linked: {name}'
  ram=[s for s in e.iter_sections() if s['sh_flags']&2 and 0x20000000<=s['sh_addr']<0x20040000]
  ram_used=max(s['sh_addr']+s['sh_size'] for s in ram)-0x20000000
  assert ram_used<=0x40000
 sources=[ROOT/'README.md',ROOT/'README-upstream.md',ROOT/'LICENSE',ROOT/'HARDWARE_TEST.md',ROOT/'USER_MANUAL.md',ROOT/'CONTRIBUTING.md']
 for directory in ['firmware','boards','tools','tests']:
  sources += [p for p in (ROOT/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts]
 manifest={
  'name':'Bonsai 8','version':version,'status':'Hardware-test candidate. See HARDWARE_TEST.md for version-specific measured results; packaging does not establish flashing or hardware verification.',
  'upstream_commit':'44ba1ecbec6c844dba7f47eacee94c53af8ab10d',
  'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
  'source_dirty':bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).strip()),
  'zephyr_commit':'75f67d766726351b30199f9a2bf55803d717a3be','sdk':'0.17.4',
  'application_address':'0x20000','initial_stack':hex(sp),'reset_vector':hex(reset),
  'binary_bytes':len(data),'ram_used_bytes':ram_used,'ram_available_bytes':262144,
  'binary_sha256':digest(binary),'automatic_storage_format':False,
  'tests':['C ASan/UBSan: recorder SPSC ring, P14S encoder, two-stage metadata publication and preservation, eight independent FX, impulse response and exact bypass',
   'C engine ASan/UBSan: eight voices, independent transports, mute/gain ramps, shared-deck starvation, counter wrap, speed bounds, pickup, codecs, saturation',
   'Python: firmware-compatible PCM14 encoding, metadata preservation, bad-table rejection, real ffmpeg resampling, hardware USB discovery and robust diagnostics',
   'C ASan/UBSan: physical telemetry queue, tap edges, overflow, counter wrap, stereo capture ordering and bounds',
   'ARM ELF/vector/flash/RAM validation; dual mixer linked; upstream audio rings absent'],
  'source_sha256':{str(p.relative_to(ROOT)):digest(p) for p in sorted(sources)}
 }
 name='bonsai-8-'+version
 shutil.copy2(binary,DIST/f'{name}.bin')
 shutil.copy2(elf,DIST/f'{name}.elf')
 shutil.copy2(ROOT/'README.md',DIST/'README.md')
 shutil.copy2(ROOT/'HARDWARE_TEST.md',DIST/'HARDWARE_TEST.md')
 for doc in ['USER_MANUAL.md','CONTRIBUTING.md']: shutil.copy2(ROOT/doc,DIST/doc)
 (DIST/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
 (DIST/'SHA256SUMS').write_text(f'{digest(binary)}  {name}.bin\n{digest(elf)}  {name}.elf\n')
 with zipfile.ZipFile(DIST/f'{name}.zip','w',zipfile.ZIP_DEFLATED) as z:
  for filename in [f'{name}.bin',f'{name}.elf','README.md','HARDWARE_TEST.md','manifest.json','SHA256SUMS','USER_MANUAL.md','CONTRIBUTING.md']:
   z.write(DIST/filename,filename)
  for path in sources: z.write(path,'source/'+str(path.relative_to(ROOT)))
 print(json.dumps({k:manifest[k] for k in ['binary_bytes','ram_used_bytes','binary_sha256','initial_stack','reset_vector']},indent=2))
 print(DIST/f'{name}.zip')
if __name__=='__main__': main()
