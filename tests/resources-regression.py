"""Host-only tests for tools/resources.py against a simulated firmware."""
from argparse import Namespace
from contextlib import redirect_stdout
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zlib

spec = importlib.util.spec_from_file_location('resources', Path(__file__).parents[1] / 'tools/resources.py')
resources = importlib.util.module_from_spec(spec)
spec.loader.exec_module(resources)

FAST_READ = resources.ReadPolicy(interval_seconds=0, backoff_seconds=0)

CAPACITY = 524288
TAIL = CAPACITY - resources.HEADER_BYTES - resources.SSB_BYTES


class FakeFirmware:
    """Answers 0x0514/0x051B/0x0570 like src/boards/uvk6/uart.c; writes only the tail
    and the calibration block 0x1E00-0x1FFF."""

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
            cal = 0x1E00 <= at and at + n <= 0x2000
            ok = TAIL <= at and at + n <= CAPACITY or cal
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
        self.radio = resources.Radio(self.firmware, FAST_READ)
        self.address = resources.ssb_address(CAPACITY)
        self.backups = Path(tempfile.mkdtemp())

    def install(self):
        return resources.install(self.radio, self.address, resources.SSB_MAGIC, self.patch,
                                 self.backups, log=lambda *a, **k: None)

    def test_rf_snapshot(self):
        class Snapshot(FakeFirmware):
            def send(self, payload, obfuscate=True):
                if int.from_bytes(payload[:2], 'little') == 0x0576:
                    assert len(payload) == 8 and struct.unpack_from('<I', payload, 4)[0] == self.stamp
                    self.reply = struct.pack('<HHHHHHHH', 0x0577, 12, 4829, 2, 0x3B, 22654, 0x48, 0xB2CF)
                else:
                    super().send(payload, obfuscate)
        firmware = Snapshot()
        radio = resources.Radio(firmware)
        self.assertEqual(radio.rf_registers(), (4829, {0x3B: 22654, 0x48: 0xB2CF}))
        self.assertEqual(firmware.writes, [])
        for bad in (b'', struct.pack('<HHHH', 0x0577, 4, 4829, 1),
                    struct.pack('<HHHHHHHH', 0x0577, 12, 4829, 2, 0x47, 0, 0x47, 0)):
            with patch.object(radio, 'request', return_value=bad):
                with self.assertRaises(IOError):
                    radio.rf_registers()

    def test_k1_info_and_full_physical_flash_read(self):
        class PhysicalFlash(FakeFirmware):
            def __init__(self):
                super().__init__()
                self.eeprom = bytearray(bytes(range(256)) * 8192)
                self.reads = 0
            def send(self, payload, obfuscate=True):
                kind = int.from_bytes(payload[:2], 'little')
                if kind == 0x0572:
                    assert struct.unpack_from('<I', payload, 4)[0] == self.stamp
                    self.reply = struct.pack('<HHI24s', 0x0573, 28, len(self.eeprom), b'SPI 856015 RO')
                elif kind == 0x051B:
                    at, n, stamp = struct.unpack_from('<IB3xI', payload, 4)
                    assert stamp == self.stamp and 0 < n <= 128 and at + n <= len(self.eeprom)
                    self.reads += 1
                    self.reply = struct.pack('<HHIB3x', 0x051C, n + 12, at, n) + self.eeprom[at:at + n]
                else:
                    super().send(payload, obfuscate)
        firmware = PhysicalFlash()
        radio = resources.Radio(firmware, FAST_READ)
        self.assertEqual(radio.info(), (2097152, 'SPI 856015 RO'))
        self.assertEqual(radio.read(0, 2097152), firmware.eeprom)
        self.assertEqual(firmware.reads, 16384)
        self.assertEqual(firmware.writes, [])

    def test_read_retries_and_ignores_stale_chunks(self):
        class Flaky(FakeFirmware):
            def __init__(self):
                super().__init__()
                self.dropped = False
                self.stale = None
            def send(self, payload, obfuscate=True):
                super().send(payload, obfuscate)
                if int.from_bytes(payload[:2], 'little') == 0x051B:
                    address = struct.unpack_from('<I', payload, 4)[0]
                    if not self.dropped:
                        self.dropped = True
                        self.reply = None
                    elif address == 128:
                        self.stale = struct.pack('<HHIB3x', 0x051C, 140, 0, 128) + bytes(128)
            def receive(self, deadline, obfuscate=True):
                if self.stale is not None:
                    reply, self.stale = self.stale, None
                    return reply
                if self.reply is None:
                    raise TimeoutError()
                return super().receive(deadline, obfuscate)
        firmware = Flaky()
        radio = resources.Radio(firmware, FAST_READ)
        self.assertEqual(radio.read(0, 256), bytes([255]) * 256)
        self.assertTrue(firmware.dropped)
        self.assertEqual(firmware.writes, [])

    def test_export_checkpoints_and_resumes_without_overwriting(self):
        class Export:
            def __init__(self):
                self.calls = []
                self.fail = True
            def read(self, address, size):
                self.calls.append((address, size))
                if address == 65536 and self.fail:
                    raise TimeoutError('disconnected')
                return bytes([address // 65536]) * size
        radio = Export()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'dump.bin'
            with self.assertRaises(TimeoutError):
                resources.read_to_file(radio, 0, 131072, output)
            self.assertFalse(output.exists())
            self.assertEqual(output.with_name('dump.bin.part').stat().st_size, 65536)
            with self.assertRaises(ValueError):
                resources.read_to_file(radio, 0, 131072, output)
            with self.assertRaises(ValueError):
                resources.read_to_file(radio, 128, 131072, output, resume=True)
            radio.fail = False
            radio.calls.clear()
            resources.read_to_file(radio, 0, 131072, output, resume=True)
            self.assertEqual(radio.calls[0], (65536, resources.CHECKPOINT_BYTES))
            self.assertEqual(output.read_bytes(), bytes(65536) + bytes([1]) * 65536)
            self.assertFalse(output.with_name('dump.bin.part').exists())
            with self.assertRaises(ValueError):
                resources.read_to_file(radio, 0, 131072, output)

    def test_retry_budget_is_shared_across_checkpoints_and_stops(self):
        class Intermittent(FakeFirmware):
            def __init__(self):
                super().__init__()
                self.read_requests = 0
            def send(self, payload, obfuscate=True):
                super().send(payload, obfuscate)
                if int.from_bytes(payload[:2], 'little') == 0x051B:
                    self.read_requests += 1
                    if self.read_requests % 2:
                        self.reply = None
            def receive(self, deadline, obfuscate=True):
                if self.reply is None:
                    raise TimeoutError('lost reply')
                return super().receive(deadline, obfuscate)
        firmware = Intermittent()
        radio = resources.Radio(firmware, FAST_READ._replace(total_retries=1))
        with patch.object(resources.time, 'sleep') as sleep:
            self.assertEqual(radio.read(0, 128), bytes([255])*128)
            with self.assertRaisesRegex(TimeoutError, 'budget exhausted'):
                radio.read(128, 128)
        self.assertEqual(firmware.read_requests, 3)
        self.assertEqual(firmware.writes, [])

    def test_read_pacing_and_no_retry(self):
        radio = resources.Radio(FakeFirmware())
        with patch.object(resources.time, 'sleep') as sleep:
            radio.read(0, 256)
        self.assertEqual(sleep.call_args_list, [unittest.mock.call(0.02)] * 2)
        policy = FAST_READ._replace(retries=0, total_retries=0)
        radio = resources.Radio(FakeFirmware(), policy)
        with patch.object(radio, 'request', side_effect=TimeoutError()) as request:
            with self.assertRaises(TimeoutError):
                radio.read(0, 128)
        self.assertEqual(request.call_count, 1)
        for bad in (FAST_READ._replace(timeout_seconds=0),
                    FAST_READ._replace(retries=-1),
                    FAST_READ._replace(interval_seconds=float('nan'))):
            with self.assertRaises(ValueError):
                resources.Radio(None, bad) # Fail before any transport access.

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
            self.radio.write(0x1DF8, b'\0' * 16)  # Starts before calibration.

    def test_verify_reports_damage(self):
        self.install()
        self.firmware.eeprom[self.address + 5] ^= 1
        self.assertFalse(resources.verify(self.radio, self.address, b'SSBP', self.patch, log=lambda *a: None))



class CalibrationTests(unittest.TestCase):
    def setUp(self):
        self.firmware = FakeFirmware()
        self.firmware.eeprom[0x1E00:0x2000] = bytes(range(256)) * 2  # Other calibration.
        self.radio = resources.Radio(self.firmware, FAST_READ)
        self.backups = Path(tempfile.mkdtemp())
        self.log = []

    def run_command(self, command, **options):
        args = Namespace(command=command, backup_dir=self.backups, yes=True, **options)
        with redirect_stdout(io.StringIO()) as out:
            self.assertEqual(resources.calibration(self.radio, args), 0)
        return out.getvalue()

    def pa(self, band):
        at = 0x1ED0 + band * 16
        return bytes(self.firmware.eeprom[at:at + 9])

    def test_write_backs_up_and_touches_only_the_row(self):
        before = bytes(self.firmware.eeprom)
        self.run_command('cal-write', band=5, row='50:50:50 60 60 60 70 70 70')
        self.assertEqual(self.pa(5), bytes.fromhex('505050606060707070'))
        changed = [i for i, (a, b) in enumerate(zip(before, self.firmware.eeprom)) if a != b]
        self.assertTrue(changed and all(0x1F20 <= i < 0x1F29 for i in changed))
        saved = list(self.backups.iterdir())
        self.assertEqual(len(saved), 1)
        self.assertEqual(saved[0].read_bytes(), before[0x1E00:0x2000])

    def test_bad_rows_are_refused_before_any_access(self):
        for band, row in ((7, '50' * 9), (2, '50' * 8), (2, '00' + '50' * 8), (2, 'ff' * 9)):
            with self.assertRaises(ValueError):
                self.run_command('cal-write', band=band, row=row)
        self.assertEqual(list(self.backups.iterdir()), [])

    def test_seed_fills_only_erased_rows(self):
        self.firmware.eeprom[0x1EF0:0x1EF9] = b'\xff' * 9
        measured = self.pa(5)
        self.run_command('cal-seed')
        self.assertEqual(self.pa(2), resources.PA_REFERENCE[2])
        self.assertEqual(self.pa(5), measured)

    def test_restore_and_confirmation(self):
        self.run_command('cal-read')
        backup = next(self.backups.iterdir())
        self.run_command('cal-write', band=2, row='40' * 9)
        self.assertEqual(len(list(self.backups.iterdir())), 2)  # Same second: kept apart.
        args = Namespace(command='cal-restore', file=backup, backup_dir=self.backups, yes=False)
        with patch('builtins.input', return_value='no'), redirect_stdout(io.StringIO()):
            resources.calibration(self.radio, args)
        self.assertEqual(self.pa(2), bytes.fromhex('40' * 9))
        self.firmware.eeprom[0x1F40:0x1F4C] = bytes(12)  # Battery calibration too.
        with patch('builtins.input', return_value='yes'), redirect_stdout(io.StringIO()):
            resources.calibration(self.radio, args)
        self.assertEqual(bytes(self.firmware.eeprom[0x1E00:0x2000]), backup.read_bytes())
        self.assertTrue(all(0x1E00 <= at and at + n <= 0x2000 for at, n in self.firmware.writes))

    def test_restore_refuses_blank_or_short_backups(self):
        for data in (bytes(512), b'\xff' * 512, bytes(range(256)), bytes(0x1F0) + b'\x50' * 16):
            path = self.backups / 'bad.bin'
            path.write_bytes(data)
            with self.assertRaises(ValueError):
                self.run_command('cal-restore', file=path)
        self.assertEqual(list(self.backups.iterdir()), [path])  # Refused before any backup.


if __name__ == '__main__':
    unittest.main()
