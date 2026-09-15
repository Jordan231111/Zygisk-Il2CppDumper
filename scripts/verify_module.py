#!/usr/bin/env python3
"""Check module contents, ELF identity, exported entry points and page alignment."""
import argparse
import pathlib
import struct
import subprocess
import tempfile
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('archive', type=pathlib.Path)
parser.add_argument('--readelf', default='llvm-readelf')
parser.add_argument('--expect-unmount', choices=['true', 'false'], help='Check the unmount option selected for this build')
args = parser.parse_args()
abis = {'armeabi-v7a': (1, 40), 'arm64-v8a': (2, 183), 'x86': (1, 3), 'x86_64': (2, 62)}
with zipfile.ZipFile(args.archive) as archive, tempfile.TemporaryDirectory() as temporary:
    members = set(archive.namelist())
    expected = {f'zygisk/{abi}.so' for abi in abis}
    actual = {name for name in members if name.startswith('zygisk/') and name.endswith('.so')}
    assert actual == expected, f'Wrong ABI payloads: {actual}'
    assert 'module.prop' in members and 'META-INF/com/google/android/update-binary' in members
    if args.expect_unmount is not None:
        properties = dict(line.split('=', 1) for line in archive.read('module.prop').decode().splitlines()
                          if '=' in line and not line.startswith('#'))
        assert properties.get('unmountModules') == args.expect_unmount, 'Wrong module unmount setting'
    for abi, (elf_class, machine) in abis.items():
        raw = archive.read(f'zygisk/{abi}.so')
        assert raw[:4] == b'\x7fELF' and raw[4:6] == bytes([elf_class, 1]), abi
        assert struct.unpack_from('<H', raw, 18)[0] == machine, abi
        if elf_class == 2:
            offset = struct.unpack_from('<Q', raw, 32)[0]
            stride, count = struct.unpack_from('<HH', raw, 54)
            for index in range(count):
                at = offset + index * stride
                if struct.unpack_from('<I', raw, at)[0] == 1:
                    assert struct.unpack_from('<Q', raw, at + 48)[0] >= 16384, f'{abi}: not 16 KB aligned'
        library = pathlib.Path(temporary) / f'{abi}.so'
        library.write_bytes(raw)
        dynamic = subprocess.check_output([args.readelf, '--dynamic', str(library)], text=True)
        assert 'libc++_shared.so' not in dynamic, 'Module must be self-contained'
        symbols = subprocess.check_output([args.readelf, '--dyn-syms', '--wide', str(library)], text=True)
        exported = {line.split()[-1] for line in symbols.splitlines()
                    if (' GLOBAL ' in line or ' WEAK ' in line) and ' UND ' not in line}
        assert 'zygisk_module_entry' in exported, f'{abi}: no Zygisk entry point'
        assert exported <= {'zygisk_module_entry', 'JNI_OnLoad'}, f'{abi}: leaked exports {exported}'
print(f'{args.archive}: four ABIs, isolated exports, static C++ runtime, 16 KB alignment OK')
