#!/usr/bin/env python3
"""Promote a pinned application only after measured device release gates pass.

This tool reads evidence and stages static release files. It never opens USB,
flashes a device, formats storage, pushes Git, or publishes a website.
"""
import argparse
import hashlib
import json
import re
import shutil
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CASES = ('dry', 'filter', 'echo', 'reverb', 'usb_mirror')
COUNTERS = ('read_errors', 'bad_blocks', 'i2s_errors', 'crc_errors',
            'capture_underruns', 'capture_overflows', 'capture_errors',
            'clips', 'fx_clips')
STABLE = ('firmware', 'reset', 'slots', 'playing', 'speed',
          'mute', 'master', 'fx', 'record', 'capture_active')

def require(condition, message):
    if not condition:
        raise ValueError(message)

def filter_only_version(version):
    require(isinstance(version, str) and re.fullmatch(r'\d+\.\d+\.\d+', version),
            'Invalid release version')
    return tuple(map(int, version.split('.'))) >= (0, 4, 4)

def playback_cases(manifest):
    if filter_only_version(manifest.get('version')):
        require(manifest.get('effects') == ['filter'], 'Filter-only capability missing')
        return ('dry', 'filter', 'usb_mirror')
    return CASES

def delta(before, after):
    require(isinstance(before, int) and isinstance(after, int), 'Invalid counter')
    return (after - before) & 0xffffffff

def stable_gains(states):
    """Allow one ADC level of endpoint noise, never a changed mixing workload."""
    gains = [s.get('gains', []) for s in states]
    return bool(gains) and all(len(g) == 8 for g in gains) and all(
        250 <= min(g[i] for g in gains) <= max(g[i] for g in gains) <= 256
        and max(g[i] for g in gains) - min(g[i] for g in gains) <= 1
        for i in range(8))

def validate_playback(report, version, case):
    require(case in CASES, 'Unknown playback gate')
    filter_only = filter_only_version(version)
    require(not filter_only or case not in ('echo', 'reverb'), 'Effect is no longer supported')
    require(report.get('seconds', 0) >= 20, 'Need at least twenty measured seconds')
    require(report.get('verdict') == 'PASS' and not report.get('error')
            and not report.get('changed'), 'Measurement was not a stable PASS')
    samples = report.get('samples', [])
    require(len(samples) >= 2, 'Missing measured samples')
    times = [s.get('time', -1) for s in samples]
    require(times[0] == 0 and times[-1] >= 20
            and all(b > a for a, b in zip(times, times[1:])), 'Invalid sample clock')
    states = [s['status'] for s in samples]
    require(stable_gains(states), 'Stem gains changed beyond one ADC level')
    first = states[0]
    for status in states:
        require(status.get('firmware') == 'bonsai-8-' + version, 'Wrong runtime version')
        if filter_only:
            require(status.get('effects') == ['filter'], 'Runtime effect capability mismatch')
        require(status.get('playing') == [1, 1] and status.get('mute') == [0, 0],
                'Both decks and every stem must be audible')
        gains = status.get('gains', [])
        require(len(gains) == 8 and all(250 <= g <= 256 for g in gains),
                'All eight stem gains must be full')
        require(status.get('master', 0) > 0, 'Master is silent')
        require(status.get('record', {}).get('state') == 0, 'Recording changes workload')
        require(all(key in status and status[key] == first[key] for key in STABLE),
                'Controls or runtime changed during measurement')
        require(len(status.get('underruns', [])) == 2, 'Missing dropout counters')
    for a, b in zip(states, states[1:]):
        require(not any(delta(x, y) for x, y in zip(a['underruns'], b['underruns'])),
                'New playback dropouts')
        require(all(key in a and key in b and delta(a[key], b[key]) == 0
                    for key in COUNTERS), 'New fault or clipping counter')
    fx = first.get('fx', [])
    require(len(fx) == 8, 'Missing effect settings')
    if filter_only:
        require(all(value >> 16 in (0, 1) and (value & 0xffff) <= 256 for value in fx),
                'Unsupported runtime effect setting')
        if case in ('filter', 'usb_mirror'):
            require(all(value >> 16 == 1 and (value & 0xffff) >= 250 for value in fx),
                    'All eight filters must be at full amount')
    if case == 'dry':
        require(all((value & 0xffff) == 0 for value in fx), 'Dry test had active effects')
    elif case in ('filter', 'echo', 'reverb'):
        kind = {'filter': 1, 'echo': 2, 'reverb': 3}[case]
        require(any(value >> 16 == kind and (value & 0xffff) >= 250 for value in fx),
                'Requested effect was not measured at full amount')
    elif case == 'usb_mirror':
        require(first.get('capture_active') == 1 and report.get('mirror_replies', 0) >= 100,
                'USB capture and mirroring must both be active')
        require(delta(first.get('capture_packets'), states[-1].get('capture_packets')) > 0,
                'USB audio packets did not advance')
    return {'seconds': report['seconds'], 'samples': len(samples),
            'mirror_replies': report.get('mirror_replies', 0), 'dropouts': [0, 0]}

def validate_library(before, after):
    def index(value):
        slots = value.get('slots', [])
        require(isinstance(slots, list), 'Missing library listing')
        result = {}
        for song in slots:
            slot = song.get('slot')
            require(isinstance(slot, int) and 1 <= slot <= 16 and slot not in result,
                    'Invalid or duplicate song slot')
            result[slot] = (song.get('present'), song.get('frames'))
        return result
    require(index(before) == index(after), 'Library presence or exact lengths changed')

def validate_flash(events, binary, manifest):
    require(not any(e.get('event') == 'failure' for e in events), 'Flash failed')
    preflight = [e for e in events if e.get('event') == 'preflight']
    require(len(preflight) == 1, 'Missing unique flash preflight')
    item = preflight[0]
    require(item.get('sha256') == hashlib.sha256(binary).hexdigest()
            == manifest.get('binary_sha256') and item.get('bytes') == len(binary),
            'Flashed binary does not match packaged hash')
    require(item.get('application_address') in ('0x20000', 131072), 'Wrong flash region')
    responses = [e for e in events if e.get('event') == 'response']
    require(sum(e.get('command') == '0x48' and e.get('reply') == '0x49'
                for e in responses) == 2, 'Missing both updater finalizations')
    require(any(e.get('event') == 'flash_finalized' for e in events)
            and any(e.get('event') == 'application_start_requested' for e in events),
            'Flash did not reach application start')

def load(path):
    return json.loads(Path(path).read_text())

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--flash', type=Path, required=True)
    p.add_argument('--library-before', type=Path, required=True)
    p.add_argument('--library-after', type=Path, required=True)
    for case in CASES:
        p.add_argument('--' + case.replace('_', '-'), type=Path)
    p.add_argument('--stage', type=Path, help='Target public firmware directory')
    a = p.parse_args()
    manifest, binary = load(a.manifest), a.binary.read_bytes()
    version = manifest.get('version', '')
    require(re.fullmatch(r'\d+\.\d+\.\d+', version), 'Invalid release version')
    require(manifest.get('source_dirty') is False, 'Package came from uncommitted source')
    require(manifest.get('automatic_storage_format') is False, 'Storage initialization forbidden')
    require(manifest.get('application_address') == '0x20000', 'Not an application-only package')
    require(len(binary) == manifest.get('binary_bytes') and 8 < len(binary) <= 0xdf000,
            'Invalid application size')
    sp, reset = struct.unpack_from('<II', binary)
    require(0x20000000 <= sp <= 0x20040000 and sp % 8 == 0 and reset & 1
            and 0x20000 <= reset - 1 < 0x20000 + len(binary), 'Invalid application vectors')
    require(('bonsai-8-' + version).encode() in binary, 'Binary version mismatch')
    validate_flash([json.loads(line) for line in a.flash.read_text().splitlines() if line],
                   binary, manifest)
    validate_library(load(a.library_before), load(a.library_after))
    cases = playback_cases(manifest)
    require(all(getattr(a, case) is not None for case in cases),
            'Missing required playback reports: ' + ', '.join(cases))
    gates = {case: validate_playback(load(getattr(a, case)), version, case) for case in cases}
    public = {'product': 'Bonsai 8', 'version': version, 'status': 'verified',
              'bytes': len(binary), 'sha256': hashlib.sha256(binary).hexdigest(),
              'application_address': 0x20000, 'file': a.binary.name,
              'source_commit': manifest['source_commit'], 'hardware_gates': gates,
              'effects': manifest.get('effects', ['filter', 'echo', 'reverb']),
              'library_preservation': 'Same occupied slots, stem presence and lengths after application-only update; no audio readback implied',
              'limits': ['Tests cover listed steady workloads, not all possible combinations',
                         'Updater acknowledgements verified; flash readback unavailable']}
    if a.stage:
        a.stage.mkdir(parents=True, exist_ok=True)
        target = a.stage / a.binary.name
        require(not target.exists() or target.read_bytes() == binary,
                'Refusing to replace a different binary under an existing version')
        shutil.copyfile(a.binary, target)
        history = a.stage / 'releases'
        history.mkdir(exist_ok=True)
        content = json.dumps(public, indent=2) + '\n'
        (history / (version + '.json')).write_text(content)
        (a.stage / 'manifest.json').write_text(content)
    print(json.dumps(public, indent=2))

if __name__ == '__main__':
    main()
