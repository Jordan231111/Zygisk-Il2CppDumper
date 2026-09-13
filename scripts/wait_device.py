#!/usr/bin/env python3
import argparse
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--serial', required=True)
parser.add_argument('--timeout', type=int, default=180)
args = parser.parse_args()
if args.timeout <= 0:
    parser.error('--timeout must be positive')
deadline = time.monotonic() + args.timeout
while time.monotonic() < deadline:
    try:
        result = subprocess.run(['adb', '-s', args.serial, 'shell', 'getprop', 'sys.boot_completed'],
                                capture_output=True, text=True,
                                timeout=max(0.001, min(10, deadline - time.monotonic())))
    except subprocess.TimeoutExpired:
        # ADB can stall briefly while the emulator's daemon reconnects.
        continue
    if result.returncode == 0 and result.stdout.strip() == '1':
        print(f'{args.serial} booted')
        break
    time.sleep(max(0, min(1, deadline - time.monotonic())))
else:
    raise SystemExit(f'{args.serial}: boot timeout')
