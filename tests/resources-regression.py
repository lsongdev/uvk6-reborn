"""Host-only tests for tools/resources.py against a simulated firmware."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

spec = importlib.util.spec_from_file_location('resources', Path(__file__).parents[1] / 'tools/resources.py')
resources = importlib.util.module_from_spec(spec)
spec.loader.exec_module(resources)

CAPACITY = 524288
TAIL = CAPACITY - resources.HEADER_BYTES - resources.SSB_BYTES


class FakeFirmware:
    """Answers 0x0514/0x051B/0x0570 like src/hal/uart.c; writes only the tail."""

    def __init__(self):
        self.eeprom = bytearray(b'\xff' * CAPACITY)
        self.stamp = None
        self.reply = None
        self.writes = []
        self.reset = False

    def send(self, payload, obfuscate=True):
        assert not obfuscate
        kind = int.from_bytes(payload[:2], 'little')
        if kind == 0x0514:
            self.stamp = struct.unpack_from('<I', payload, 4)[0]
            self.reply = struct.pack('<HH', 0x0515, 36) + b's0v4'.ljust(16, b'\0') + bytes(20)
        elif kind == 0x051B:
            at, n, stamp = struct.unpack_from('<IB3xI', payload, 4)
            assert stamp == self.stamp and 0 < n <= 128 and at + n <= CAPACITY
            self.reply = struct.pack('<HHIB3x', 0x051C, n + 12, at, n) + self.eeprom[at:at + n]
        elif kind == 0x0570:
            at, n, stamp = struct.unpack_from('<IB3xI', payload, 4)
            assert stamp == self.stamp and len(payload) == 16 + n
            ok = TAIL <= at and at + n <= CAPACITY
            if ok:
                self.eeprom[at:at + n] = payload[16:]
                self.writes.append((at, n))
            self.reply = struct.pack('<HHIB3x', 0x0571, 8, at, ok)
        elif kind == 0x05DD:
            self.reset = True

    def receive(self, deadline, obfuscate=True):
        assert not obfuscate and self.reply is not None
        reply, self.reply = self.reply, None
        return reply


class ResourceTests(unittest.TestCase):
    def setUp(self):
        self.patch = resources.parse_patch(resources.SSB_PATCH.read_text())
        self.firmware = FakeFirmware()
        self.radio = resources.Radio(self.firmware)
        self.address = resources.ssb_address(CAPACITY)
        self.backups = Path(tempfile.mkdtemp())

    def install(self):
        return resources.install(self.radio, self.address, resources.SSB_MAGIC, self.patch,
                                 self.backups, log=lambda *a, **k: None)

    def test_repository_patch(self):
        self.assertEqual(len(self.patch), 8840)
        self.assertEqual(zlib.crc32(self.patch), 0xf9c0f474)
        self.assertEqual(self.address, 0x7DD78)
        self.assertEqual(self.radio.version, 's0v4')
        with self.assertRaises(ValueError):
            resources.parse_patch('const uint8_t ssb_patch_content[] = {0x15, 0x00};')

    def test_header_matches_firmware_layout(self):
        h = resources.header(b'SSBP', self.patch)
        self.assertEqual(len(h), 16)
        self.assertEqual(h[:12], b'SSBP' + struct.pack('<II', 8840, 0xf9c0f474))

    def test_install_clears_header_first_and_writes_it_last(self):
        self.assertTrue(self.install())
        writes = self.firmware.writes
        self.assertEqual(writes[0], (TAIL, 16))
        self.assertEqual(writes[-1], (TAIL, 16))
        self.assertTrue(all(n <= 128 for _, n in writes))
        image = self.firmware.eeprom[TAIL:]
        self.assertEqual(bytes(image[16:]), self.patch)
        self.assertEqual(bytes(image[:16]), resources.header(b'SSBP', self.patch))
        self.assertTrue(resources.verify(self.radio, self.address, b'SSBP', self.patch, log=lambda *a: None))
        self.assertEqual(list(self.backups.iterdir()), [])  # Erased tail: nothing to keep.
        self.firmware.writes.clear()
        self.assertFalse(self.install())  # Same image again: no writes.
        self.assertEqual(self.firmware.writes, [])

    def test_previous_contents_are_backed_up(self):
        self.firmware.eeprom[TAIL + 100] = 0x42
        self.install()
        saved = list(self.backups.iterdir())
        self.assertEqual(len(saved), 1)
        self.assertEqual(saved[0].read_bytes()[100], 0x42)

    def test_refused_write_raises(self):
        with self.assertRaises(IOError):
            self.radio.write(0x1E00, b'\0' * 16)  # Calibration is outside the tail.

    def test_verify_reports_damage(self):
        self.install()
        self.firmware.eeprom[self.address + 5] ^= 1
        self.assertFalse(resources.verify(self.radio, self.address, b'SSBP', self.patch, log=lambda *a: None))


if __name__ == '__main__':
    unittest.main()
