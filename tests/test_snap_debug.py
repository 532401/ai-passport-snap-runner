#!/usr/bin/env python3
"""Protocol, native bridge, HTTP boundary and optional pseudo-serial acceptance."""
import importlib.util
import json
import os
from pathlib import Path
import pty
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('snap_debug_tool', ROOT / 'tools/snap_debug.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def audio(ms=100, **extra):
    return dict(v=1, type='audio', ms=ms, dropped=0, peak=1000, rms=100, noise=20,
                reason='quiet', clipped=False, **extra)


def wire(r):
    return b'@snap ' + json.dumps(r).encode() + b'\r\n'


class ProtocolTests(unittest.TestCase):
    def test_browser_media_controls(self):
        node = shutil.which('node')
        if not node:
            self.skipTest('Node.js is required for browser controller tests')
        subprocess.run([node, '--test', str(ROOT/'tests/test_snap_browser.js')], cwd=ROOT, check=True)

    def test_c_producer_contract(self):
        with tempfile.TemporaryDirectory() as td:
            exe = str(Path(td) / 'protocol')
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(ROOT / 'main'), str(ROOT / 'tests/test_snap_debug.c'),
                            *[str(ROOT / 'main' / n) for n in ('snap_debug.c', 'snap_model.c', 'snap_session.c', 'snap_detector.c', 'snap_chart.c')],
                            '-lm', '-o', exe], check=True)
            rows = subprocess.check_output([exe]).splitlines()
        t = m.Telemetry()
        for row in rows:
            t.feed(b'@snap ' + row + b'\n')
        self.assertEqual(t.received, 3)
        self.assertEqual(t.invalid, 0)
        self.assertEqual(t.state['page'], 'playing')
        self.assertEqual(t.events[0]['reason'], 'jump')

    def test_fragmentation_limits_and_recovery(self):
        t = m.Telemetry()
        data = b'I (100) ordinary log\n' + wire(audio())
        for byte in data:
            t.feed(bytes([byte]))
        self.assertEqual(t.received, 1)
        t.feed(b'x' * 9000 + b'\n')
        for data in (b'@snap broken\n', b'@snap {"v":9}\n', wire({**audio(), 'peak': float('nan')})):
            t.feed(data)
        t.feed(wire(audio(200)))
        self.assertEqual(t.received, 2)
        self.assertEqual(t.invalid, 4)
        for i in range(1000):
            t.feed(wire(audio(300+i*100)))
        self.assertEqual(len(t.samples), 300)
        t.feed(wire(audio(1)))
        self.assertEqual(len(t.samples), 1)  # reboot clears old chart

    def test_bad_schema_and_recording(self):
        t = m.Telemetry()
        for value in ([], {'v':1}, {**audio(), 'reason':'<script>'}, {**audio(), 'ms':-1}, {**audio(), 'rms':'bad'}):
            t.feed(wire(value))
        self.assertEqual(t.received, 0)
        with tempfile.TemporaryFile(mode='w+', encoding='utf-8') as f:
            t.recording = f
            t.feed(b'private normal log must not be recorded\n' + wire(audio()))
            f.seek(0)
            self.assertEqual(json.loads(f.read())['type'], 'audio')

    def test_pseudo_serial(self):
        try:
            import serial  # noqa: F401
        except ImportError:
            self.skipTest('pyserial not installed in this Python')
        master, slave = pty.openpty()
        stop, t = threading.Event(), m.Telemetry()
        thread = threading.Thread(target=m.serial_reader, args=(t, os.ttyname(slave), stop))
        thread.start()
        try:
            deadline = time.monotonic() + 2
            while '已打开' not in t.snapshot()['connection'] and time.monotonic() < deadline:
                time.sleep(.01)
            os.write(master, wire(audio()))
            deadline = time.monotonic() + 2
            while not t.snapshot()['received'] and time.monotonic() < deadline:
                time.sleep(.01)
            self.assertEqual(t.received, 1)
            os.close(master); master = None
            thread.join(2)
            self.assertIn('不可用', t.snapshot()['connection'])
        finally:
            stop.set(); thread.join(2)
            if master is not None: os.close(master)
            os.close(slave)


@unittest.skipUnless(os.environ.get('SNAP_HOST_LIBRARY'), 'set SNAP_HOST_LIBRARY for built native UI integration')
class HostTests(unittest.TestCase):
    def setUp(self):
        self.sim = m.Simulator(os.environ['SNAP_HOST_LIBRARY'])

    def test_deterministic_map_and_full_frame(self):
        def run():
            self.sim.command(dict(op='reset',seed=123,calibrated=True))
            self.sim.command(dict(op='key',key=2))
            for _ in range(20): self.sim.tick()
            self.sim.command(dict(op='level',level=10))
            for _ in range(30): self.sim.tick()
            state=self.sim.snapshot(False)
            state.pop('musicEpoch')  # Transport identity changes; game state must reproduce.
            return state
        a, b = run(), run()
        self.assertEqual(a, b)
        self.assertGreater(a['y'], 0)
        self.assertEqual(a['level'], 10)
        self.assertEqual(len(__import__('base64').b64decode(self.sim.snapshot()['frame'])),240*320*2)
        self.assertEqual(self.sim.lib.snap_ui_check_fonts(), 1)

    def test_pause_step_calibration_and_input_validation(self):
        self.sim.command(dict(op='freeze',value=True))
        before=self.sim.snapshot(False)
        self.sim.tick()
        self.assertEqual(before, self.sim.snapshot(False))
        self.sim.command(dict(op='step',steps=1))
        self.assertEqual(self.sim.snapshot(False)['ms'],10)
        self.sim.command(dict(op='reset',seed=42,calibrated=False))
        self.sim.command(dict(op='key',key=2))
        for _ in range(3):
            self.sim.command(dict(op='step',steps=100))
            self.sim.command(dict(op='sound',peak=10000))
        self.assertEqual(self.sim.snapshot(False)['page'],'soft_ready')
        self.sim.command(dict(op='key',key=2))
        for _ in range(3):
            self.sim.command(dict(op='step',steps=100))
            self.sim.command(dict(op='sound',peak=1000))
        self.assertEqual(self.sim.snapshot(False)['page'],'practice')
        for data in (dict(op='level',level=0), dict(op='reset',seed=-1), dict(op='sound',peak=float('nan')), dict(op='freeze',value='false')):
            with self.assertRaises(ValueError): self.sim.command(data)

    def test_http_boundary(self):
        server=m.DebugServer(('127.0.0.1',0), self.sim,m.Telemetry())
        thread=threading.Thread(target=server.serve_forever); thread.start()
        url=f'http://127.0.0.1:{server.server_port}'
        try:
            with urllib.request.urlopen(url, timeout=3) as r:
                self.assertIn(server.key,r.read().decode())
            request=urllib.request.Request(url+'/api/command',data=b'{"op":"step","steps":1}',headers={'Content-Type':'application/json'})
            with self.assertRaises(urllib.error.HTTPError) as cm: urllib.request.urlopen(request, timeout=3)
            self.assertEqual(cm.exception.code,403)
            request.add_header('X-Snap-Key',server.key)
            request.add_header('Origin','https://untrusted.example')
            with self.assertRaises(urllib.error.HTTPError) as cm: urllib.request.urlopen(request, timeout=3)
            self.assertEqual(cm.exception.code,403)
            request.remove_header('Origin')
            request.data=b'{"op":"freeze","value":true}'
            with urllib.request.urlopen(request, timeout=3) as r:self.assertTrue(json.load(r)['ok'])
            with urllib.request.urlopen(url+'/api/sim', timeout=3) as r:self.assertTrue(json.load(r)['frozen'])
            request=urllib.request.Request(url+'/music.wav', headers={'Range':'bytes=0-43'})
            with urllib.request.urlopen(request, timeout=3) as r:
                header=r.read()
                self.assertEqual(r.status,206)
                self.assertEqual(len(header),44)
                self.assertEqual(header[:4],b'RIFF')
                self.assertEqual(header[8:12],b'WAVE')
            request=urllib.request.Request(url+'/music.wav', headers={'Range':'bytes=44-123'})
            with urllib.request.urlopen(request, timeout=3) as r:
                self.assertEqual(r.read(),(ROOT/'assets/music/mountain-king.pcm').read_bytes()[:80])
            request=urllib.request.Request(url+'/music.wav', headers={'Range':'bytes=99999999-'})
            with self.assertRaises(urllib.error.HTTPError) as cm: urllib.request.urlopen(request, timeout=3)
            self.assertEqual(cm.exception.code,416)
        finally:
            server.shutdown(); server.server_close();thread.join()

    def test_music_clock_pause_resume_and_stale_run(self):
        self.sim.command(dict(op='music',value=True))
        self.sim.command(dict(op='key',key=2))
        run=self.sim.snapshot(False)['seed']
        self.sim.tick()
        self.assertEqual(self.sim.snapshot(False)['songMs'],0)
        def clock(ms, identity=run):
            self.sim.command(dict(op='music_clock',song_ms=ms,run=identity,
                                  epoch=self.sim.snapshot(False).get('musicEpoch',0)))
        clock(200)
        self.assertEqual(self.sim.snapshot(False)['songMs'],200)
        clock(240,run-1)
        self.assertEqual(self.sim.snapshot(False)['songMs'],200)
        self.sim.command(dict(op='key',key=2))
        clock(240)
        self.assertEqual(self.sim.snapshot(False)['songMs'],200)
        self.sim.command(dict(op='key',key=2))
        clock(240)
        self.assertEqual(self.sim.snapshot(False)['songMs'],240)
        self.sim.command(dict(op='freeze',value=True))
        self.sim.last_music_clock=time.monotonic()-2
        self.sim.command(dict(op='freeze',value=False))
        self.sim.tick()
        self.assertEqual(self.sim.snapshot(False)['page'],'playing')
        clock(2400)  # A long discontinuity still pauses rather than teleporting.
        self.assertEqual(self.sim.snapshot(False)['page'],'paused')
        self.sim.command(dict(op='key',key=2))
        self.sim.last_music_clock=time.monotonic()-4
        self.sim.tick()
        self.assertEqual(self.sim.snapshot(False)['page'],'paused')

    def test_browser_clock_jitter_does_not_trap_resume_at_eleven_metres(self):
        self.sim.command(dict(op='music',value=True))
        self.sim.command(dict(op='key',key=2))
        def clock(ms, epoch=None):
            s=self.sim.snapshot(False)
            self.sim.command(dict(op='music_clock',song_ms=ms,run=s['seed'],
                                  epoch=s.get('musicEpoch',0) if epoch is None else epoch))
        for ms in range(50,1101,50): clock(ms)
        old_epoch=self.sim.snapshot(False).get('musicEpoch',0)
        clock(1450)
        self.assertEqual(self.sim.snapshot(False)['page'],'playing')
        self.assertEqual(self.sim.snapshot(False)['songMs'],1450)
        clock(2450)  # Browser timers can coalesce to 1 Hz.
        self.assertEqual(self.sim.snapshot(False)['page'],'playing')
        self.sim.last_music_clock=time.monotonic()-1.1
        self.sim.tick()
        self.assertEqual(self.sim.snapshot(False)['page'],'playing')
        self.sim.command(dict(op='key',key=2))
        self.sim.command(dict(op='key',key=2))
        clock(5000,old_epoch)  # A request from before pause/resume arrives late.
        self.assertEqual(self.sim.snapshot(False)['page'],'playing')
        self.assertEqual(self.sim.snapshot(False)['songMs'],2450)
        clock(2800)
        self.assertEqual(self.sim.snapshot(False)['songMs'],2800)

    def test_whole_music_chart_with_coalesced_browser_updates(self):
        self.sim.command(dict(op='music',value=True))
        self.sim.command(dict(op='key',key=2))
        chart=json.loads((ROOT/'assets/music/mountain-king-chart.json').read_text())
        updates=0
        def advance(target):
            nonlocal updates
            while self.sim.snapshot(False)['songMs'] < target:
                state=self.sim.snapshot(False)
                step=(50,350,1000)[updates%3]
                updates+=1
                self.sim.command(dict(op='music_clock',song_ms=min(target,state['songMs']+step),
                                      run=state['seed'],epoch=state['musicEpoch']))
                now=self.sim.snapshot(False)
                self.assertIn(now['page'],('playing','over'))
                self.assertEqual(now['lives'],3)
                if now['complete']: break
        for cue in chart['events']:
            advance(cue['jump_ms'])
            self.sim.command(dict(op='level',level=7))
        advance(chart['duration_ms'])
        self.assertTrue(self.sim.snapshot(False)['complete'])


if __name__ == '__main__':
    unittest.main()
