#!/usr/bin/env python3
"""Local Snap Runner simulator + read-only USB telemetry dashboard (no flashing)."""
import argparse
import base64
from collections import deque
import ctypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
from pathlib import Path
import secrets
import socketserver
import shutil
import subprocess
import sys
import struct
import threading
import time
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'tools/snap_debug'
RESULTS = ('mic', 'guard', 'clipped', 'noisy', 'range', 'captured', 'jump', 'air', 'idle')


def build_host():
    cmake = shutil.which('cmake')
    if not cmake:
        candidates = sorted((Path.home() / '.espressif/tools/cmake').glob('*/CMake.app/Contents/bin/cmake'))
        cmake = str(candidates[-1]) if candidates else None
    if not cmake:
        raise RuntimeError('CMake is missing. Activate ESP-IDF or install CMake first.')
    if not (ROOT / 'managed_components/lvgl__lvgl/CMakeLists.txt').exists():
        raise RuntimeError('LVGL dependency missing. Run an ESP-IDF build once to resolve managed components.')
    subprocess.run([cmake, '-S', str(ROOT / 'tests/snap_preview'), '-B', str(ROOT / 'build/snap-debug'),
                    '-DCMAKE_BUILD_TYPE=Debug'], check=True, stdout=sys.stderr)
    subprocess.run([cmake, '--build', str(ROOT / 'build/snap-debug'), '--target', 'snap_host', '-j', '4'],
                   check=True, stdout=sys.stderr)


class Simulator:
    def __init__(self, path=None):
        suffix = '.dylib' if sys.platform == 'darwin' else '.so'
        self.lib = ctypes.CDLL(str(path or ROOT / f'build/snap-debug/libsnap_host{suffix}'))
        for name, args, result in (
            ('reset', [ctypes.c_uint32, ctypes.c_int], ctypes.c_int),
            ('step', [ctypes.c_uint], None), ('key', [ctypes.c_uint], None),
            ('sound', [ctypes.c_float, ctypes.c_int, ctypes.c_int], ctypes.c_int),
            ('level', [ctypes.c_uint], ctypes.c_int), ('music_time', [ctypes.c_uint32], ctypes.c_int), ('state', [], ctypes.c_char_p),
            ('frame', [], ctypes.c_void_p),
        ):
            fn = getattr(self.lib, 'snap_host_' + name)
            fn.argtypes, fn.restype = args, result
        self.lock = threading.Lock()
        self.frozen = False
        self.music_master = False
        self.music_epoch = 0
        self.last_music_clock = time.monotonic()
        self.events = deque(maxlen=80)
        self.seed = 42
        if not self.lib.snap_host_reset(self.seed, 1):
            raise RuntimeError('LVGL font initialization failed')

    def state(self):
        return json.loads(self.lib.snap_host_state())

    def tick(self):
        with self.lock:
            if not self.frozen:
                if self.music_master and self.state()['page'] == 'playing':
                    if time.monotonic() - self.last_music_clock > 3:
                        self.lib.snap_host_key(2)  # Browser suspended/disconnected: pause safely.
                        self.music_epoch += 1
                else:
                    self.lib.snap_host_step(2)

    @staticmethod
    def integer(data, name, low, high):
        value = data.get(name)
        if type(value) is not int or not low <= value <= high:
            raise ValueError(f'{name} must be an integer in [{low}, {high}]')
        return value

    def command(self, data):
        with self.lock:
            op = data.get('op')
            result = None
            before = self.state()
            if op == 'music':
                if type(data.get('value')) is not bool:
                    raise ValueError('value must be boolean')
                self.music_master = data['value']
                self.last_music_clock = time.monotonic()
            elif op == 'music_clock':
                ms = self.integer(data, 'song_ms', 0, 160000)
                run = self.integer(data, 'run', 0, 0xffffffff)
                epoch = self.integer(data, 'epoch', 0, 0xffffffff)
                current = self.state()
                if (self.music_master and not self.frozen and current['page'] == 'playing'
                        and run == current['seed'] and epoch == self.music_epoch):
                    if not self.lib.snap_host_music_time(ms):
                        self.music_epoch += 1
                    self.last_music_clock = time.monotonic()
                return
            elif op == 'pause':
                if before['page'] == 'playing':
                    self.lib.snap_host_key(2)
            elif op == 'key':
                self.last_music_clock = time.monotonic()
                self.lib.snap_host_key(self.integer(data, 'key', 0, 2))
            elif op == 'level':
                result = self.lib.snap_host_level(self.integer(data, 'level', 1, 10))
            elif op == 'sound':
                peak = data.get('peak')
                if type(peak) not in (int, float) or not math.isfinite(peak) or not 0 <= peak <= 65536:
                    raise ValueError('peak must be finite and in [0, 65536]')
                for key in ('clipped', 'noisy'):
                    if type(data.get(key, False)) is not bool:
                        raise ValueError(f'{key} must be a boolean')
                result = self.lib.snap_host_sound(peak, data.get('clipped', False), data.get('noisy', False))
            elif op == 'freeze':
                if type(data.get('value')) is not bool:
                    raise ValueError('value must be a boolean')
                self.frozen = data['value']
                self.last_music_clock = time.monotonic()
            elif op == 'step':
                if not self.frozen:
                    raise ValueError('Freeze the simulation before stepping')
                self.lib.snap_host_step(self.integer(data, 'steps', 1, 100))
            elif op == 'reset':
                seed = self.integer(data, 'seed', 0, 0xffffffff)
                if type(data.get('calibrated', True)) is not bool:
                    raise ValueError('calibrated must be a boolean')
                self.seed = seed
                self.lib.snap_host_reset(seed, data.get('calibrated', True))
                self.events.clear()
            else:
                raise ValueError('Unknown simulator command')
            s = self.state()
            if (op in ('music', 'freeze', 'reset') or
                    (s['page'], s['seed']) != (before['page'], before['seed'])):
                self.music_epoch += 1
            self.events.appendleft({'ms': s['ms'], 'action': op,
                                    'reason': RESULTS[result] if result is not None else op,
                                    'level': s['level']})

    def snapshot(self, frame=True):
        with self.lock:
            s = self.state()
            s.update(frozen=self.frozen, musicMaster=self.music_master, musicEpoch=self.music_epoch,
                     initialSeed=self.seed, events=list(self.events))
            if frame:
                s['frame'] = base64.b64encode(ctypes.string_at(self.lib.snap_host_frame(), 240 * 320 * 2)).decode()
            return s


class Telemetry:
    """Bounded, fragmented-line parser. Ignore ordinary logs and unknown schemas."""
    def __init__(self):
        self.lock = threading.Lock()
        self.pending = bytearray()
        self.discard_line = False
        self.samples = deque(maxlen=300)
        self.events = deque(maxlen=80)
        self.state = None
        self.received = 0
        self.invalid = 0
        self.last_received = 0.0
        self.last_ms = 0
        self.connection = '未连接：以 --port 指定设备；也可先看演示数据'
        self.demo = False
        self.recording = None

    def feed(self, chunk):
        with self.lock:
            for byte in chunk:
                if byte == 10:
                    if not self.discard_line:
                        self._line(bytes(self.pending).rstrip(b'\r'))
                    self.pending.clear()
                    self.discard_line = False
                elif not self.discard_line:
                    self.pending.append(byte)
                    if len(self.pending) > 4096:
                        self.pending.clear()
                        self.discard_line = True
                        self.invalid += 1

    @staticmethod
    def validate(r):
        if not isinstance(r, dict) or r.get('v') != 1 or r.get('type') not in ('state', 'audio', 'detect', 'input'):
            return False
        if type(r.get('ms')) is not int or not 0 <= r['ms'] <= 2**53:
            return False
        if type(r.get('dropped')) is not int or r['dropped'] < 0:
            return False
        # JSON NaN/Infinity must not reach the browser or chart renderer.
        def finite(value):
            if isinstance(value, float):
                return math.isfinite(value)
            if isinstance(value, dict):
                return all(finite(v) for v in value.values())
            if isinstance(value, list):
                return all(finite(v) for v in value)
            return True
        if not finite(r):
            return False
        if r['type'] == 'state':
            if r.get('page') not in ('home', 'loud', 'soft_ready', 'soft', 'invalid', 'practice', 'playing', 'paused', 'over'):
                return False
            return all(type(r.get(k)) in (int, float) for k in
                       ('level', 'min', 'max', 'samples', 'seed', 'distance', 'score', 'speed', 'y', 'velocity', 'lives'))
        reasons = RESULTS + ('stale',) if r['type'] == 'input' else (
            'warmup', 'quiet', 'threshold', 'cooldown', 'wait_quiet', 'collecting', 'accepted', 'decay', 'spectrum')
        return r.get('reason') in reasons and all(type(r.get(k)) in (int, float) and 0 <= r[k] <= 1e9
                                                for k in ('peak', 'rms', 'noise'))

    def _line(self, line):
        if not line.startswith(b'@snap '):
            return
        try:
            r = json.loads(line[6:])
            if not self.validate(r):
                raise ValueError('Invalid telemetry')
        except (ValueError, UnicodeDecodeError, RecursionError):
            self.invalid += 1
            return
        # Producers are concurrent: slight timestamp reordering is not a reboot.
        if r['ms'] + 2000 < self.last_ms:
            self.samples.clear()
            self.events.clear()
            self.state = None
        self.last_ms = r['ms']
        self.last_received = time.monotonic()
        self.received += 1
        if r['type'] == 'audio':
            self.samples.append(r)
        elif r['type'] == 'state':
            self.state = r
        else:
            self.events.appendleft(r)
        if self.recording:
            try:
                self.recording.write(json.dumps(r, ensure_ascii=False) + '\n')
                self.recording.flush()
            except OSError:
                self.recording.close()
                self.recording = None
                self.connection = '记录文件写入失败；实时显示继续'

    def snapshot(self):
        with self.lock:
            age = time.monotonic() - self.last_received if self.last_received else None
            return dict(state=self.state, samples=list(self.samples), events=list(self.events),
                        received=self.received, invalid=self.invalid, age=age,
                        connection=self.connection, demo=self.demo)


def serial_reader(telemetry, port, stop):
    import serial
    # No DTR/RTS assertion, reset, writes, automatic port selection or reconnect.
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    connection.dtr = False
    connection.rts = False
    connection.port = port
    try:
        connection.open()
        with telemetry.lock:
            telemetry.connection = f'串口已打开：{port}；等待 @snap 数据'
        while not stop.is_set():
            telemetry.feed(connection.read(2048))
    except (serial.SerialException, OSError) as exc:
        with telemetry.lock:
            telemetry.connection = f'串口不可用：{exc}。检查连接后重新启动工具。'
    finally:
        connection.close()


def demo_tick(t, ms):
    peak = 12000 if ms % 1500 < 120 else 50
    for kind in (['audio', 'detect', 'input'] if peak > 1000 else ['audio']):
        r = dict(v=1, type=kind, ms=ms, dropped=0, peak=peak, rms=peak / 3,
                 noise=24, reason='jump' if kind == 'input' else 'accepted' if kind == 'detect' else 'quiet', clipped=False)
        t.feed(b'@snap ' + json.dumps(r).encode() + b'\n')


class DebugServer(ThreadingHTTPServer):
    daemon_threads = True
    def server_bind(self):
        # Loopback only: HTTPServer's reverse DNS lookup can stall on offline Macs.
        socketserver.TCPServer.server_bind(self)
        self.server_name = 'localhost'
        self.server_port = self.server_address[1]

    def __init__(self, address, simulator, telemetry, allow_demo=True):
        self.simulator, self.telemetry = simulator, telemetry
        self.allow_demo = allow_demo
        self.key = secrets.token_urlsafe(32)
        super().__init__(address, Handler)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def reply(self, status, content, mime='application/json', headers=None):
        if isinstance(content, dict):
            content = json.dumps(content, ensure_ascii=False, allow_nan=False).encode()
        self.send_response(status)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(content)))
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Content-Security-Policy', "default-src 'self'; style-src 'self'; script-src 'self'; connect-src 'self'; img-src 'self' data:; media-src 'self' data:; frame-ancestors 'none'")
        self.end_headers()
        self.wfile.write(content)

    def local_request(self):
        host = self.headers.get('Host')
        allowed = {f'127.0.0.1:{self.server.server_port}', f'localhost:{self.server.server_port}'}
        return host in allowed and self.headers.get('Origin', 'http://' + str(host)) == 'http://' + str(host)

    def do_GET(self):
        if not self.local_request():
            return self.reply(403, {'error': 'Local origin required'})
        path = urlsplit(self.path).path
        reports = {'/rhythm/': ('index.html', 'text/html; charset=utf-8'),
                   '/rhythm/report.js': ('report.js', 'text/javascript; charset=utf-8'),
                   '/rhythm/report.css': ('report.css', 'text/css; charset=utf-8'),
                   '/rhythm/beats.csv': ('beats.csv', 'text/csv; charset=utf-8')}
        if path in reports:
            name, mime = reports[path]
            report = ROOT / 'build/music-analysis' / name
            if not report.is_file():
                return self.reply(404, {'error': 'Run python3 tools/snap_music_report.py first'})
            return self.reply(200, report.read_bytes(), mime)
        if path == '/music.wav':
            pcm = (ROOT / 'assets/music/mountain-king.pcm').read_bytes()
            content = struct.pack('<4sI4s4sIHHIIHH4sI', b'RIFF', len(pcm) + 36,
                                  b'WAVE', b'fmt ', 16, 1, 1, 16000, 32000, 2, 16,
                                  b'data', len(pcm)) + pcm
            range_header = self.headers.get('Range')
            if range_header:
                import re
                match = re.fullmatch(r'bytes=(\d+)-(\d*)', range_header)
                if not match:
                    return self.reply(416, b'', headers={'Content-Range': f'bytes */{len(content)}'})
                start = int(match[1]); end = int(match[2]) if match[2] else len(content) - 1
                end = min(end, len(content) - 1)
                if start > end:
                    return self.reply(416, b'', headers={'Content-Range': f'bytes */{len(content)}'})
                return self.reply(206, content[start:end + 1], 'audio/wav',
                                  {'Accept-Ranges': 'bytes', 'Content-Range': f'bytes {start}-{end}/{len(content)}'})
            return self.reply(200, content, 'audio/wav', {'Accept-Ranges': 'bytes'})
        if path == '/api/sim':
            return self.reply(200, self.server.simulator.snapshot())
        if path == '/api/device':
            return self.reply(200, self.server.telemetry.snapshot())
        files = {'/': ('index.html', 'text/html; charset=utf-8'), '/app.js': ('app.js', 'text/javascript; charset=utf-8'),
                 '/style.css': ('style.css', 'text/css; charset=utf-8')}
        if path not in files:
            return self.reply(404, {'error': 'Not found'})
        name, mime = files[path]
        content = (ASSETS / name).read_bytes().replace(b'__SNAP_KEY__', self.server.key.encode())
        self.reply(200, content, mime)

    def do_POST(self):
        if not self.local_request() or self.headers.get('X-Snap-Key') != self.server.key:
            return self.reply(403, {'error': 'Local session key required'})
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= 1024:
                raise ValueError('Invalid request length')
            data = json.loads(self.rfile.read(length))
            if not isinstance(data, dict):
                raise ValueError('Expected object')
            if self.path == '/api/command':
                self.server.simulator.command(data)
            elif self.path == '/api/demo' and self.server.allow_demo:
                if type(data.get('value')) is not bool:
                    raise ValueError('value must be boolean')
                t = self.server.telemetry
                with t.lock:
                    t.demo = data['value']
                    t.samples.clear(); t.events.clear(); t.state = None
                    t.received = t.invalid = 0
                    t.last_received = 0
                    t.last_ms = 0
                    t.connection = '演示数据（非真机）' if t.demo else '未连接设备'
            else:
                return self.reply(404, {'error': 'Not found'})
            self.reply(200, {'ok': True})
        except (ValueError, TypeError, KeyError) as exc:
            self.reply(400, {'error': str(exc)})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='Explicit serial device to observe; never resets or flashes')
    parser.add_argument('--list-ports', action='store_true')
    parser.add_argument('--http-port', type=int, default=8765)
    parser.add_argument('--no-build', action='store_true', help='Use an already built matching host library')
    parser.add_argument('--record', type=Path, help='Save parsed USB telemetry only, to a new .jsonl file under build/')
    args = parser.parse_args()
    if args.list_ports:
        from serial.tools import list_ports
        for port in list_ports.comports():
            print(f'{port.device}\t{port.description}')
        return
    if args.port:
        try:
            import serial  # noqa: F401
        except ImportError:
            parser.error('USB mode needs pyserial; use the ESP-IDF Python environment.')
    if args.record and not args.port:
        parser.error('--record requires --port (demo data cannot be recorded as real telemetry)')
    record_path = None
    if args.record:
        record_path = args.record.resolve()
        if ROOT / 'build' not in record_path.parents or record_path.suffix != '.jsonl':
            parser.error('Record only to a new .jsonl file inside this project build/ directory')
    if not args.no_build:
        build_host()
    sim, telemetry = Simulator(), Telemetry()
    server = DebugServer(('127.0.0.1', args.http_port), sim, telemetry, allow_demo=not args.port)
    if record_path:
        record_path.parent.mkdir(parents=True, exist_ok=True)
        telemetry.recording = record_path.open('x', encoding='utf-8')
    stop = threading.Event()
    worker = None
    if args.port:
        worker = threading.Thread(target=serial_reader, args=(telemetry, args.port, stop), daemon=True)
        worker.start()
    def tick():
        demo_ms = 0
        while not stop.wait(0.02):
            sim.tick()  # No catch-up bursts after the host is suspended.
            demo_ms += 20
            if telemetry.demo and demo_ms % 100 == 0:
                demo_tick(telemetry, demo_ms)
    clock = threading.Thread(target=tick, daemon=True)
    clock.start()
    print(f'Snap debug ready: http://127.0.0.1:{server.server_port}', flush=True)
    print('Keyboard simulator is local. USB is read-only; Ctrl+C closes the tool.', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        server.server_close()
        clock.join(2)
        if worker:
            worker.join(2)
        if telemetry.recording:
            telemetry.recording.close()


if __name__ == '__main__':
    main()
