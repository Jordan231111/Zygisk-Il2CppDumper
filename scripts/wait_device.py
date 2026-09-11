#!/usr/bin/env python3
import argparse
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('--serial', required=True)
parser.add_argument('--timeout', type=int, default=180)
args = parser.parse_args()
deadline = time.monotonic() + args.timeout
while time.monotonic() < deadline:
    result = subprocess.run(['adb', '-s', args.serial, 'shell', 'getprop', 'sys.boot_completed'],
                            capture_output=True, text=True, timeout=10)
    if result.returncode == 0 and result.stdout.strip() == '1':
        print(f'{args.serial} booted')
        break
    time.sleep(1)
else:
    raise SystemExit(f'{args.serial}: boot timeout')
