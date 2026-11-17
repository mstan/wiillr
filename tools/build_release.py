#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build release products once per host and smoke-check their packaged tools."""
from __future__ import annotations
import argparse
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
TITLES = {'wii-menu': 'Wii Menu', 'mega-man-9': 'Mega Man 9'}


def run(*arguments: str) -> None:
    print('+ ' + subprocess.list2cmdline(arguments), flush=True)
    subprocess.run(arguments, check=True)


def smoke_game(archive: Path, product: str, host: str, output: Path) -> None:
    # This only checks empty-profile package startup. --launcher-check explicitly
    # suppresses all windows, dialogs and guest execution; no original data is used.
    with tempfile.TemporaryDirectory(prefix='wiillr-game-package-smoke-') as temporary:
        destination = Path(temporary)
        if archive.suffix == '.zip':
            with zipfile.ZipFile(archive) as source:
                source.extractall(destination)
        else:
            with tarfile.open(archive) as source:
                source.extractall(destination, filter='data')
        title = TITLES[product]
        base = destination / title
        binary = (base / (title + '.app') / 'Contents/MacOS' / title if host == 'macos'
                  else base / (title + ('.exe' if host == 'windows' else '')))
        if host != 'windows':
            binary.chmod(binary.stat().st_mode | 0o111)
        user = destination / 'empty-user-profile'
        environment = dict(os.environ, WIILLR_USER_DIR=str(user), GCN_AUDIO_MUTE='1',
                           GCNLLR_RENDER_HIDDEN='1', LNG_TEST_HIDDEN='1')
        resources = base / (title + '.app') / 'Contents/Resources' if host == 'macos' else base
        graphics = resources / ('gcnllr-native-gx.dll' if host == 'windows' else
                                'libgcnllr-native-gx.' + ('dylib' if host == 'macos' else 'so'))
        # Match the runtime's immediate/local dynamic loading and query its
        # static API table. This does not initialize SDL, a GPU or a window.
        probe = ('import ctypes as c,json,os,sys; '
                 'scope=os.add_dll_directory(os.path.dirname(sys.argv[1])) if os.name=="nt" else None; '
                 'library=c.CDLL(sys.argv[1],mode=getattr(os,"RTLD_NOW",0)); '
                 'api=library.gcnllr_native_gx_get_api; api.argtypes=[c.c_uint32]; api.restype=c.c_void_p; '
                 'assert api(12) and not api(0); print(json.dumps({"graphics_loaded":True,"abi":12,"gpu_initialized":False}))')
        loaded = subprocess.run([sys.executable, '-c', probe, str(graphics)],
                                cwd=destination, env=environment, capture_output=True, text=True)
        if loaded.returncode:
            raise RuntimeError('Packaged graphics service did not load: ' + loaded.stderr)
        process = subprocess.run([str(binary), '--launcher-check'], cwd=destination,
                                 env=environment, capture_output=True, text=True)
        if process.returncode != 2:
            raise RuntimeError(f'{product}: expected unconfigured-profile result 2, got {process.returncode}: {process.stderr}')
        if product == 'wii-menu':
            if process.stderr.strip() or not (user / 'data').is_dir() or not (user / 'cache').is_dir():
                raise RuntimeError('Wii Menu package resources did not resolve: ' + process.stderr)
        elif 'import the supported Mega Man 9 (USA) WAD first' not in process.stderr:
            raise RuntimeError('Mega Man 9 package resources did not resolve: ' + process.stderr)
        report = {'product': product, 'platform': host, 'returncode': process.returncode,
                  'stdout': process.stdout, 'stderr': process.stderr,
                  'profile': 'fresh disposable profile without original content',
                  'window_opened': False, 'guest_executed': False,
                  'graphics': json.loads(loaded.stdout)}
        (output / f'{product}-{host}-package-smoke.json').write_text(json.dumps(report, indent=2) + '\n')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs-dir', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--products', choices=('all', 'tools', 'games'), default='all')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--common-key', type=Path,
                        help='Explicit private 16-byte MM9 packaging input; omitted by default')
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
        prefix = f'WiiMenuRecomp-{host}-{arch}-v{args.version}'
        smoke_game(args.output_dir / (prefix + ('.tar.gz' if host == 'linux' else '.zip')),
                   'wii-menu', host, args.output_dir)
        extra: list[str] = []
        if args.common_key:
            key = args.common_key.resolve(strict=True)
            if key.stat().st_size != 16:
                raise ValueError('Private WAD packaging input must contain exactly 16 bytes')
            extra = ['--common-key', str(key)]
        package('mega-man-9', directory, *runtime, *extra)
        prefix = f'MegaMan9Recomp-{host}-{arch}-v{args.version}'
        smoke_game(args.output_dir / (prefix + ('.tar.gz' if host == 'linux' else '.zip')),
                   'mega-man-9', host, args.output_dir)

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
