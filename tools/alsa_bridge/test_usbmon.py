import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import shutil
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

import analyze_usbmon as a
import usbmon_capture as c
import prepare_offline as prep


def record(ident, event, payload, descriptors, status=0, device=7):
    header = bytearray(64)
    desc = b''.join(struct.pack('<iIII', *d, 0) for d in descriptors)
    struct.pack_into('<QBBBBHccqiiII', header, 0, ident, ord(event), 0, 0x83, device, 1,
                     b'-', b'\0' if payload else b'<', 1, 0, status,
                     sum(d[2] for d in descriptors), len(desc)+len(payload))
    struct.pack_into('<iIiiII', header, 40, int(any(d[0] for d in descriptors)), len(descriptors), 1, 100, 0, len(descriptors))
    body = bytes(header)+desc+payload
    return struct.pack('<IIII', 1, 0, len(body), 64+len(desc)+sum(d[2] for d in descriptors))+body


class UsbTests(unittest.TestCase):
    def setUp(self):
        base=Path(os.environ.get('BROTRACKER_TEST_DIR',Path(__file__).resolve().parents[2]/'build/usbmon-tests'))
        base.mkdir(parents=True,exist_ok=True)
        self.tmp=tempfile.TemporaryDirectory(prefix='synthetic-usbmon-',dir=base);self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.usb=self.root/'usb';self.usb.mkdir();self.pcm=self.root/'pcm';self.pcm.mkdir()
        (self.usb/'target.json').write_text(json.dumps(dict(bus=1,device=7,endpoint=0x83)))
        (self.usb/'capture-result.json').write_text(json.dumps(dict(clean_stop=True)))
        rng=random.Random(1729)
        self.audio=b''.join(struct.pack('<hh',rng.randrange(-30000,30000),rng.randrange(-30000,30000)) for _ in range(704))
        (self.pcm/'received.s16le').write_bytes(self.audio)
        (self.pcm/'transfers.tsv').write_text('kind\tstep\tcaptured\n')
        self.chunks=[]
        for i in range(8):
            first=self.audio[i*352:i*352+176];second=self.audio[i*352+176:(i+1)*352]
            desc=[(0,0,176),(0,192,176)]
            self.chunks += [record(i,'S',b'',desc),record(i,'C',first+bytes(16)+second,desc)]
        self.write()

    def write(self,loss=0,chunks=None):
        chunks=self.chunks if chunks is None else chunks
        (self.usb/'capture.pcap').write_bytes(struct.pack('<IHHIIII',0xa1b2c3d4,2,4,0,0,a.SNAPLEN,220)+b''.join(chunks))
        (self.usb/'capture.log').write_text('%d packets captured\n%d packets received by filter\n%d packets dropped by kernel\n'%(len(chunks),len(chunks),loss))

    def test_complete_sparse_iso_retains_metadata_and_exact_audio(self):
        result=a.analyze(self.usb,self.pcm)
        self.assertTrue(result['usb_comparison_valid'],result['errors'])
        self.assertEqual((Path(result['derived_directory'])/'usb-audio.s16le').read_bytes(),self.audio)
        self.assertEqual(result['alignment'][0]['differing_frames'],0)
        metadata=[json.loads(s) for s in (Path(result['derived_directory'])/'usb-records.jsonl').read_text().splitlines()]
        self.assertEqual([r['event'] for r in metadata[:2]],['S','C'])
        self.assertGreater(metadata[1]['captured_length'],metadata[1]['original_length'])
        self.assertEqual(metadata[1]['descriptors'][1]['offset'],192)

    def test_unfinished_pcap_is_invalid(self):
        p=self.usb/'capture.pcap';p.write_bytes(p.read_bytes()[:-7])
        result=a.analyze(self.usb,self.pcm)
        self.assertFalse(result['usb_comparison_valid']);self.assertTrue(any('unfinished' in x for x in result['errors']))

    def test_declared_usb_data_truncation_invalidates_complete_pcap_record(self):
        data=bytearray(self.chunks[1])
        struct.pack_into('<I',data,16+36,struct.unpack_from('<I',data,16+36)[0]+4)
        self.chunks[1]=bytes(data);self.write()
        result=a.analyze(self.usb,self.pcm)
        self.assertFalse(result['usb_comparison_valid'])
        self.assertTrue(any('snaplen truncation' in e for e in result['errors']))

    def test_zero_data_cancelled_tail_is_retained_without_fabricating_audio(self):
        self.chunks.extend([record(99,'S',b'',[]),record(99,'C',b'',[],status=-2)])
        self.write();result=a.analyze(self.usb,self.pcm)
        self.assertTrue(result['usb_comparison_valid'],result['errors'])
        self.assertEqual(result['reconstructed_frames'],704)

    def test_sparse_descriptor_data_missing_despite_plausible_pcap_lengths(self):
        payload=self.audio[:176]+bytes(16)+self.audio[176:352]
        self.chunks[1]=record(0,'C',payload,[(0,0,176),(0,1200,176)])
        self.write();result=a.analyze(self.usb,self.pcm)
        self.assertFalse(result['usb_comparison_valid']);self.assertTrue(any('missing sparse payload' in e for e in result['errors']))

    def test_iso_packet_error_is_explicit_and_no_silence_is_inserted(self):
        self.chunks[1]=record(0,'C',self.audio[:176]+bytes(16)+self.audio[176:352],[(0,0,176),(-71,192,176)])
        self.write();result=a.analyze(self.usb,self.pcm)
        self.assertFalse(result['usb_comparison_valid']);self.assertEqual(result['alignment'],[])
        self.assertEqual(len((Path(result['derived_directory'])/'usb-audio.s16le').read_bytes()),len(self.audio)-176)

    def test_capture_loss_invalidates_comparison_even_when_pcm_matches(self):
        self.write(loss=1);result=a.analyze(self.usb,self.pcm)
        self.assertFalse(result['usb_comparison_valid']);self.assertEqual(result['alignment'],[])

    def test_missing_statistics_is_not_zero_loss(self):
        (self.usb/'capture.log').write_text('tcpdump crashed')
        self.assertFalse(a.analyze(self.usb,self.pcm)['usb_comparison_valid'])

    def test_unmatched_submission_invalidates_usb(self):
        self.write(chunks=self.chunks[:-1])
        self.assertTrue(any('missing USB completions' in e for e in a.analyze(self.usb,self.pcm)['errors']))

    def test_content_alignment_reports_2048_frame_signature(self):
        rng=random.Random(123)
        usb=b''.join(struct.pack('<hh',rng.randrange(-30000,30000),0) for _ in range(5000))
        pcm=bytearray(usb);pcm[2500*4:2536*4]=pcm[(2500-2048)*4:(2536-2048)*4]
        result=a.align(usb,bytes(pcm),[0,5000])[0]
        self.assertEqual(result['status'],'ALIGNED');self.assertEqual(result['differing_frames'],36)
        self.assertEqual(result['differing_pcm_frames_equal_2048_prior'],36)

    def test_silent_or_repeated_content_is_unknown_not_arbitrarily_aligned(self):
        self.assertEqual(a.align(bytes(4096),bytes(2048),[0,512])[0]['status'],'UNKNOWN')
        block=bytes(range(256))*4
        self.assertEqual(a.align(block*10,block*2,[0,512])[0]['status'],'UNKNOWN')

    def test_discovery_uses_cdc_ancestry_and_audio_data_endpoint(self):
        usb=self.root/'actual-usb';usb.mkdir();tty=self.root/'tty';(tty/'ttyACM0').mkdir(parents=True)
        (tty/'ttyACM0/device').symlink_to(usb.resolve(),target_is_directory=True)
        for name,text in [('idVendor','16c0'),('idProduct','048a'),('busnum','1'),('devnum','7')]:
            (usb/name).write_text(text)
        descriptors=bytes([9,4,2,1,1,1,2,0,0,11,0x24,2,1,2,2,16,1,0x44,0xac,0,7,5,0x83,5,180,0,1])
        (usb/'descriptors').write_bytes(descriptors)
        result=c.discover('/dev/ttyACM0',tty)
        self.assertEqual((result['bus'],result['device'],result['endpoint']),(1,7,0x83))
        self.assertNotIn('usbmon0',c.packet_filter(result))

    def test_sudo_uses_no_password_cache_and_only_helper_is_privileged(self):
        command=c.sudo_command();self.assertEqual(command[:4],['sudo','-n','-k','--'])
        self.assertTrue(command[-1].endswith('usbmon_capture.py'))
        with patch.object(c.subprocess,'run',return_value=Mock(returncode=1,stderr=b'password required')):
            with self.assertRaisesRegex(ValueError,'noninteractive sudo'):c.permission_check()

    def test_preparation_failure_precedes_install_and_installs_opt_in_when_ready(self):
        repo=self.root/'checkout';ports=self.root/'ports'
        for path in ['tools/arkos/port','tools/alsa_bridge','deploy/arkos']:(repo/path).mkdir(parents=True,exist_ok=True)
        (repo/'tools/arkos/port/BroTracker Bridge Clicks.sh').write_text('#!/bin/bash\n')
        (repo/'tools/alsa_bridge/offline_runner.py').write_text('# synthetic')
        (repo/'deploy/arkos/bridge-clicks-artifacts.json').write_text('{}')
        with patch.object(c,'permission_check',side_effect=ValueError('permission denied')):
            with self.assertRaises(ValueError):prep.install(repo,ports,True)
        self.assertFalse(ports.exists())
        with patch.object(c,'permission_check',return_value=dict(dependencies='synthetic libpcap')):
            prep.install(repo,ports,True)
        self.assertEqual((ports/'BroTracker Bridge Clicks.usbmon').read_text(),'enabled\n')

    def test_owned_helper_stops_and_reaps_fake_tcpdump_on_pipe_eof(self):
        fake=self.root/'fake-tcpdump'
        fake.write_text('#!/usr/bin/env python3\nimport signal,sys,time\n'
                        'def stop(*args):\n print("0 packets captured\\n0 packets received by filter\\n0 packets dropped by kernel",file=sys.stderr,flush=True);sys.exit(0)\n'
                        'signal.signal(signal.SIGINT,stop);signal.alarm(10)\n'
                        'sys.stdout.buffer.write(bytes(24));sys.stdout.buffer.flush()\n'
                        'print("listening on usbmon1, link-type USB_LINUX_MMAPPED",file=sys.stderr,flush=True)\n'
                        'while True:time.sleep(.01)\n')
        fake.chmod(0o755)
        code='import sys;sys.path.insert(0,%r);import usbmon_capture as c;c.semantics=lambda:{"tcpdump":%r};c.discover=lambda p:{"bus":1,"device":7,"endpoint":131};c.capture("/dev/ttyACM0")' % (str(Path(__file__).resolve().parent),str(fake.resolve()))
        with (self.root/'owned.pcap').open('wb') as data,(self.root/'owned.log').open('wb') as log:
            child=subprocess.Popen([sys.executable,'-B','-c',code],stdin=subprocess.PIPE,stdout=data,stderr=log)
            try:
                deadline=time.monotonic()+5
                while time.monotonic()<deadline and 'USBMON_READY' not in (self.root/'owned.log').read_text():time.sleep(.05)
                self.assertIn('USBMON_READY',(self.root/'owned.log').read_text())
                child.stdin.close();self.assertEqual(child.wait(timeout=5),0)
            finally:
                if child.poll() is None:child.kill();child.wait(timeout=5)
        line=next(s for s in (self.root/'owned.log').read_text().splitlines() if s.startswith('USBMON_RESULT '))
        self.assertTrue(json.loads(line[len('USBMON_RESULT '):])['clean_stop'])

    @unittest.skipUnless(shutil.which('tcpdump'),'local tcpdump not installed; target preparation runs this check')
    def test_installed_tcpdump_offline_sparse_fixture_filter(self):
        original=self.root/'semantics.pcap';selected=self.root/'filtered.pcap'
        original.write_bytes(c.fixture())
        result=subprocess.run([shutil.which('tcpdump'),'-r',str(original),'-w',str(selected),c.packet_filter(dict(endpoint=131,device=7))],capture_output=True,timeout=5)
        self.assertEqual(result.returncode,0,result.stderr)
        saved=list(a.records(selected))
        self.assertEqual([r['event'] for r in saved],['S','C'])
        self.assertEqual(saved[1]['payload'],b'abcd'+bytes(36)+b'WXYZ')


if __name__ == '__main__':
    unittest.main()
