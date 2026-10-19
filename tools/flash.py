#!/usr/bin/env python3
"""Flash a raw DP32G030 UV-K5/K6 image via its 38400-baud bootloader."""
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
BOOTLOADER_PREFIX = '3.'


def xor(data):
    return bytes(value ^ KEY[i % len(KEY)] for i, value in enumerate(data))


def packet(payload, obfuscate=True):
    crc = struct.pack('<H', binascii.crc_hqx(payload, 0))
    body = payload + crc
    return b'\xab\xcd' + struct.pack('<H', len(payload)) + (xor(body) if obfuscate else body) + b'\xdc\xba'


def validate_image(image):
    if not 8 <= len(image) <= FLASH_SIZE:
        raise ValueError(f'Raw firmware must be 8..{FLASH_SIZE} bytes')
    stack, reset = struct.unpack_from('<II', image)
    if not (0x20000000 <= stack <= 0x20004000 and stack % 4 == 0
            and reset & 1 and 8 <= (reset & ~1) < len(image)):
        raise ValueError('Invalid DP32G030 vector table; use firmware.bin, not firmware.packed.bin')


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


def flash(loader, image, wait=45, retries=5):
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
    if not version.startswith(BOOTLOADER_PREFIX):
        raise ValueError(f'Bootloader {version} is not a UV-K6 (expected {BOOTLOADER_PREFIX}x); '
                         'nothing was written')
    loader.send(bytes.fromhex('30051000') + b'*.01.23' + bytes(9))
    time.sleep(.15)
    end = (len(image) + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1)
    for offset in range(0, len(image), BLOCK_SIZE):
        chunk = image[offset:offset + BLOCK_SIZE]
        address = bytes([offset >> 8, offset & 255])
        payload = (bytes.fromhex('19050c018a8d9f1d') + address + bytes([end >> 8, 0])
                   + struct.pack('<H', len(chunk)) + b'\0\0' + chunk.ljust(BLOCK_SIZE, b'\0'))
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
    parser.add_argument('--port', required=True, help='e.g. /dev/cu.usbserial-1420 or /dev/ttyUSB0')
    parser.add_argument('--firmware', type=Path, default=Path('bin/uvk6/firmware.bin'), help='raw image, not packed')
    args = parser.parse_args()
    try:
        image = args.firmware.read_bytes()
        validate_image(image)
        print('Hold PTT while powering on to enter Bootloader.', flush=True)
        loader = Bootloader(args.port)
        try:
            flash(loader, image)
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
