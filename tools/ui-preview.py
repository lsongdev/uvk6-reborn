#!/usr/bin/env python3
"""Compile the firmware's real drawing code and export a single/dual VFO PNG."""
import argparse
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


def read_frame(path):
    magic, dimensions, pixels = path.read_bytes().split(b'\n', 2)
    if magic != b'P4' or dimensions != b'128 64' or len(pixels) != 1024:
        raise ValueError(f'Invalid framebuffer snapshot: {path}')
    return pixels


def png_chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)


def write_preview(path, left, right, scale):
    gap = 4 * scale
    width, height = 256 * scale + gap, 64 * scale
    rows = []
    for y in range(height):
        row = bytearray(b'\0')
        for x in range(width):
            if 128 * scale <= x < 128 * scale + gap:
                value = 255
            else:
                source = left if x < 128 * scale else right
                sx = x // scale if source is left else (x - 128 * scale - gap) // scale
                ink = source[(y // scale) * 16 + sx // 8] & (128 >> (sx % 8))
                value = 30 if ink else 232
            row.extend((value, value, value))
        rows.append(row)
    header = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', header)
                    + png_chunk(b'IDAT', zlib.compress(b''.join(rows))) + png_chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'docs/images/vfo-states.png')
    parser.add_argument('--scale', type=int, choices=range(1, 13), default=4)
    parser.add_argument('--frames', nargs=2, choices=['single', 'dual', 'plain', 'max', 'maxtext', 'lw', 'rds', 'frequency', 'name', 'tuned', 'tx', 'seek'],
                        default=['frequency', 'name'], help='left and right preview scenarios')
    args = parser.parse_args()
    try:
        with tempfile.TemporaryDirectory(prefix='uvk6-ui-preview-') as temp:
            directory = Path(temp)
            binary = directory / 'preview'
            sources = ['tools/ui-preview.c', 'src/ui/graphics.c', 'src/ui/components/vfo.c',
                       'src/ui/components/frequency.c',
                       'src/core/rds.c',
                       'src/ui/components/signal.c', 'src/core/radio-model.c', 'src/external/printf/printf.c']
            command = shlex.split(os.environ.get('CC', 'cc')) + [
                '-Isrc', '-std=c2x', '-ffunction-sections', '-fdata-sections',
                '-Wno-incompatible-pointer-types', '-DPRINTF_INCLUDE_CONFIG_H', '-Isrc/config',
                '-Isrc/external/CMSIS_5/CMSIS/Core/Include',
                '-Isrc/external/CMSIS_5/Device/ARM/ARMCM0/Include',
                *sources, '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections',
                '-o', str(binary)]
            subprocess.run(command, cwd=ROOT, check=True)
            subprocess.run([str(binary), str(directory)], check=True)
            write_preview(args.output, read_frame(directory / f'{args.frames[0]}.pbm'),
                          read_frame(directory / f'{args.frames[1]}.pbm'), args.scale)
        print(f'Preview: {args.output.resolve()} (left: {args.frames[0]}; right: {args.frames[1]})')
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'Preview failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
