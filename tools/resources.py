#!/usr/bin/env python3
"""Install EEPROM resources (the SSB patch; fonts later) and maintain the PA
calibration through the running firmware's UART, and read them back.

The radio runs normal firmware (not the bootloader). Resources live in the
reserved tail at the top of the EEPROM, each behind a 16-byte header
(magic, length, CRC-32) that is written last; see src/core/resource.h.
Calibration writes reach only the 9 PA bytes of each band record at
0x1ED0 + band*16; the whole 0x1E00-0x1FFF block is backed up first.
The firmware refuses writes anywhere else.
"""
import argparse
import importlib.util
from pathlib import Path
import re
import struct
import sys
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('flasher', Path(__file__).with_name('flash.py'))
flasher = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(flasher)

HEADER_BYTES = 16
CHUNK = 128  # Largest read/write the firmware accepts per command.
CAPACITY = 524288  # UV-K6 512 KiB (AGENTS.md); --capacity for other parts.
SSB_MAGIC = b'SSBP'
SSB_BYTES = 8840
SSB_PATCH = ROOT / 'resources/si4735/patch_init.h'
# Factory calibration block and its PA rows (src/hal/eeprom-layout.h).
CAL_START, CAL_BYTES = 0x1E00, 0x200
PA_BASE, PA_BANDS, PA_STRIDE, PA_BYTES = 0x1ED0, 7, 16, 9
# Reference rows for an erased radio, from three public UV-K5(8) dumps:
# Low/Medium identical in all three, High the per-anchor minimum. They are
# not this radio's measured calibration.
PA_REFERENCE = {2: bytes.fromhex('5f5f5f 696969 7a8b7f'),
                5: bytes.fromhex('5a5a5a 646464 7b7b7a')}


def parse_patch(text):
    """Bytes of the ssb_patch_content[] array in a PU2CLR patch header."""
    start = text.find('ssb_patch_content[]')
    if start < 0:
        raise ValueError('No ssb_patch_content[] array')
    body = text[text.index('{', start) + 1:text.index('}', start)]
    data = bytes(int(value, 16) for value in re.findall(r'0x([0-9A-Fa-f]{2})', body))
    if len(data) != SSB_BYTES:
        raise ValueError(f'Patch is {len(data)} bytes, expected {SSB_BYTES}')
    # Each 8-byte line is a PATCH_ARGS (0x15) or PATCH_DATA (0x16) command.
    if data[0] != 0x15 or any(data[i] not in (0x15, 0x16) for i in range(0, len(data), 8)):
        raise ValueError('Not an SI473x patch: lines must start with 0x15/0x16')
    return data


def header(magic, data):
    return magic + struct.pack('<II', len(data), zlib.crc32(data)) + b'\xff' * 4


def ssb_address(capacity):
    if capacity < 0x20000:
        raise ValueError('EEPROM too small for resources')
    return capacity - SSB_BYTES


def chunks(address, size):
    for offset in range(0, size, CHUNK):
        yield address + offset, min(CHUNK, size - offset)


class Radio:
    """Firmware UART: plain frames after the 0x0514 hello, stamped requests."""

    def __init__(self, link):
        self.link = link
        self.stamp = int(time.time()) & 0xffffffff
        version = self.request(struct.pack('<HHI', 0x0514, 4, self.stamp), 0x0515)
        self.version = version[4:20].split(b'\0')[0].decode(errors='replace')

    def request(self, payload, reply_id, timeout=2.0):
        self.link.send(payload, obfuscate=False)
        deadline = time.monotonic() + timeout
        while True:
            reply = self.link.receive(deadline, obfuscate=False)
            if int.from_bytes(reply[:2], 'little') == reply_id:
                return reply

    def read(self, address, size):
        out = bytearray()
        for at, n in chunks(address, size):
            reply = self.request(struct.pack('<HHIB3xI', 0x051B, 12, at, n, self.stamp), 0x051C)
            offset, count = struct.unpack_from('<IB', reply, 4)
            if offset != at or count != n or len(reply) < 12 + n:
                raise IOError(f'Bad read reply at 0x{at:05X}')
            out += reply[12:12 + n]
        return bytes(out)

    def write(self, address, data, progress=None):
        for at, n in chunks(address, len(data)):
            piece = data[at - address:at - address + n]
            payload = struct.pack('<HHIB3xI', 0x0570, 12 + n, at, n, self.stamp) + piece
            offset, ok = struct.unpack_from('<IB', self.request(payload, 0x0571), 4)
            if offset != at or not ok:
                raise IOError(f'Radio refused or failed the write at 0x{at:05X}')
            if progress:
                progress(at + n - address, len(data))

    def reset(self):
        self.link.send(struct.pack('<HH', 0x05DD, 0), obfuscate=False)


def backup_path(backup_dir, name):
    """A new file per backup: never overwrite one taken in the same second."""
    backup_dir.mkdir(parents=True, exist_ok=True)
    stem = f'{name}-{time.strftime("%Y%m%d-%H%M%S")}'
    path, n = backup_dir / f'{stem}.bin', 1
    while path.exists():
        path, n = backup_dir / f'{stem}-{n}.bin', n + 1
    return path


def install(radio, address, magic, data, backup_dir, log=print):
    """Header cleared first and written last; returns False if already there."""
    image = header(magic, data) + data
    start = address - HEADER_BYTES
    current = radio.read(start, len(image))
    if current == image:
        log('Already installed; nothing written.')
        return False
    if current != b'\xff' * len(image):
        path = backup_path(backup_dir, f'eeprom-{start:05X}')
        path.write_bytes(current)
        log(f'Previous contents saved to {path}')
    radio.write(start, b'\xff' * HEADER_BYTES)
    radio.write(address, data, lambda done, total: log(f'\r{done}/{total} bytes', end=''))
    log('')
    radio.write(start, image[:HEADER_BYTES])
    if radio.read(start, len(image)) != image:
        raise IOError('Read-back differs after writing')
    log(f'Installed and verified: {len(data)} bytes at 0x{address:05X}, CRC-32 0x{zlib.crc32(data):08x}')
    return True


def verify(radio, address, magic, data, log=print):
    image = header(magic, data) + data
    current = radio.read(address - HEADER_BYTES, len(image))
    if current == image:
        log(f'Installed: matches ({len(data)} bytes, CRC-32 0x{zlib.crc32(data):08x})')
        return True
    if current[:4] != magic:
        log('Not installed (no header)')
    else:
        differ = sum(a != b for a, b in zip(current, image))
        log(f'Installed copy differs from the file in {differ} bytes')
    return False


def pa_address(band):
    if not 0 <= band < PA_BANDS:
        raise ValueError(f'Band must be 0-{PA_BANDS - 1}')
    return PA_BASE + band * PA_STRIDE


def pa_rows(block):
    """PA rows {band: 9 bytes} from a 0x1E00 calibration block."""
    if len(block) != CAL_BYTES:
        raise ValueError(f'Calibration block must be {CAL_BYTES} bytes')
    return {band: bytes(block[pa_address(band) - CAL_START:][:PA_BYTES]) for band in range(PA_BANDS)}


def parse_row(text):
    row = bytes.fromhex(text.replace(':', ' '))
    if len(row) != PA_BYTES:
        raise ValueError(f'A PA row is {PA_BYTES} bytes: Low x3, Medium x3, High x3')
    if any(value in (0x00, 0xFF) for value in row):
        raise ValueError('00 and FF mean "no calibration" to the firmware')
    return row


def erased(row):
    return row in (b'\0' * PA_BYTES, b'\xff' * PA_BYTES)


def show_row(band, row):
    levels = ' '.join(row[i:i + 3].hex(' ') for i in (0, 3, 6))
    note = ' (erased)' if erased(row) else ''
    return f'band {band} 0x{pa_address(band):04X}: {levels}{note}'


def backup_calibration(radio, backup_dir, log=print):
    """Saves 0x1E00-0x1FFF; every calibration command starts here."""
    block = radio.read(CAL_START, CAL_BYTES)
    path = backup_path(backup_dir, 'calibration')
    path.write_bytes(block)
    log(f'Calibration 0x{CAL_START:04X}-0x{CAL_START + CAL_BYTES - 1:04X} saved to {path}')
    return block


def write_rows(radio, block, rows, confirm, log=print):
    """Writes {band: row} that differ from block after confirm(); read back."""
    current = pa_rows(block)
    changes = {band: row for band, row in rows.items() if current[band] != row}
    if not changes:
        log('Nothing to change.')
        return False
    for band, row in changes.items():
        log('  now  ' + show_row(band, current[band]))
        log('  new  ' + show_row(band, row))
    if not confirm():
        log('Cancelled; nothing written.')
        return False
    for band, row in changes.items():
        radio.write(pa_address(band), row)
        if radio.read(pa_address(band), PA_BYTES) != row:
            raise IOError(f'Read-back differs for band {band}')
    log(f'Written and verified: band {", ".join(map(str, changes))}')
    return True


def ask():
    return input('Write these PA rows? Type yes: ').strip().lower() == 'yes'


def calibration(radio, args):
    if args.command == 'cal-read':
        for band, row in pa_rows(backup_calibration(radio, args.backup_dir)).items():
            print(show_row(band, row))
        return 0
    # Check the input before touching the radio.
    if args.command == 'cal-restore':
        rows = pa_rows(args.file.read_bytes())
    elif args.command == 'cal-write':
        pa_address(args.band)
        rows = {args.band: parse_row(args.row)}
    block = backup_calibration(radio, args.backup_dir)
    confirm = (lambda: True) if args.yes else ask
    if args.command == 'cal-seed':
        # Only erased rows of the bands TX uses; measured values never change.
        rows = {band: row for band, row in PA_REFERENCE.items() if erased(pa_rows(block)[band])}
        print('Reference values, not measured for this radio.')
    write_rows(radio, block, rows, confirm)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--port', required=True, help='e.g. /dev/cu.usbserial-1410')
    parser.add_argument('--capacity', type=int, default=CAPACITY, help='EEPROM bytes')
    commands = parser.add_subparsers(dest='command', required=True)
    for name, text in (('ssb-write', 'install the SSB patch'), ('ssb-verify', 'compare it')):
        command = commands.add_parser(name, help=text)
        command.add_argument('--patch', type=Path, default=SSB_PATCH)
    commands.choices['ssb-write'].add_argument('--backup-dir', type=Path, default=ROOT / 'backups')
    commands.choices['ssb-write'].add_argument('--no-reboot', action='store_true',
                                               help='skip the reset that reloads the patch check')
    cal_read = commands.add_parser('cal-read', help='back up calibration, show the PA rows')
    cal_write = commands.add_parser('cal-write', help='write one PA row')
    cal_write.add_argument('band', type=int, help='0-6; TX uses 2 (136-174 MHz) and 5 (400-470 MHz)')
    cal_write.add_argument('row', help="9 hex bytes, Low/Medium/High x3, e.g. '5f5f5f 696969 7a8b7f'")
    cal_seed = commands.add_parser('cal-seed', help='reference values into erased band 2/5 rows')
    cal_restore = commands.add_parser('cal-restore', help='PA rows from a cal-read backup')
    cal_restore.add_argument('file', type=Path)
    for command in (cal_read, cal_write, cal_seed, cal_restore):
        command.add_argument('--backup-dir', type=Path, default=ROOT / 'backups')
    for command in (cal_write, cal_seed, cal_restore):
        command.add_argument('--yes', action='store_true', help='skip the confirmation')
    read = commands.add_parser('read', help='save an EEPROM range to a file')
    read.add_argument('address', type=lambda value: int(value, 0))
    read.add_argument('size', type=lambda value: int(value, 0))
    read.add_argument('output', type=Path)
    args = parser.parse_args()

    link = flasher.Bootloader(args.port)
    try:
        radio = Radio(link)
        print(f'Firmware {radio.version}')
        if args.command == 'read':
            args.output.write_bytes(radio.read(args.address, args.size))
            print(f'Saved {args.size} bytes from 0x{args.address:05X} to {args.output}')
            return 0
        if args.command.startswith('cal-'):
            return calibration(radio, args)
        data = parse_patch(args.patch.read_text())
        address = ssb_address(args.capacity)
        if args.command == 'ssb-verify':
            return 0 if verify(radio, address, SSB_MAGIC, data) else 1
        if install(radio, address, SSB_MAGIC, data, args.backup_dir,
                   lambda *a, **k: print(*a, **k, flush=True)) and not args.no_reboot:
            radio.reset()
            print('Radio restarting; About → SSB patch should read Present.')
        return 0
    except TimeoutError:  # Before OSError, its base class.
        print('No reply: is the radio on with this firmware (not in the bootloader)?',
              file=sys.stderr)
        return 1
    except (ValueError, OSError) as error:
        print(f'Error: {error}', file=sys.stderr)
        return 1
    finally:
        link.close()


if __name__ == '__main__':
    sys.exit(main())
