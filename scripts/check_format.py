#!/usr/bin/env python3
"""Check maintained C/C++ sources; leave upstream and generated files alone."""
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
files = []
for directory in (root / 'module/src/main/cpp', root / 'tests'):
    for file in directory.rglob('*'):
        if file.suffix not in ('.h', '.cpp') or 'xdl' in file.parts or file.name == 'zygisk.hpp':
            continue
        files.append(str(file))
sys.exit(subprocess.run(['clang-format', '--dry-run', '--Werror', *sorted(files)]).returncode)
