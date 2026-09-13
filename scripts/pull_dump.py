#!/usr/bin/env python3
"""Pull a root-readable dump without terminal newline conversion; verify its SHA-256."""
import argparse
import hashlib
import pathlib
import re
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--source', required=True, help='Absolute dump path on the Android device')
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    if not args.source.startswith('/') or '\0' in args.source:
        parser.error('--source must be an absolute device path')
    adb = ['adb', '-s', args.serial]
    uid = subprocess.check_output(adb + ['shell', 'id', '-u'], text=True, timeout=15).strip()
    if not uid.isdecimal():
        raise SystemExit('Cannot determine ADB shell UID')

    def root(command):
        script = command if uid == '0' else 'su -c ' + shlex.quote(command)
        return subprocess.check_output(adb + ['shell', script], text=True, timeout=60).strip()

    remote = root('mktemp -d /data/local/tmp/il2cppdumper-export.XXXXXXXXXX')
    if not re.fullmatch(r'/data/local/tmp/il2cppdumper-export\.[A-Za-z0-9]+', remote):
        raise SystemExit('Unexpected staging directory returned by device')
    try:
        staged = remote + '/dump.cs'
        # A private staging directory keeps the copied app data accessible only
        # to root and the ADB shell. adb pull avoids su implementations using a PTY.
        root(f'''set -e
cp -- {shlex.quote(args.source)} {staged}
chmod 600 {staged}
chcon -R u:object_r:shell_data_file:s0 {remote}
chown -R {uid}:{uid} {remote}
''')
        digest = root('sha256sum ' + staged).split()[0]
        if not re.fullmatch('[0-9a-fA-F]{64}', digest):
            raise SystemExit('Invalid device checksum')
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.il2cpp-export-', dir=args.output.parent) as directory:
            local = pathlib.Path(directory) / 'dump.cs'
            subprocess.run(adb + ['pull', staged, str(local)], check=True, timeout=120)
            with local.open('rb') as file:
                actual = hashlib.file_digest(file, 'sha256').hexdigest()
            if actual != digest.lower():
                raise SystemExit('Dump checksum mismatch; previous local output preserved')
            local.chmod(0o600)
            local.replace(args.output)
        print(f'{args.output}: SHA-256 {actual}')
    finally:
        root('rm -rf -- ' + remote)


if __name__ == '__main__':
    main()
