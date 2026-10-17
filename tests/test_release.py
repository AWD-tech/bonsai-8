"""A release cannot be promoted by a build or a claimed PASS alone."""
import copy
import importlib.util
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('bonsai_release', ROOT/'tools/release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)

def sample():
    return dict(firmware='bonsai-8-0.4.1', reset=4, slots=[1,2], playing=[1,1],
                gains=[256]*8, mute=[0,0], speed=[65536]*2, master=128,
                fx=[0]*8, record={'state':0}, capture_active=0,
                capture_packets=0, underruns=[0,0], read_errors=0,
                bad_blocks=0, crc_errors=0, i2s_errors=0, clips=0, fx_clips=0,
                capture_underruns=0, capture_overflows=0, capture_errors=0)

def report():
    a,b=sample(),sample()
    return dict(seconds=20.01, verdict='PASS', error=None, changed=[],
                samples=[{'time':0,'status':a},{'time':20,'status':b}],
                mirror_replies=0)

class ReleaseTests(unittest.TestCase):
    def test_046_requires_physical_feature_evidence_after_steady_playback(self):
        with self.assertRaisesRegex(ValueError, 'physical feature evidence'):
            release.validate_features(None, '0.4.6', 'a'*64)
        self.assertEqual(release.validate_features(None, '0.4.5', 'a'*64), {})

    def test_cli_never_stages_verified_046_with_only_steady_reports(self):
        with tempfile.TemporaryDirectory() as directory:
            folder=Path(directory)
            binary=struct.pack('<II',0x20020000,0x20009)+b'bonsai-8-0.4.6'+b'\0'*64
            digest=hashlib.sha256(binary).hexdigest()
            (folder/'application.bin').write_bytes(binary)
            manifest=dict(version='0.4.6',effects=[],source_dirty=False,
                          automatic_storage_format=False,application_address='0x20000',
                          binary_bytes=len(binary),binary_sha256=digest,source_commit='fixture')
            events=[dict(event='preflight',sha256=digest,bytes=len(binary),
                         application_address='0x20000'),
                    dict(event='response',command='0x48',reply='0x49'),
                    dict(event='response',command='0x48',reply='0x49'),
                    dict(event='flash_finalized'),dict(event='application_start_requested')]
            (folder/'flash.jsonl').write_text('\n'.join(map(json.dumps,events)))
            dry=report()
            for item in dry['samples']:
                item['status'].update(firmware='bonsai-8-0.4.6',effects=[])
            usb=copy.deepcopy(dry);usb['mirror_replies']=180
            for item in usb['samples']:item['status']['capture_active']=1
            usb['samples'][-1]['status']['capture_packets']=20000
            for name,value in [('manifest',manifest),('library',{'slots':[]}),
                               ('dry',dry),('usb',usb)]:
                (folder/(name+'.json')).write_text(json.dumps(value))
            stage=folder/'public'
            result=subprocess.run([sys.executable,str(ROOT/'tools/release.py'),
                '--manifest',str(folder/'manifest.json'),
                '--binary',str(folder/'application.bin'),
                '--flash',str(folder/'flash.jsonl'),
                '--library-before',str(folder/'library.json'),
                '--library-after',str(folder/'library.json'),
                '--dry',str(folder/'dry.json'),'--usb-mirror',str(folder/'usb.json'),
                '--stage',str(stage)],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('Missing physical feature evidence',result.stderr)
            self.assertFalse(stage.exists())

    def test_feature_evidence_rejects_host_claims_and_checks_artifact_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            folder=Path(directory)
            artifact=folder/'trace.json'
            artifact.write_text('{"test_fixture":true}\n')
            digest=hashlib.sha256(artifact.read_bytes()).hexdigest()
            checks={name: {'verdict':'PASS', 'environment':'physical-device',
                          'coverage':list(coverage), 'evidence':[
                              {'path':'trace.json','sha256':digest}]}
                    for name,coverage in release.FEATURE_CHECKS.items()}
            evidence={'firmware':'bonsai-8-0.4.6','binary_sha256':'a'*64,
                      'checks':checks}
            path=folder/'features.json'
            def validate(value):
                path.write_text(json.dumps(value))
                return release.validate_features(path,'0.4.6','a'*64)
            # This is a validator fixture, not physical test evidence.
            summary=validate(evidence)
            self.assertEqual(set(summary),set(release.FEATURE_CHECKS))
            self.assertNotIn(str(folder),json.dumps(summary))
            for name in release.FEATURE_CHECKS:
                for field,value in [('verdict','PENDING'),('environment','host'),
                                    ('coverage',[]),('evidence',[])]:
                    bad=copy.deepcopy(evidence);bad['checks'][name][field]=value
                    with self.assertRaises(ValueError):validate(bad)
                bad=copy.deepcopy(evidence);del bad['checks'][name]
                with self.assertRaises(ValueError):validate(bad)
            for field,value in [('firmware','bonsai-8-0.4.5'),
                                ('binary_sha256','b'*64)]:
                bad=copy.deepcopy(evidence);bad[field]=value
                with self.assertRaises(ValueError):validate(bad)
            artifact.write_text('changed')
            with self.assertRaisesRegex(ValueError,'hash mismatch'):validate(evidence)
            artifact.unlink()
            with self.assertRaisesRegex(ValueError,'missing'):validate(evidence)

    def test_no_effects_release_requires_zero_effect_state_and_usb(self):
        self.assertEqual(release.playback_cases({'version':'0.4.5','effects':[]}),
                         ('dry','usb_mirror'))
        for effects in [None,['filter']]:
            with self.assertRaises(ValueError):
                release.playback_cases({'version':'0.4.5','effects':effects})
        r=report()
        for item in r['samples']:
            item['status'].update(firmware='bonsai-8-0.4.5',effects=[])
        release.validate_playback(r,'0.4.5','dry')
        for case in ['filter','echo','reverb','usb_mirror']:
            with self.assertRaises(ValueError): release.validate_playback(r,'0.4.5',case)
        for item in r['samples']: item['status']['capture_active']=1
        r['samples'][-1]['status']['capture_packets']=20000
        r['mirror_replies']=500
        release.validate_playback(r,'0.4.5','usb_mirror')
        for item in r['samples']: item['status']['fx'][0]=1<<16
        with self.assertRaises(ValueError): release.validate_playback(r,'0.4.5','usb_mirror')

    def test_filter_only_release_requires_its_declared_capability(self):
        self.assertEqual(release.playback_cases({'version':'0.4.3'}), release.CASES)
        self.assertEqual(release.playback_cases({'version':'0.4.4','effects':['filter']}),
                         ('dry','filter','usb_mirror'))
        for effects in [None, [], ['echo'], ['filter','echo']]:
            with self.assertRaises(ValueError):
                release.playback_cases({'version':'0.4.4','effects':effects})

    def test_filter_only_gate_requires_eight_full_filters_even_with_usb(self):
        r=report()
        for sample in r['samples']:
            sample['status'].update(firmware='bonsai-8-0.4.4',effects=['filter'],
                                    fx=[(1<<16)|256]*8)
        release.validate_playback(r,'0.4.4','filter')
        for case in ['echo','reverb']:
            with self.assertRaises(ValueError): release.validate_playback(r,'0.4.4',case)
        r['mirror_replies']=500
        for sample in r['samples']:sample['status']['capture_active']=1
        r['samples'][-1]['status']['capture_packets']=20000
        release.validate_playback(r,'0.4.4','usb_mirror')
        for invalid in [0, (1<<16)|249, (2<<16)|256]:
            bad=copy.deepcopy(r)
            for sample in bad['samples']:sample['status']['fx'][7]=invalid
            with self.assertRaises(ValueError): release.validate_playback(bad,'0.4.4','usb_mirror')

    def test_accepts_measured_eight_stem_dry_run(self):
        release.validate_playback(report(), '0.4.1', 'dry')

    def test_endpoint_adc_noise_is_bounded_to_one_level(self):
        r=report();r['samples'][-1]['status']['gains'][2]=255
        release.validate_playback(r,'0.4.1','dry')
        for value in [254,249,0,257]:
            r['samples'][-1]['status']['gains'][2]=value
            with self.assertRaises(ValueError):release.validate_playback(r,'0.4.1','dry')

    def test_recalculates_counters_instead_of_trusting_verdict(self):
        r=report();r['samples'][-1]['status']['underruns'][1]=1
        with self.assertRaises(ValueError): release.validate_playback(r,'0.4.1','dry')

    def test_rejects_zero_gain_and_changed_intermediate_sample(self):
        for key,value in [('gains',[256]*7+[0]),('master',64),('playing',[1,0])]:
            r=report();middle=copy.deepcopy(r['samples'][0]);middle['time']=10
            middle['status'][key]=value;r['samples'].insert(1,middle)
            with self.assertRaises(ValueError): release.validate_playback(r,'0.4.1','dry')

    def test_rejects_wrong_firmware_and_short_run(self):
        for version in ['0.4.0','0.5.0']:
            with self.assertRaises(ValueError): release.validate_playback(report(),version,'dry')
        r=report();r['seconds']=3
        with self.assertRaises(ValueError): release.validate_playback(r,'0.4.1','dry')

    def test_requires_requested_effect_and_usb_workload(self):
        for case in ['filter','echo','reverb','usb_mirror']:
            with self.assertRaises(ValueError): release.validate_playback(report(),'0.4.1',case)
        for kind,case in [(1,'filter'),(2,'echo'),(3,'reverb')]:
            r=report()
            for s in r['samples']:s['status']['fx'][0]=(kind<<16)|256
            release.validate_playback(r,'0.4.1',case)
        r=report();r['mirror_replies']=500
        for s in r['samples']:s['status']['capture_active']=1
        r['samples'][-1]['status']['capture_packets']=20000
        release.validate_playback(r,'0.4.1','usb_mirror')

    def test_library_presence_and_lengths_must_match(self):
        before={'slots':[{'slot':1,'present':[1]*4,'frames':1000}]}
        release.validate_library(before,copy.deepcopy(before))
        after=copy.deepcopy(before);after['slots'][0]['frames']=900
        with self.assertRaises(ValueError):release.validate_library(before,after)

    def test_flash_must_match_binary_and_have_both_finalizations(self):
        binary=b'reviewed application';digest=hashlib.sha256(binary).hexdigest()
        manifest={'binary_sha256':digest}
        events=[{'event':'preflight','sha256':digest,'bytes':len(binary),
                 'application_address':'0x20000'},
                {'event':'response','command':'0x48','reply':'0x49'},
                {'event':'response','command':'0x48','reply':'0x49'},
                {'event':'flash_finalized'},{'event':'application_start_requested'}]
        release.validate_flash(events,binary,manifest)
        for changed in [events[:-1],events[:2]+events[3:],
                        events+[{'event':'failure'}]]:
            with self.assertRaises(ValueError):release.validate_flash(changed,binary,manifest)
        with self.assertRaises(ValueError):release.validate_flash(events,binary+b'x',manifest)
        events[0]['application_address']='0x0'
        with self.assertRaises(ValueError):release.validate_flash(events,binary,manifest)

    def test_checks_every_intermediate_counter_and_sample_clock(self):
        r=report();middle=copy.deepcopy(r['samples'][0]);middle['time']=10
        middle['status']['fx_clips']=1;r['samples'].insert(1,middle)
        with self.assertRaises(ValueError):release.validate_playback(r,'0.4.1','dry')
        for time in [-1,1,21]:
            r=report();r['samples'][0]['time']=time
            with self.assertRaises(ValueError):release.validate_playback(r,'0.4.1','dry')

    def test_duplicate_slot_is_not_valid_preservation_evidence(self):
        duplicate={'slots':[{'slot':1,'present':[1]*4,'frames':1000}]*2}
        with self.assertRaises(ValueError):release.validate_library(duplicate,duplicate)

if __name__=='__main__':unittest.main()
