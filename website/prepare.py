#!/usr/bin/env python3
"""Copy the buildless website and verified firmware artifacts for GitHub Pages."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from flash import validate_image

VARIANTS = {'uvk6-si4732': ('uvk6', 1), 'uvk6-bk1080': ('uvk6', 0),
            'uvk1-bk1080': ('uvk1', 0)}


def prepare(artifacts, output, commit):
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('Expected a full Git commit SHA')
    source = ROOT / 'website'
    if output.exists():
        raise ValueError('Output directory must not already exist')
    # Validate every artifact before producing a publishable directory.
    found = {}
    for metadata in artifacts.rglob('build-info.json'):
        info = json.loads(metadata.read_text())
        variant = info['variant']
        if variant not in VARIANTS or variant in found:
            raise ValueError(f'Unexpected or duplicate variant: {variant}')
        board, si = VARIANTS[variant]
        if info['board'] != board or info['commit'] != commit or info['flags']['ENABLE_SI4732'] != si:
            raise ValueError(f'Artifact target or commit mismatch: {variant}')
        filename = f'{variant}.bin'
        data = (metadata.parent / filename).read_bytes()
        validate_image(data, board)
        digest = hashlib.sha256(data).hexdigest()
        if len(data) != info['raw_bytes'] or digest != info['sha256'][filename]:
            raise ValueError(f'Artifact checksum mismatch: {variant}')
        found[variant] = (data, info, {
            'variant': variant, 'board': board, 'file': filename,
            'bytes': len(data), 'sha256': digest,
        })
    if set(found) != set(VARIANTS):
        raise ValueError('All three firmware variants are required')
    output.mkdir(parents=True)
    files = ('index.html', 'app.js', 'serial.js', 'protocol.js', 'style.css', 'favicon.svg')
    asset_urls = {f'./{filename}' for filename in files}
    asset_urls.update(f'./{path.relative_to(source).as_posix()}' for path in (source / 'assets').rglob('*') if path.is_file())
    for filename in files:
        content = (source / filename).read_text()
        # Version the complete local module graph with the same deployment SHA.
        # This also keeps modulepreload and import() pointed at one module URL.
        for url in sorted(asset_urls):
            content = re.sub(r'([\'\"])' + re.escape(url) + r'\1',
                             lambda match: f'{match[1]}{url}?v={commit}{match[1]}', content)
        (output / filename).write_text(content)
    shutil.copytree(source / 'assets', output / 'assets')
    (output / '.nojekyll').touch()
    firmware = output / 'firmware'
    firmware.mkdir()
    entries = {}
    for variant, (data, info, entry) in found.items():
        (firmware / entry['file']).write_bytes(data)
        (firmware / f'{variant}.json').write_text(json.dumps(info, indent=2) + '\n')
        entries[variant] = entry
    (firmware / 'manifest.json').write_text(json.dumps({'commit': commit, 'variants': entries}, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--commit', required=True)
    args = parser.parse_args()
    prepare(args.artifacts, args.output, args.commit)
