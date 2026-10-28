"""Pages packaging checks with synthetic images; no network or radio access."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('prepare', Path(__file__).parents[1] / 'prepare.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
COMMIT = 'a' * 40


class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.artifacts, self.output = self.root / 'artifacts', self.root / 'public'
        for variant, (board, si) in module.VARIANTS.items():
            folder = self.artifacts / variant
            folder.mkdir(parents=True)
            origin = module.validate_image.__globals__['BOARDS'][board][0]
            data = struct.pack('<II', 0x20004000, origin + 9) + bytes(300)
            filename = f'{variant}.bin'
            (folder / filename).write_bytes(data)
            (folder / 'build-info.json').write_text(json.dumps({
                'board': board, 'variant': variant, 'commit': COMMIT,
                'flags': {'ENABLE_SI4732': si}, 'raw_bytes': len(data),
                'sha256': {filename: hashlib.sha256(data).hexdigest()},
            }))

    def test_complete_website_has_matching_manifest_and_no_development_files(self):
        module.prepare(self.artifacts, self.output, COMMIT)
        manifest = json.loads((self.output / 'firmware/manifest.json').read_text())
        self.assertEqual(manifest['commit'], COMMIT)
        self.assertEqual(set(manifest['variants']), set(module.VARIANTS))
        for entry in manifest['variants'].values():
            data = (self.output / 'firmware' / entry['file']).read_bytes()
            self.assertEqual(entry['sha256'], hashlib.sha256(data).hexdigest())
        self.assertTrue((self.output / 'assets/main-screen.png').is_file())
        self.assertTrue((self.output / 'index.html').is_file())
        self.assertFalse((self.output / 'tests').exists())
        self.assertFalse((self.output / 'prepare.py').exists())
        index = (self.output / 'index.html').read_text()
        self.assertIn(f'./style.css?v={COMMIT}', index)
        self.assertEqual(index.count(f'./app.js?v={COMMIT}'), 2)
        import_map = json.loads(index.split('<script type="importmap">', 1)[1].split('</script>', 1)[0])
        for name in ('preact.js', 'preact-hooks.js', 'htm.js'):
            url = f'https://lsong.org/scripts/react/{name}'
            self.assertEqual(import_map['imports'][url], f'{url}?v={COMMIT}')
        self.assertLess(index.index('<script type="importmap">'), index.index('rel="modulepreload"'))
        app = (self.output / 'app.js').read_text()
        self.assertIn(f'./serial.js?v={COMMIT}', app)
        self.assertIn(f'./protocol.js?v={COMMIT}', app)
        self.assertIn(f'./assets/main-screen.png?v={COMMIT}', app)
        self.assertIn(f'./serial.js?v={COMMIT}', (self.output / 'protocol.js').read_text())

    def test_mixed_commits_and_modified_firmware_cannot_publish(self):
        metadata = self.artifacts / 'uvk1-bk1080/build-info.json'
        info = json.loads(metadata.read_text())
        info['commit'] = 'b' * 40
        metadata.write_text(json.dumps(info))
        with self.assertRaisesRegex(ValueError, 'commit mismatch'):
            module.prepare(self.artifacts, self.output, COMMIT)
        self.assertFalse(self.output.exists())
        info['commit'] = COMMIT
        metadata.write_text(json.dumps(info))
        binary = metadata.parent / 'uvk1-bk1080.bin'
        binary.write_bytes(binary.read_bytes() + b'x')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            module.prepare(self.artifacts, self.output, COMMIT)

    def test_missing_variant_and_wrong_chip_variant_cannot_publish(self):
        metadata = self.artifacts / 'uvk6-si4732/build-info.json'
        info = json.loads(metadata.read_text())
        info['flags']['ENABLE_SI4732'] = 0
        metadata.write_text(json.dumps(info))
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            module.prepare(self.artifacts, self.output, COMMIT)
        metadata.unlink()
        with self.assertRaisesRegex(ValueError, 'three firmware'):
            module.prepare(self.artifacts, self.output, COMMIT)


if __name__ == '__main__':
    unittest.main()
