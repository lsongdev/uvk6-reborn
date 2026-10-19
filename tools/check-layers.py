#!/usr/bin/env python3
"""Fail when a source file includes a layer it must not depend on.

Layers (docs/development.md §3): app -> ui -> radio -> devices -> drivers ->
hal; radio and devices may read store; core depends on nothing; boards
implement hal with drivers. An MCU driver is a drivers/ directory holding
an mcu.mk.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1] / 'src'
ALLOWED = {
    'app': {'app', 'ui', 'radio', 'store', 'core', 'devices', 'hal', 'drivers'},
    'ui': {'ui', 'radio', 'store', 'core', 'devices', 'hal'},
    'radio': {'radio', 'store', 'core', 'devices', 'hal'},
    'devices': {'devices', 'store', 'core', 'hal', 'drivers'},
    'store': {'store', 'core', 'hal'},
    'core': {'core'},
    'hal': {'hal'},
    'chip': {'core', 'hal'},          # Plus its own directory.
    'mcu': {'hal', 'external'},       # Plus its own directory.
    'boards': {'boards', 'store', 'core', 'hal', 'drivers'},
}
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)


def layer(path):
    parts = path.relative_to(ROOT).parts
    if parts[0] == 'drivers':
        return ('mcu' if (ROOT / 'drivers' / parts[1] / 'mcu.mk').exists() else 'chip'), parts[1]
    return parts[0], None


def violations():
    found = []
    for path in sorted(ROOT.rglob('*')):
        if path.suffix not in ('.c', '.h') or 'external' in path.parts:
            continue
        kind, own = layer(path)
        if kind not in ALLOWED:
            continue
        for target in INCLUDE.findall(path.read_text(errors='replace')):
            if '/' not in target:
                continue  # Same-directory or toolchain headers.
            top = target.split('/')[0]
            if kind in ('chip', 'mcu') and top == 'drivers':
                if target.split('/')[1] == own:
                    continue
                if kind == 'chip' or target.split('/')[1] != own:
                    found.append((path, target))
                continue
            if top not in ALLOWED[kind]:
                found.append((path, target))
    return found


def main():
    found = violations()
    for path, target in found:
        print(f'{path.relative_to(ROOT.parent)}: includes {target}')
    if found:
        print(f'{len(found)} layer violation(s); see docs/development.md §3', file=sys.stderr)
        return 1
    print('Layers OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
