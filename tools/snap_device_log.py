#!/usr/bin/env python3
"""Bounded serial health probe; --reset explicitly restarts the selected board.

Requires the ESP-IDF Python environment (pyserial/esptool). Never flashes.
Raw logs remain in the caller-selected local output path.
"""
import argparse
from pathlib import Path
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--seconds', type=float, default=15)
parser.add_argument('--reset', action='store_true')
args = parser.parse_args()
assert 1 <= args.seconds <= 60
stream = serial.Serial(args.port, 115200, timeout=0.1)
raw = bytearray()
try:
    if args.reset:
        from esptool.reset import HardReset
        stream.dtr = False
        HardReset(stream, uses_usb=True)()
    end = time.monotonic() + args.seconds
    while time.monotonic() < end:
        raw.extend(stream.read(4096))
finally:
    stream.close()
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_bytes(raw)
text = raw.decode('utf-8', errors='replace')
# Only selected diagnostics are printed; the full local log is not published.
for line in text.splitlines():
    if any(token in line for token in ('snap:', 'ELF file SHA256', 'task_wdt:', 'Guru Meditation', 'assert failed', 'Backtrace:', 'esp_app_desc:', 'app_init:')):
        print(line)
failure = any(token in text for token in ('task_wdt:', 'Guru Meditation', 'assert failed', 'Brownout'))
if failure:
    print('Device log health: FAIL')
    raise SystemExit(1)
if not raw or (args.reset and 'Snap Runner ready' not in text):
    print('Device log health: INCONCLUSIVE (no confirmed app startup)')
    raise SystemExit(2)
print('Device log health: PASS (observed window only; not physical gameplay acceptance)')
