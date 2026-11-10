# SPDX-License-Identifier: MIT
"""Run bounded original NAND-title discovery with explicit fallback accounting."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from wii_inputs import title_metadata


def identity(path):
    return dict(path=str(path.resolve()), sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def boot_content(nand, title):
    if len(title) != 16 or any(c not in '0123456789abcdef' for c in title):
        raise ValueError('Use a lowercase 16-digit title ID')
    directory = nand / 'title' / title[:8] / title[8:] / 'content'
    tmd = directory / 'title.tmd'
    metadata = title_metadata(tmd.read_bytes())
    if metadata['title_id'] != title:
        raise ValueError('NAND directory/title identity mismatch')
    boot = next((c for c in metadata['contents'] if c['index'] == metadata['boot_index']), None)
    if not boot or boot['flags'] & 0x8000:
        raise ValueError('Discovery requires a non-shared boot content')
    content = directory / (boot['id'] + '.app')
    raw = content.read_bytes()
    if len(raw) != boot['size'] or hashlib.sha1(raw).hexdigest() != boot['sha1']:
        raise ValueError('Boot content size/hash differs from TMD')
    return content, tmd, metadata


def discover(args):
    content, tmd, metadata = boot_content(args.nand.resolve(), args.title)
    if metadata['ios'] != '0000000100000004':
        raise ValueError('This discovery boundary implements IOS4 v3 memory only')
    ios_content, ios_tmd, ios = boot_content(args.nand.resolve(), metadata['ios'])
    if ios['version'] != 3:
        raise ValueError('This discovery boundary requires verified IOS4 v3 inputs')
    executable = args.executable.resolve(strict=True)
    work = args.work_root.resolve()
    # One invocation owns a fresh directory; previous evidence is never replaced.
    work.mkdir(parents=True, exist_ok=False)
    (work / 'pages').mkdir()
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1',
               GCN_INTERP_JOURNAL=str(work / 'native-misses.jsonl'),
               GCN_INTERP_BLOB_DIR=str(work / 'pages'))
    if os.name == 'nt':
        env['PATH'] = r'C:\msys64\mingw64\bin' + os.pathsep + env.get('PATH', '')
    command = [str(executable), str(content), str(args.steps), str(work / 'mem1.bin'), hex(args.stop_pc)]
    if bool(args.dsp_rom) != bool(args.dsp_coef):
        raise ValueError('Supply both DSP ROM and coefficient files')
    paths = dict(executable=executable, content=content, tmd=tmd, ios_content=ios_content, ios_tmd=ios_tmd)
    if args.dsp_rom:
        if args.dsp_rom.stat().st_size != 8192 or args.dsp_coef.stat().st_size != 4096:
            raise ValueError('Invalid DSP ROM/coefficient extent')
        paths.update(dsp_rom=args.dsp_rom, dsp_coef=args.dsp_coef)
        command += [str(args.dsp_rom.resolve()), str(args.dsp_coef.resolve())]
    before = {name: identity(path) for name,path in paths.items()}
    receipt = dict(inputs=before, title=metadata, command=command, boot_profile='native-ios4-v3-memory-contract',
                   execution='discovery-interpreter', accepted=False,
                   boundary='NAND boot-content PPC entry; boot0/1/2 and IOS execution excluded')
    (work / 'run.json').write_text(json.dumps(receipt, indent=2) + '\n')
    with (work / 'host.log').open('xb') as log:
        result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                                timeout=args.timeout,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    after = {name: identity(path) for name,path in paths.items()}
    if before != after:
        raise ValueError('Discovery inputs changed during execution')
    receipt.update(exit_code=result.returncode, log=identity(work / 'host.log'))
    if (work / 'mem1.bin').exists():
        receipt['mem1'] = identity(work / 'mem1.bin')
    (work / 'run.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print((work / 'host.log').read_text(), end='')
    return result.returncode


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--nand', type=Path, required=True)
    p.add_argument('--title', default='0000000100000002')
    p.add_argument('--executable', type=Path, required=True)
    p.add_argument('--work-root', type=Path, required=True)
    p.add_argument('--steps', type=int, default=10000000)
    p.add_argument('--stop-pc', type=lambda v: int(v, 0), default=0)
    p.add_argument('--timeout', type=int, default=120)
    p.add_argument('--dsp-rom', type=Path)
    p.add_argument('--dsp-coef', type=Path)
    args = p.parse_args()
    if not 0 < args.steps <= 1000000000 or args.timeout <= 0:
        p.error('Positive bounded steps/timeout required')
    try:
        return discover(args)
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        p.exit(2, f'{error}\n')


if __name__ == '__main__':
    raise SystemExit(main())
