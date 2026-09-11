#!/usr/bin/env python3
"""Run synthetic native tests on an explicitly selected, authorized Android device."""
import argparse
import pathlib
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True)
parser.add_argument('--build-dir', type=pathlib.Path, required=True)
args = parser.parse_args()
adb = ['adb', '-s', args.serial]
remote = '/data/local/tmp/il2cpp-dumper-tests'
files = ['core_tests', 'elf_tests', 'runtime_tests', 'libil2cpp.so', 'librenamed_runtime.so',
         'libhidden_runtime.so', 'libmissing_runtime.so']
for name in files:
    if not (args.build_dir / name).is_file():
        parser.error(f'Missing {args.build_dir / name}; build the Android CMake test project first')
subprocess.run(adb + ['shell', 'mkdir', '-p', remote], check=True)
subprocess.run(adb + ['push', *[str(args.build_dir / name) for name in files], remote + '/'], check=True)
commands = [
    ['./core_tests'], ['./elf_tests'], ['./runtime_tests', './libil2cpp.so', '0'],
    ['./runtime_tests', './libil2cpp.so', '2'], ['./runtime_tests', './libil2cpp.so', '4'],
    ['./runtime_tests', './librenamed_runtime.so', '0'], ['./runtime_tests', './librenamed_runtime.so', '3'],
    ['./runtime_tests', './libhidden_runtime.so', '0'], ['./runtime_tests', './libmissing_runtime.so', '5'],
    ['./runtime_tests', './libil2cpp.so', '0', '--maps'],
]
for command in commands:
    script = f'cd {shlex.quote(remote)} && chmod 755 *_tests && export TMPDIR=/data/local/tmp LD_LIBRARY_PATH=. && {shlex.join(command)}'
    subprocess.run(adb + ['shell', script], check=True, timeout=30)
print(f'All synthetic native tests passed on {args.serial}')
