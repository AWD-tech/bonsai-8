#!/usr/bin/env python3
"""Restore pinned sources without changing existing user checkouts or installing an SDK."""
from pathlib import Path
import shutil
import subprocess
import sys
import venv
ROOT=Path(__file__).resolve().parents[1]
ENV=ROOT/'.build-env'
DEPS=[
 ('zephyr','zephyr','75f67d766726351b30199f9a2bf55803d717a3be',
  ['arch','cmake','drivers','dts','include','kernel','lib','misc','modules','scripts','soc','subsys','samples/subsys/usb/common','share','snippets','boards/common','boards/shields']),
 ('modules/cmsis','cmsis','512cc7e895e8491696b61f7ba8066b4a182569b8',['CMSIS/Core','CMSIS/Include']),
 ('modules/cmsis_6','CMSIS_6','30a859f44ef8ab4dc8f84b03ed586fd16ccf9d74',['CMSIS/Core','zephyr']),
 ('modules/hal_nordic','hal_nordic','7858281d843468fe53c829995fb63f45a227387a',['nrfx','zephyr'])]
def main():
 if shutil.disk_usage(ROOT).free<500_000_000: raise SystemExit('At least 500 MB free is needed for build dependencies.')
 for location,repo,rev,paths in DEPS:
  dest=ENV/location
  def git(*args): subprocess.run(['git',*args],cwd=dest,check=True)
  if (dest/'.git').exists():
   actual=subprocess.check_output(['git','rev-parse','HEAD'],cwd=dest,text=True).strip()
   if actual!=rev: raise SystemExit(f'{dest} has another revision; leave it intact and choose a separate build directory.')
  else:
   dest.mkdir(parents=True,exist_ok=True)
   git('init','-q');git('remote','add','origin',f'https://github.com/zephyrproject-rtos/{repo}.git')
   git('fetch','--depth=1','--filter=blob:none','origin',rev)
   git('sparse-checkout','init','--cone');git('sparse-checkout','set',*paths);git('checkout','--detach','FETCH_HEAD')
  print('Pinned:',repo,rev,flush=True)
 for path in (ROOT/'firmware/zephyr-uac2-backport').iterdir():
  if path.is_file(): shutil.copy2(path,ENV/'zephyr/subsys/usb/device_next/class'/path.name)
 py=ENV/'python/bin/python'
 if not py.exists(): venv.EnvBuilder(with_pip=True).create(ENV/'python')
 subprocess.run([str(py),'-m','pip','install','--no-cache-dir','-r',str(ROOT/'tools/requirements-build.txt')],check=True)
 print('Dependencies ready. Set ZEPHYR_SDK_INSTALL_DIR if needed, then run tools/build.sh.')
if __name__=='__main__': main()
