"""A release cannot be promoted by a build or a claimed PASS alone."""
import copy
import importlib.util
import hashlib
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
