#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build release products once per host and smoke-check their packaged tools."""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def run(*arguments: str) -> None:
    print('+ ' + subprocess.list2cmdline(arguments), flush=True)
    subprocess.run(arguments, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs-dir', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--products', choices=('all', 'tools', 'games'), default='all')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    host = {'win32': 'windows', 'linux': 'linux', 'darwin': 'macos'}[sys.platform]
    arch = {'AMD64': 'x86_64', 'aarch64': 'arm64'}.get(platform.machine(), platform.machine())
    common = ['--version', args.version, '--inputs-dir', str(args.inputs_dir),
              '--output-dir', str(args.output_dir), '--arch', arch]

    def build(product: str, directory: Path) -> None:
        run(sys.executable, str(ROOT / 'tools/build.py'), '--product', product,
            '--build-dir', str(directory), '--jobs', str(args.jobs), '--no-package', *common)

    def package(product: str, directory: Path, *extra: str) -> None:
        run(sys.executable, str(ROOT / 'tools/package_release.py'), '--product', product,
            '--build-dir', str(directory), '--platform', host, *common, *extra)

    if args.products in ('all', 'tools'):
        directory = args.build_dir / 'tools'
        build('wiillr', directory)
        package('wiillr', directory)
        matches = list(args.output_dir.glob(f'wiillr-{host}-{arch}-v{args.version}.*'))
        archive = next(p for p in matches if p.name.endswith(('.zip', '.tar.gz')))
        with tempfile.TemporaryDirectory(prefix='wiillr-package-smoke-') as temporary:
            destination = Path(temporary)
            if archive.suffix == '.zip':
                with zipfile.ZipFile(archive) as source:
                    source.extractall(destination)
            else:
                with tarfile.open(archive) as source:
                    source.extractall(destination, filter='data')
            cli = destination / 'wiillr/lib/wiillr/tools/wiillr.py'
            run(sys.executable, str(cli), '--version')
            run(sys.executable, str(cli), '--help')
            for command in ('inputs', 'discover', 'prepare', 'install-wad', 'install-ios'):
                run(sys.executable, str(cli), command, '--help')

    if args.products in ('all', 'games'):
        directory = args.build_dir / 'games'
        # Both products intentionally use the same qualified native executable.
        # Only package identity, launcher profile and mod catalogue differ.
        build('wii-menu', directory)
        products = json.loads((directory / 'build-products.json').read_text())
        runtime = ['--native-exe', products['native_exe'], '--gx-library', products['gx_library']]
        package('wii-menu', directory, *runtime)
        key = os.environ.get('WIILLR_WAD_COMMON_KEY', '')
        with tempfile.TemporaryDirectory(prefix='wiillr-private-package-') as temporary:
            extra: list[str] = []
            if key:
                value = base64.b64decode(key, validate=True)
                if len(value) != 16:
                    raise ValueError('Private WAD packaging input must contain exactly 16 bytes')
                path = Path(temporary) / 'common-key.bin'
                path.write_bytes(value)
                extra = ['--common-key', str(path)]
            package('mega-man-9', directory, *runtime, *extra)

    lines = []
    for archive in sorted(args.output_dir.iterdir()):
        if archive.name.endswith(('.zip', '.tar.gz')):
            digest = hashlib.sha256()
            with archive.open('rb') as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b''):
                    digest.update(block)
            lines.append(digest.hexdigest() + '  ' + archive.name)
    (args.output_dir / f'SHA256SUMS-{host}-{arch}.txt').write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
