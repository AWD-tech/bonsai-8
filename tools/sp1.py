#!/usr/bin/env python3
"""Bonsai 8 transfer/diagnostics. Requires pyserial and ffmpeg for uploads."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import time

MAGIC = 0x53453341
X3_MAGIC = 0x53453358

def pack_block(pcm):
    pcm = pcm.ljust(560, b'\0')
    values = struct.unpack('<280h', pcm)
    block = bytearray(512)
    block[:4] = b'P14S'
    block[5], block[11] = 1, 0x5b
    struct.pack_into('<H', block, 12, 280)
    for g in range(70):
        bits = 0
        for sample in values[g*4:g*4+4]:
            bits = (bits << 14) | ((sample >> 2) & 0x3fff)
        block[16+g*7:23+g*7] = bits.to_bytes(7, 'big')
    return bytes(block)

def encode(source, output, max_blocks):
    proc = subprocess.Popen(['ffmpeg', '-v', 'error', '-nostdin', '-i', str(source),
        '-f', 's16le', '-acodec', 'pcm_s16le', '-ac', '2', '-ar', '24000', '-'], stdout=subprocess.PIPE)
    frames = blocks = 0
    try:
        with open(output, 'wb') as out:
            while True:
                data = proc.stdout.read(560)
                if not data: break
                if len(data) % 4: raise ValueError('Incomplete stereo frame from ffmpeg')
                if blocks >= max_blocks: raise ValueError('Stem exceeds device track capacity')
                frames += len(data)//4; blocks += 1
                out.write(pack_block(data))
        if proc.wait() != 0: raise ValueError(f'ffmpeg could not decode {source}')
        if not frames: raise ValueError(f'Empty stem: {source}')
        return frames, blocks
    finally:
        if proc.poll() is None: proc.terminate(); proc.wait()
        proc.stdout.close()

def x3_table(data):
    result = bytearray(data)
    valid = len(result) == 1536 and struct.unpack_from('<IH', result) == (X3_MAGIC, 1)
    valid = valid and struct.unpack_from('<H', result, 6)[0] == sum(result[16:1040]) % 65536
    if not valid:
        # Never invent records for other tracks. Invalid old table becomes empty.
        result = bytearray(1536)
        struct.pack_into('<IH', result, 0, X3_MAGIC, 1)
    return result

def update_song(meta, x3, slot, frames, counts):
    meta, x3 = bytearray(meta), x3_table(x3)
    length = max(frames)
    offset = 8 + slot*44
    struct.pack_into('<II', meta, offset, 65536, length*2)
    meta[offset+8:offset+12] = b'\1'*4
    blocks = (length+139)//140
    for s in range(4):
        struct.pack_into('<I', meta, offset+12+s*4, blocks)
        struct.pack_into('<I', meta, offset+28+s*4, 0)
        struct.pack_into('<I', meta, 716+(slot*4+s)*4, counts[s])
        struct.pack_into('<IIIBBBB', x3, 16+(slot*4+s)*16, 0, length*2, counts[s], 5, 1, 128, 0)
    meta[976+slot*2:978+slot*2] = b'\0\0'
    meta[1008+slot] = 0
    struct.pack_into('<H', x3, 6, sum(x3[16:1040]) % 65536)
    return meta, x3

class Device:
    def __init__(self, port):
        import serial
        self.serial = serial.Serial(port, 115200, timeout=5, write_timeout=5)
        self.transfer = False
        time.sleep(.15)
        self.serial.reset_input_buffer()
    def exact(self, n):
        result = self.serial.read(n)
        if len(result) != n: raise RuntimeError('Device timed out; check power and USB connection')
        return result
    def enter(self):
        self.serial.write(b'SP1XFER!P')
        if self.exact(4) != b'SP1!': raise RuntimeError('Unexpected firmware; transfer handshake failed')
        self.layout = struct.unpack('<6I', self.exact(24))
        if self.layout[:3] != (512, 16, 4) or self.layout[5] != MAGIC:
            raise RuntimeError('Incompatible storage layout')
        self.transfer = True
    def read(self, block):
        self.serial.write(b'R'+struct.pack('<I', block))
        if self.exact(1) != b'r': raise RuntimeError(f'Could not read block {block}')
        return self.exact(512)
    def write(self, block, data):
        if len(data) != 512: raise ValueError('Write must be exactly one sector')
        self.serial.write(b'W'+struct.pack('<I', block)+data)
        if self.exact(1) != b'w': raise RuntimeError(f'Write rejected at block {block}; stock/old storage is locked')
    def write_verified_blocks(self, block, data):
        """Firmware 0.2+: verify 1–8 sectors using acknowledged single writes."""
        if not data or len(data)%512 or len(data)>4096:
            raise ValueError('Verified burst must contain 1–8 whole sectors')
        # Firmware 0.2 drops requests larger than its 1024-byte RX ring.
        # Pacing multi-sector writes passed short probes but failed a longer
        # upload. Use one acknowledged sector at a time: it fits the ring and
        # avoids the driver's separate asynchronous multi-sector write path.
        for offset in range(0,len(data),512):
            address=block+offset//512
            self.serial.write(b'B'+struct.pack('<IB',address,1)+data[offset:offset+512])
            if self.exact(1)!=b'b':
                raise RuntimeError(f'Verified write rejected at block {address}; do not publish this song')
    def flush(self):
        self.serial.write(b'F')
        if self.exact(1) != b'f': raise RuntimeError('Flush/index validation failed')
    def status(self):
        self.serial.write(b'S' if self.transfer else b'DDSTAT?\n')
        # USB startup printk shares CDC with diagnostics. Wait for the actual
        # status record instead of treating a blank/banner line as JSON.
        deadline = time.monotonic()+5
        while time.monotonic() < deadline:
            line = self.serial.readline(1024)
            if not line: break
            try: state = json.loads(line)
            except (ValueError, UnicodeError): continue
            if isinstance(state, dict) and str(state.get('firmware', '')).startswith(('bonsai-8-', 'sp1-dual-deck-')):
                return state
        raise RuntimeError('No complete Dual Deck status response; check device power and firmware')
    def close(self):
        try:
            if self.transfer: self.serial.write(b'X'); self.exact(1)
        finally: self.serial.close()

def select_port(port):
    if port: return port
    from serial.tools.list_ports import comports
    # Zephyr 4.3's sample USB initializer uses 0x2fe3 despite SAMPLE_USBD_VID.
    # The product string prevents selecting an unrelated upstream looper.
    found = [p.device for p in comports() if p.vid in (0x2fe3, 0x1915)
             and p.pid == 0x5210 and p.product in ('Bonsai 8', 'SP-1 Dual Deck')]
    if len(found) != 1: raise ValueError('Use --port with the powered-on SP-1 serial port (not bootloader)')
    return found[0]

def upload(dev, slot, files, replace, verify):
    dev.enter()
    meta = dev.read(0)+dev.read(1)
    if struct.unpack_from('<I', meta)[0] != MAGIC:
        raise ValueError('Stock/old storage preserved. Back it up before using init --erase-all-songs.')
    present = any(meta[8+slot*44+8:8+slot*44+12])
    if present and not replace: raise ValueError('Slot contains audio. Choose an empty slot or use --replace.')
    # Pre-encode before any device mutation. Keep pinging while ffmpeg works by
    # leaving transfer mode; re-enter and re-read metadata before publishing.
    track_blocks = dev.layout[4]
    dev.serial.write(b'X'); dev.exact(1); dev.transfer=False
    with tempfile.TemporaryDirectory(prefix='sp1-stems-') as tmp:
        frames, counts, encoded = [], [], []
        for s, source in enumerate(files):
            path = Path(tmp)/f'{s}.p14s'
            n, blocks = encode(source, path, track_blocks)
            frames.append(n);counts.append(blocks);encoded.append(path)
        if max(frames) > 35840*643//2: raise ValueError('Song exceeds the supported eight-minute length')
        dev.enter();bulk = min(int(dev.status().get('bulk_write',0)),8)
        meta = dev.read(0)+dev.read(1)
        if struct.unpack_from('<I',meta)[0] != MAGIC: raise ValueError('Storage changed during preparation')
        if any(meta[16+slot*44:20+slot*44]) and not replace: raise ValueError('Slot is no longer empty')
        table = b''.join(dev.read(i) for i in range(3,6))
        if present:
            invalid = bytearray(meta);invalid[16+slot*44:20+slot*44] = b'\0'*4
            dev.write(1,invalid[512:]);dev.write(0,invalid[:512]);dev.flush()
        for s, path in enumerate(encoded):
            base = dev.layout[3]+(slot*4+s)*track_blocks
            with open(path,'rb') as stream:
                if verify and bulk>0:
                    for b in range(0,counts[s],bulk):
                        dev.write_verified_blocks(base+b,stream.read(min(bulk,counts[s]-b)*512))
                else:
                    for b in range(counts[s]):
                        data=stream.read(512);dev.write(base+b,data)
                        if verify and dev.read(base+b) != data: raise RuntimeError('Audio readback mismatch; upload remains unpublished')
            print(f'Stem {s+1}/4 transferred ({frames[s]/24000:.1f}s)',flush=True)
        meta, table = update_song(meta,table,slot,frames,counts)
        for i in range(3): dev.write(3+i,table[i*512:(i+1)*512])
        dev.write(1,meta[512:]);dev.write(0,meta[:512]);dev.flush()
        if dev.read(0)+dev.read(1) != meta: raise RuntimeError('Index readback mismatch')
        print(f'Song {slot+1} ready. Both decks are paused; choose this song on the device.')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port')
    sub=p.add_subparsers(dest='command',required=True)
    sub.add_parser('ports')
    status=sub.add_parser('status');status.add_argument('--seconds',type=int,default=0)
    sub.add_parser('list')
    init=sub.add_parser('init');init.add_argument('--erase-all-songs',action='store_true')
    up=sub.add_parser('upload');up.add_argument('--slot',type=int,required=True,choices=range(1,17))
    up.add_argument('--replace',action='store_true');up.add_argument('--skip-readback',action='store_true')
    up.add_argument('stems',nargs=4,type=Path)
    args=p.parse_args()
    if args.command=='ports':
        from serial.tools.list_ports import comports
        for port in comports(): print(port.device,port.description)
        return
    if args.command=='init' and not args.erase_all_songs:
        p.error('init erases the song index; back up first, then explicitly pass --erase-all-songs')
    dev=Device(select_port(args.port))
    try:
        if args.command=='status':
            deadline=time.monotonic()+args.seconds
            while True:
                print(json.dumps(dev.status()),flush=True)
                if time.monotonic()>=deadline: break
                time.sleep(1)
        elif args.command=='list':
            dev.enter();meta=dev.read(0)+dev.read(1)
            if struct.unpack_from('<I',meta)[0]!=MAGIC: print('Stock/old storage is preserved and write-locked.');return
            for slot in range(16): print(slot+1,list(meta[16+slot*44:20+slot*44]))
        elif args.command=='init':
            dev.enter();dev.serial.write(b'IERASE ALL SONGS!')
            if dev.exact(1)!=b'i': raise RuntimeError('Initialization rejected')
            print('Initialized empty Tape Looper 3.x storage; upload songs next.')
        elif args.command=='upload': upload(dev,args.slot-1,args.stems,args.replace,not args.skip_readback)
    finally: dev.close()
if __name__=='__main__':
    try: main()
    except (RuntimeError,ValueError,OSError) as exc: raise SystemExit(str(exc))
