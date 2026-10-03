import ctypes as C
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('sp1',ROOT/'tools/sp1.py')
sp1=importlib.util.module_from_spec(spec);spec.loader.exec_module(sp1)
class Frame(C.Structure): _fields_=[('l',C.c_int16),('r',C.c_int16)]
class TransferTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory()
  lib=Path(cls.tmp.name)/'engine.dylib'
  subprocess.run(['cc','-shared','-O2','-std=c11','-fPIC',str(ROOT/'firmware/src/dual_engine.c'),str(ROOT/'firmware/src/bonsai_fx.c'),'-o',str(lib)],check=True)
  cls.lib=C.CDLL(str(lib));cls.lib.dd_decode.argtypes=[C.c_void_p,C.c_uint8,C.POINTER(Frame)]
 @classmethod
 def tearDownClass(cls): cls.tmp.cleanup()
 def test_encoder_matches_firmware(self):
  values=[-32768,-1,0,32767,17013,-19451,100,-100]*35
  data=sp1.pack_block(struct.pack('<280h',*values));out=(Frame*248)()
  self.assertEqual(self.lib.dd_decode(data,5,out),140)
  decoded=[v for i in range(140) for v in (out[i].l,out[i].r)]
  self.assertEqual(decoded,[(v>>2)*4 for v in values])
 def test_index_preserves_other_songs_and_padding(self):
  meta=bytearray(1024);struct.pack_into('<I',meta,0,sp1.MAGIC)
  meta[16]=1;meta[972:976]=b'\x01\0\0\0'
  table=sp1.x3_table(bytes(1536));table[16:32]=bytes(range(16))
  struct.pack_into('<H',table,6,sum(table[16:1040])%65536)
  new,ext=sp1.update_song(meta,table,7,[1000,980,900,997],[8,7,7,8])
  self.assertEqual(new[:8+7*44],meta[:8+7*44])
  self.assertEqual(new[972:976],meta[972:976])
  self.assertEqual(new[16+7*44:20+7*44],b'\1'*4)
  self.assertEqual(ext[16:32],table[16:32])
  for s in range(4):
   start,length,content,codec,flags,pan,rsv=struct.unpack_from('<IIIBBBB',ext,16+(7*4+s)*16)
   self.assertEqual((start,length,codec,flags,pan,rsv),(0,2000,5,1,128,0))
   self.assertEqual(content,[8,7,7,8][s])
  self.assertEqual(struct.unpack_from('<H',ext,6)[0],sum(ext[16:1040])%65536)
  self.assertEqual(sp1.x3_table(ext),ext)
 def test_bad_table_fails_closed(self):
  bad=bytearray([255])*1536
  result=sp1.x3_table(bad)
  self.assertEqual(result[16:1040],bytes(1024))
 def test_real_resampler(self):
  import wave
  src=Path(self.tmp.name)/'test.wav';target=Path(self.tmp.name)/'test.p14s'
  with wave.open(str(src),'wb') as w:
   w.setnchannels(2);w.setsampwidth(2);w.setframerate(48000)
   w.writeframes(struct.pack('<hh',12000,-8000)*4800)
  frames,blocks=sp1.encode(src,target,100)
  self.assertEqual(frames,2400);self.assertEqual(blocks,18)
  self.assertEqual(target.stat().st_size,18*512)
  out=(Frame*248)();data=target.read_bytes()
  self.assertEqual(self.lib.dd_decode(data[:512],5,out),140)
  self.assertEqual((out[80].l,out[80].r),(12000,-8000))
 def test_detects_usb_descriptor_seen_on_hardware(self):
  ports=[SimpleNamespace(device='/dev/cu.usbmodem1101',vid=0x2fe3,pid=0x5210,product='SP-1 Dual Deck'),
         SimpleNamespace(device='/dev/cu.unrelated',vid=0x1915,pid=0x5210,product='Other firmware')]
  with patch('serial.tools.list_ports.comports',return_value=ports):
   self.assertEqual(sp1.select_port(None),'/dev/cu.usbmodem1101')
 def test_bonsai_usb_and_runtime_identity(self):
  ports=[SimpleNamespace(device='/dev/cu.bonsai',vid=0x2fe3,pid=0x5210,product='Bonsai 8')]
  with patch('serial.tools.list_ports.comports',return_value=ports):
   self.assertEqual(sp1.select_port(None),'/dev/cu.bonsai')
  device=sp1.Device.__new__(sp1.Device);device.transfer=False
  device.serial=SimpleNamespace(write=lambda _:None,readline=lambda *args:b'{"firmware":"bonsai-8-0.4.0","storage":1}\n')
  self.assertEqual(device.status()['storage'],1)
 def test_status_skips_startup_lines(self):
  device=sp1.Device.__new__(sp1.Device);device.transfer=False
  lines=iter([b'\r\n',b'boot complete\n',b'{"firmware":"sp1-dual-deck-0.1","storage":0}\n'])
  written=[]
  device.serial=SimpleNamespace(write=written.append,readline=lambda *args:next(lines,b''))
  self.assertEqual(device.status()['storage'],0)
  self.assertEqual(written,[b'DDSTAT?\n'])
 def test_status_accepts_extended_capture_timing(self):
  import json
  device=sp1.Device.__new__(sp1.Device);device.transfer=False
  status={'firmware':'bonsai-8-0.4.3','storage':1,'capture_sof_us':4294967295,
          'capture_push_us':4294967295,'diagnostic_padding':'x'*1050}
  line=(json.dumps(status)+'\n').encode()
  device.serial=SimpleNamespace(write=lambda _:None,readline=lambda limit:line[:limit])
  self.assertEqual(device.status(),status)
 def test_status_rejects_incomplete_reply(self):
  device=sp1.Device.__new__(sp1.Device);device.transfer=False
  lines=iter([b'{"firmware":',b''])
  device.serial=SimpleNamespace(write=lambda data:None,readline=lambda *args:next(lines,b''))
  with self.assertRaisesRegex(RuntimeError,'status response'):device.status()
 def test_verified_burst_protocol_and_failure(self):
  device=sp1.Device.__new__(sp1.Device);written=[]
  device.serial=SimpleNamespace(write=written.append,flush=lambda:None,read=lambda n:b'b')
  data=bytes(range(256))*16
  device.write_verified_blocks(4096,data)
  self.assertEqual(written,[b'B'+struct.pack('<IB',4096+i,1)+data[i*512:(i+1)*512] for i in range(8)])
  device.serial.read=lambda n:b'E'
  with self.assertRaisesRegex(RuntimeError,'do not publish'):device.write_verified_blocks(4096,data)
  for invalid in [b'',bytes(511),bytes(4097),bytes(4608)]:
   with self.assertRaises(ValueError):device.write_verified_blocks(4096,invalid)
 def test_verified_burst_does_not_overrun_device_receive_ring(self):
  device=sp1.Device.__new__(sp1.Device)
  pending=0;overflow=False;written=[]
  def write(data):
   nonlocal pending,overflow
   pending+=len(data);overflow|=pending>1024;written.append(data)
  def acknowledge(n):
   nonlocal pending
   self.assertEqual(n,1);pending=0
   return b'E' if overflow else b'b'
  device.serial=SimpleNamespace(write=write,flush=lambda:None,
                               read=acknowledge)
  data=bytes(range(256))*16
  device.write_verified_blocks(4096,data)
  self.assertFalse(overflow)
  self.assertEqual(written,[b'B'+struct.pack('<IB',4096+i,1)+data[i*512:(i+1)*512] for i in range(8)])
 def test_verified_write_stops_at_first_failed_sector(self):
  device=sp1.Device.__new__(sp1.Device);written=[];replies=iter([b'b',b'E'])
  device.serial=SimpleNamespace(write=written.append,read=lambda n:next(replies))
  with self.assertRaisesRegex(RuntimeError,'block 4097'):
   device.write_verified_blocks(4096,bytes(4096))
  self.assertEqual(len(written),2)
if __name__=='__main__': unittest.main()
