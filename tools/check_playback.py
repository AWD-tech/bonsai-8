#!/usr/bin/env python3
"""Read-only hardware dropout check. Close the site's serial connection first.

Start the requested decks on the physical player before running this command.
No transfer mode, storage writes, transport changes or counter resets are used.
Exit 0: no new faults; 1: dropouts/faults; 2: test conditions changed/unavailable.
"""
import argparse
import json
import shutil
import subprocess
import time
from pathlib import Path

from sp1 import Device, select_port


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port')
    ap.add_argument('--seconds', type=float, default=8)
    ap.add_argument('--playing', choices=['both', 'a', 'b'], default='both')
    ap.add_argument('--mirror', action='store_true',
                    help='Exercise the site cadence: mirror every 30 ms, status every 500 ms')
    ap.add_argument('--output', type=Path, help='Optional local JSON evidence file')
    args = ap.parse_args()
    if not 1 <= args.seconds <= 300:
        ap.error('--seconds must be between 1 and 300')
    expected = {'both': [1, 1], 'a': [1, 0], 'b': [0, 1]}[args.playing]
    port = select_port(args.port)
    lsof = shutil.which('lsof')
    if not lsof:
        raise RuntimeError('lsof is required to check serial ownership')
    owner = subprocess.run([lsof, port], capture_output=True, text=True)
    if owner.returncode != 1 or owner.stdout:
        raise RuntimeError('Serial port is busy or ownership could not be checked')
    dev = Device(port)
    try:
        start = dev.status()
        if start['playing'] != expected:
            raise RuntimeError(f'Start decks {args.playing} on the device first')
        before = time.monotonic()
        if args.mirror:
            last_status = before
            while time.monotonic()-before < args.seconds:
                dev.serial.write(b'DDMIR?\n')
                line = dev.serial.readline(4096)
                mirror = json.loads(line)
                if mirror.get('mirror') != 1 or 'frames' not in mirror:
                    raise RuntimeError('Incomplete physical mirror response')
                if time.monotonic()-last_status >= .5:
                    dev.status()
                    last_status = time.monotonic()
                time.sleep(.03)
        else:
            time.sleep(args.seconds)
        end = dev.status()
        elapsed = time.monotonic() - before
    finally:
        dev.close()
    delta = [(b-a) & 0xffffffff for a, b in zip(start['underruns'], end['underruns'])]
    faults = {key: (end[key]-start[key]) & 0xffffffff for key in
              ['read_errors', 'bad_blocks', 'i2s_errors', 'crc_errors',
               'capture_underruns', 'capture_overflows', 'capture_errors']}
    stable = all(start[key] == end[key] for key in
                 ['firmware', 'reset', 'slots', 'playing', 'speed', 'gains', 'mute'])
    report = {'seconds': elapsed, 'start': start, 'end': end,
              'dropouts': delta, 'faults': faults, 'stable': stable, 'mirror': args.mirror}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({'seconds': round(elapsed, 3), 'slots': end['slots'],
                      'playing': end['playing'], 'dropouts': delta, 'faults': faults}))
    if not stable:
        print('INCONCLUSIVE: playback conditions changed during measurement')
        return 2
    if any(delta) or any(faults.values()):
        print('FAIL: playback underruns or storage/output faults')
        return 1
    print('PASS: no new playback underruns or storage/output faults')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (RuntimeError, ValueError, OSError) as exc:
        print(f'INCONCLUSIVE: {exc}')
        raise SystemExit(2)
