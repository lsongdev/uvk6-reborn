#!/usr/bin/env python3
"""Flash a raw UV-K6 or UV-K1 image via its 38400-baud UART bootloader."""
import argparse
import binascii
import os
from pathlib import Path
import select
import struct
import sys
import termios
import time

KEY = bytes.fromhex('166c14e62e910d402135d5401303e980')
BLOCK_SIZE = 256
FLASH_SIZE = 0xF000  # Application partition; never overwrite the bootloader.
# UV-K6 (DP32G030) bootloaders report 3.x; the UV-K1 (PY32F071) reports 7.x.
# A DP32G030 image on a K1, or the reverse, leaves a radio only SWD can revive.
BOARDS = {'uvk6': (0, FLASH_SIZE, '3.'),
          'uvk1': (0x08002800, 118 * 1024, '7.')}


def xor(data):
    return bytes(value ^ KEY[i % len(KEY)] for i, value in enumerate(data))


def packet(payload, obfuscate=True):
    crc = struct.pack('<H', binascii.crc_hqx(payload, 0))
    body = payload + crc
    return b'\xab\xcd' + struct.pack('<H', len(payload)) + (xor(body) if obfuscate else body) + b'\xdc\xba'


def validate_image(image, board='uvk6'):
    origin, size, _ = BOARDS[board]
    if not 8 <= len(image) <= size:
        raise ValueError(f'Raw firmware must be 8..{size} bytes')
    stack, reset = struct.unpack_from('<II', image)
    if not (0x20000000 <= stack <= 0x20004000 and stack % 4 == 0
            and reset & 1 and origin + 8 <= (reset & ~1) < origin + len(image)):
        raise ValueError(f'Invalid {board} vector table; use firmware.bin, not firmware.packed.bin')


class Bootloader:
    def __init__(self, port):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.buffer = bytearray()
        try:
            attrs = termios.tcgetattr(self.fd)
            attrs[:6] = [0, 0, termios.CS8 | termios.CREAD | termios.CLOCAL,
                         0, termios.B38400, termios.B38400]
            attrs[6][termios.VMIN] = 0
            attrs[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
            termios.tcflush(self.fd, termios.TCIOFLUSH)
        except BaseException:
            self.close()
            raise

    def close(self):
        os.close(self.fd)

    def send(self, payload, obfuscate=True):
        data = packet(payload, obfuscate)
        deadline = time.monotonic() + 3
        while data:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([], [self.fd], [], remaining)[1]:
                raise TimeoutError('Serial write timed out')
            try:
                count = os.write(self.fd, data)
            except BlockingIOError:
                continue
            if not count:
                raise OSError('Serial connection closed')
            data = data[count:]

    def receive(self, deadline, obfuscate=True):
        while time.monotonic() < deadline:
            start = self.buffer.find(b'\xab\xcd')
            if start >= 0:
                del self.buffer[:start]
                if len(self.buffer) >= 4:
                    size = int.from_bytes(self.buffer[2:4], 'little')
                    if size > 512:
                        del self.buffer[0]
                        continue
                    if len(self.buffer) >= size + 8:
                        frame = bytes(self.buffer[:size + 8])
                        del self.buffer[:size + 8]
                        if frame[-2:] != b'\xdc\xba':
                            continue
                        body = frame[4:4 + size + 2]
                        decoded = xor(body) if obfuscate else body
                        payload = decoded[:-2]
                        crc = int.from_bytes(decoded[-2:], 'little')
                        # Stock bootloader replies append literal, un-XORed
                        # 0xffff; also accept firmware replies with encoded CRC.
                        if frame[4 + size:6 + size] == b'\xff\xff' or crc in (0xffff, binascii.crc_hqx(payload, 0)):
                            return payload
                        continue
            elif len(self.buffer) > 1:
                del self.buffer[:-1]
            remaining = deadline - time.monotonic()
            if remaining > 0 and select.select([self.fd], [], [], min(.1, remaining))[0]:
                try:
                    data = os.read(self.fd, 4096)
                except BlockingIOError:
                    continue
                if not data:
                    raise OSError('Serial connection closed')
                self.buffer.extend(data)
        raise TimeoutError('No valid bootloader reply')


def flash(loader, image, wait=45, retries=5, board='uvk6'):
    deadline = time.monotonic() + wait
    next_dot = min(time.monotonic() + 1, deadline)
    print(f'Waiting for Bootloader (up to {wait} seconds; starts immediately on handshake)',
          end='', flush=True)
    try:
        while True:
            try:
                hello = loader.receive(next_dot)
            except TimeoutError:
                print('.', end='', flush=True)
                if time.monotonic() >= deadline:
                    raise TimeoutError('Bootloader handshake timed out')
                next_dot = min(time.monotonic() + 1, deadline)
                continue
            if len(hello) >= 36 and hello[:4] == bytes.fromhex('18052000'):
                break
    finally:
        print(flush=True)
    version = hello[20:36].split(b'\0')[0].decode('ascii', errors='replace')
    print(f'Bootloader: {version}', flush=True)
    prefix = BOARDS[board][2]
    if not version.startswith(prefix):
        raise ValueError(f'Bootloader {version} is not {board} (expected {prefix}x); '
                         'nothing was written')
    if board == 'uvk1':
        # Reference _prog.py: 0x0530, four-byte version prefix, on three hellos.
        handshake = struct.pack('<HH', 0x0530, 4) + version[:4].encode('ascii').ljust(4, b'\0')
        for attempt in range(3):
            if attempt:
                handshake_deadline = time.monotonic() + 3
                while loader.receive(handshake_deadline)[:4] != bytes.fromhex('18052000'):
                    pass
            loader.send(handshake)
    else:
        loader.send(bytes.fromhex('30051000') + b'*.01.23' + bytes(9))
    time.sleep(.15)
    end = (len(image) + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1)
    for offset in range(0, len(image), BLOCK_SIZE):
        chunk = image[offset:offset + BLOCK_SIZE]
        address = struct.pack('<H', offset // BLOCK_SIZE)
        # _prog.py leaves the four reserved bytes zero on K1. K6 keeps its
        # measured short-page length field. Page number/count are both u16 LE.
        reserved = bytes(4) if board == 'uvk1' else struct.pack('<HH', len(chunk), 0)
        payload = (bytes.fromhex('19050c018a8d9f1d') + address
                   + struct.pack('<H', end // BLOCK_SIZE) + reserved
                   + chunk.ljust(BLOCK_SIZE, b'\0'))
        for attempt in range(retries):
            try:
                loader.send(payload)
                deadline = time.monotonic() + 3
                while True:
                    reply = loader.receive(deadline)
                    if (len(reply) >= 12 and reply[:8] == bytes.fromhex('1a0508008a8d9f1d')
                            and reply[8:10] == address and reply[10:12] == b'\0\0'):
                        break
                break
            except TimeoutError as error:
                if attempt + 1 == retries:
                    raise TimeoutError(f'Block 0x{offset:04x} failed after {retries} attempts') from error
                print(f'Retrying block 0x{offset:04x} ({attempt + 1}/{retries})', flush=True)
                time.sleep(.2)
        done = min(offset + BLOCK_SIZE, len(image))
        print(f'\rAcknowledged {done}/{len(image)} bytes ({done * 100 // len(image)}%)', end='', flush=True)
        time.sleep(.1)
    print('\nFlash complete: all blocks acknowledged. Power-cycle the radio.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--board', choices=BOARDS, default='uvk6')
    parser.add_argument('--port', required=True, help='e.g. /dev/cu.usbserial-1420 or /dev/ttyUSB0')
    parser.add_argument('--firmware', type=Path, default=None, help='raw image, not packed')
    args = parser.parse_args()
    try:
        image = (args.firmware or Path('bin') / args.board / 'firmware.bin').read_bytes()
        validate_image(image, args.board)
        print('Hold PTT while powering on to enter Bootloader.', flush=True)
        loader = Bootloader(args.port)
        try:
            flash(loader, image, board=args.board)
        finally:
            loader.close()
    except (OSError, ValueError, TimeoutError) as error:
        print(f'\nFlash failed: {error}. Re-enter Bootloader and rerun the full flash.', file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print('\nFlash interrupted. Re-enter Bootloader and rerun the full flash.', file=sys.stderr)
        return 130
    return 0


if __name__ == '__main__':
    sys.exit(main())
