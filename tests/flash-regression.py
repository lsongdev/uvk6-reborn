"""Host-only tests: simulated bootloader replies, never access a radio."""
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('flasher', Path(__file__).parents[1] / 'tools/flash.py')
flasher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flasher)


class FlashTests(unittest.TestCase):
    def test_actual_bootloader_30002_hello(self):
        loader = flasher.Bootloader.__new__(flasher.Bootloader)
        loader.fd = -1
        loader.buffer = bytearray.fromhex(
            'abcd24000e6934e62f930944366682755d441681'
            '0a6c53e61dbf3d700f05e7402709e980166c14c6ffffdcba')
        hello = loader.receive(flasher.time.monotonic() + 1)
        self.assertEqual(hello[:4], bytes.fromhex('18052000'))
        self.assertEqual(hello[20:36].split(b'\0')[0], b'3.00.02')

    def test_raw_image_bounds_and_vectors(self):
        image = struct.pack('<II', 0x20004000, 9) + bytes(300)
        flasher.validate_image(image)
        for bad in (b'', bytes(300), image + bytes(flasher.FLASH_SIZE),
                    struct.pack('<II', 0x20004000, 8) + bytes(300)):
            with self.assertRaises(ValueError):
                flasher.validate_image(bad)

    def test_k1_vectors_and_full_472_pages(self):
        image = struct.pack('<II', 0x20004000, 0x08002809) + bytes(118 * 1024 - 8)
        flasher.validate_image(image, 'uvk1')
        with self.assertRaises(ValueError):
            flasher.validate_image(image, 'uvk6')
        with self.assertRaises(ValueError):
            flasher.validate_image(image + b'x', 'uvk1')
        with self.assertRaises(ValueError):
            flasher.validate_image(struct.pack('<II', 0x20004000, 9) + bytes(300), 'uvk1')

        class Fake:
            def __init__(self):
                self.sent = []
            def send(self, payload):
                self.sent.append(payload)
            def receive(self, deadline):
                if len(self.sent) <= 2:
                    return bytes.fromhex('18052000') + bytes(16) + b'7.02.02'.ljust(16, b'\0')
                return bytes.fromhex('1a050800') + self.sent[-1][4:10] + bytes(2)
        loader = Fake()
        with patch.object(flasher.time, 'sleep'), patch('builtins.print'):
            flasher.flash(loader, image, board='uvk1')
        self.assertEqual(loader.sent[:3], [bytes.fromhex('30050400') + b'7.02'] * 3)
        self.assertEqual(len(loader.sent), 3 + 472)
        self.assertEqual(loader.sent[3][8:16], struct.pack('<HHI', 0, 472, 0))
        self.assertEqual(loader.sent[3 + 256][8:16], struct.pack('<HHI', 256, 472, 0))
        self.assertEqual(loader.sent[-1][8:16], struct.pack('<HHI', 471, 472, 0))
        self.assertEqual(len(loader.sent[-1]), 272)

    def test_k1_rejects_k6_before_handshake(self):
        class Fake:
            def send(self, payload):
                self.fail = True
                raise AssertionError('must not send to wrong board')
            def receive(self, deadline):
                return bytes.fromhex('18052000') + bytes(16) + b'3.00.02'.ljust(16, b'\0')
        with patch('builtins.print'), self.assertRaises(ValueError):
            flasher.flash(Fake(), bytes(300), board='uvk1')

    def test_fragmented_frames_bad_crc_and_placeholder_crc(self):
        loader = flasher.Bootloader.__new__(flasher.Bootloader)
        loader.fd = -1
        reply = bytes.fromhex('1a0508008a8d9f1d00000000')
        corrupt = bytearray(flasher.packet(reply))
        corrupt[5] ^= 1
        placeholder = b'\xab\xcd' + struct.pack('<H', len(reply)) + flasher.xor(reply + b'\xff\xff') + b'\xdc\xba'
        loader.buffer = bytearray(b'noise' + corrupt + placeholder[:3])
        with patch.object(flasher.select, 'select', return_value=([-1], [], [])), \
             patch.object(flasher.os, 'read', return_value=placeholder[3:]):
            self.assertEqual(loader.receive(flasher.time.monotonic() + 1), reply)
        loader.buffer.extend(flasher.packet(reply))
        self.assertEqual(loader.receive(flasher.time.monotonic() + 1), reply)

    def test_retry_stale_ack_and_partial_last_block(self):
        class Fake:
            def __init__(self):
                self.sent = []
                self.replies = iter([
                    bytes.fromhex('18052000') + bytes(16) + b'3.00.02'.ljust(16, b'\0'),
                    TimeoutError(),
                    bytes.fromhex('1a0508008a8d9f1d01000000'), # Wrong address.
                    bytes.fromhex('1a0508008a8d9f1d00000000'),
                    bytes.fromhex('1a0508008a8d9f1d00000000'), # Previous block.
                    bytes.fromhex('1a0508008a8d9f1d01000000'),
                ])
            def send(self, payload):
                self.sent.append(payload)
            def receive(self, deadline):
                reply = next(self.replies)
                if isinstance(reply, Exception):
                    raise reply
                return reply
        loader = Fake()
        with patch.object(flasher.time, 'sleep'), patch('builtins.print'):
            flasher.flash(loader, bytes(300))
        self.assertEqual(loader.sent[1], loader.sent[2]) # Identical retry.
        last = loader.sent[-1]
        self.assertEqual(last[8:14], bytes.fromhex('010002002c00'))
        self.assertEqual(len(last), 272)

    def test_failure_never_advances_to_next_block(self):
        class Fake:
            sent = []
            def send(self, payload):
                self.sent.append(payload)
            def receive(self, deadline):
                if not self.sent:
                    return bytes.fromhex('18052000') + bytes(16) + b'3.00.02'.ljust(16, b'\0')
                raise TimeoutError()
        loader = Fake()
        with patch.object(flasher.time, 'sleep'), patch('builtins.print'), self.assertRaises(TimeoutError):
            flasher.flash(loader, bytes(300), retries=2)
        self.assertEqual(len(loader.sent), 3) # Handshake and two attempts.
        self.assertEqual(loader.sent[-1][8:10], b'\0\0')

    def test_other_board_bootloader_gets_nothing(self):
        # The UV-K1 hello read on 2026-10-19 (Bootloader 7.02.02).
        hello = bytes.fromhex('180520005434504108363233588e800078d50456'
                              '372e30322e3032000000c42000080000')
        class Fake:
            sent = []
            def send(self, payload):
                self.sent.append(payload)
            def receive(self, deadline):
                return hello
        loader = Fake()
        with patch('builtins.print'), self.assertRaises(ValueError):
            flasher.flash(loader, bytes(300))
        self.assertEqual(loader.sent, []) # Not even the handshake.


if __name__ == '__main__':
    unittest.main()
