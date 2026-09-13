#!/usr/bin/env python3
"""Configure this module's exact targets and preserve its installed SELinux label."""
import argparse
import pathlib
import re
import shlex
import subprocess
import tempfile
import secrets

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True)
parser.add_argument('--unmount', choices=['on', 'off'], help='Opt into provider module-mount isolation; may affect other modules in selected apps')
parser.add_argument('--verbose', choices=['on', 'off'], help='Enable or disable per-image diagnostics')
parser.add_argument('targets', nargs='+', help='Exact package/process names; PACKAGE:* opts into secondary processes')
args = parser.parse_args()
pattern = r'[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)+(:[A-Za-z0-9_.*]+)?'
if any(not re.fullmatch(pattern, target) for target in args.targets):
    parser.error('Invalid package/process name')
text = '\n'.join(args.targets) + '\n'
if len(text.encode()) > 4096:
    parser.error('Target configuration exceeds 4 KB')
adb = ['adb', '-s', args.serial]
module = '/data/adb/modules/zygisk_il2cppdumper'
nonce = secrets.token_hex(8)
remote = '/data/local/tmp/il2cppdumper-targets-' + nonce + '.txt'
staged = module + '/.targets-' + nonce
with tempfile.TemporaryDirectory() as directory:
    file = pathlib.Path(directory) / 'targets.txt'
    file.write_text(text)
    subprocess.run(adb + ['push', str(file), remote], check=True)
    script = f'''set -e
trap 'rm -f {remote} {staged} {module}/.verbose-{nonce} {module}/.unmount-{nonce}' EXIT
label=$(ls -Zd {module}/module.prop | awk '{{print $1}}')
case "$label" in *:*:*) ;; *) echo 'Cannot determine module SELinux label' >&2; exit 1;; esac
cp {remote} {staged}
chmod 644 {staged}
chcon "$label" {staged}
mv -f {staged} {module}/targets.txt
rm {remote}
'''
    for name, state in [('unmount', args.unmount), ('verbose', args.verbose)]:
        if state == 'on':
            option = f'{module}/.{name}-{nonce}'
            script += f': > {option}\nchmod 644 {option}\nchcon "$label" {option}\nmv -f {option} {module}/{name}\n'
        elif state == 'off':
            script += f'rm -f {module}/{name}\n'
    uid = subprocess.check_output(adb + ['shell', 'id', '-u'], text=True).strip()
    command = script if uid == '0' else 'su -c ' + shlex.quote(script)
    subprocess.run(adb + ['shell', command], check=True)
print('Targets configured. Force-stop and relaunch the selected app to apply.')
