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
import nand_profile


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
    starlet = args.processor == 'starlet'
    original_nand,uid=None,None
    if args.nand_profile:
        if starlet:raise ValueError('A native filesystem profile belongs to PPC execution')
        original_nand,uid=nand_profile.prepare(args.nand,args.nand_profile,args.title)
    content_root=args.nand_profile if args.nand_profile else args.nand
    content, tmd, metadata = boot_content(content_root.resolve(), args.title)
    ios_profile=4;ios_content=ios_tmd=None
    if metadata['ios']=='0000000100000004':
        ios_content,ios_tmd,ios=boot_content(args.nand.resolve(),metadata['ios'])
        if ios['version']!=3:raise ValueError('This boundary requires verified IOS4 v3 inputs')
    elif metadata['ios'] in ('0000000100000025','0000000100000050'):
        ios_profile,revision={'0000000100000025':(37,5663),'0000000100000050':(80,6944)}[metadata['ios']]
        if starlet:
            ios_content,ios_tmd,ios=boot_content(content_root.resolve(),metadata['ios'])
            if ios['version']!=revision:raise ValueError(f'This boundary requires verified IOS{ios_profile} v{revision} inputs')
        elif not getattr(args,f'native_ios{ios_profile}'):
            raise ValueError(f'Native IOS{ios_profile} development requires --native-ios{ios_profile}')
    else:raise ValueError('Unsupported requested IOS')
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
    command = ([str(executable), str(ios_content), str(args.steps)] if starlet else
               [str(executable), str(content), str(args.steps), str(work / 'mem1.bin'), hex(args.stop_pc)])
    if bool(args.dsp_rom) != bool(args.dsp_coef):
        raise ValueError('Supply both DSP ROM and coefficient files')
    if starlet and (args.dsp_rom or args.stop_pc):
        raise ValueError('DSP ROM/stop-PC options belong to PPC discovery')
    paths = dict(executable=executable, content=content, tmd=tmd)
    if env.get('WIILLR_INPUT_SCRIPT'):
        paths['input_script']=Path(env['WIILLR_INPUT_SCRIPT']).resolve(strict=True)
    if ios_content:paths.update(ios_content=ios_content,ios_tmd=ios_tmd)
    if args.dsp_rom:
        if args.dsp_rom.stat().st_size != 8192 or args.dsp_coef.stat().st_size != 4096:
            raise ValueError('Invalid DSP ROM/coefficient extent')
        paths.update(dsp_rom=args.dsp_rom, dsp_coef=args.dsp_coef)
        command += [str(args.dsp_rom.resolve()), str(args.dsp_coef.resolve())]
    if args.nand_profile:
        command += [str(args.nand_profile.absolute()), str(uid), str(metadata['group_id'])]
    if args.gx_library or args.gx_software:
        if starlet:
            raise ValueError('GX belongs to PPC execution')
        if args.gx_library and args.gx_software:
            raise ValueError('Choose either a native GX library or software GX')
        if args.gx_library:
            paths['gx_library'] = args.gx_library.resolve(strict=True)
        data, cache = work / 'gx-data', work / 'gx-cache'
        data.mkdir(); cache.mkdir()
        command += ['--gx', str(paths['gx_library']) if args.gx_library else 'software', str(data), str(cache)]
        env['WIILLR_WINDOW']='1' if args.interactive else '0'
        menu_title='Wii Startup Menu' if metadata['version']==1 else 'Wii Menu'
        env['WIILLR_WINDOW_TITLE']=args.window_title or (menu_title if args.title=='0000000100000002' else 'Wii')
        audio=args.audio_output or ('on' if args.interactive else 'off')
        env['GCN_AUDIO']='0' if audio=='off' else '1'
        env['GCN_AUDIO_MUTE']='1' if audio=='muted' else '0'
        if not args.interactive:env.update(SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0', SDL_AUDIODRIVER='dummy')
    if ios_profile!=4:command+=['--ios-profile',str(ios_profile)]
    before = {name: identity(path) for name,path in paths.items()}
    receipt = dict(inputs=before, title=metadata, command=command,
                   boot_profile=f'ios{ios_profile}-v{ios["version"]}-elf-kernel-entry' if starlet else (f'native-ios{ios_profile}-memory-contract-development' if ios_profile!=4 else 'native-ios4-v3-memory-contract'),
                   execution='ARM-discovery-interpreter' if starlet else {'static':'PPC-static-native','mixed-discovery':'PPC-native-with-discovery','discovery':'PPC-discovery-interpreter'}[args.ppc_mode], accepted=False,
                   boundary=('IOS ELF kernel entry; boot0/1/2 and PPC execution excluded' if starlet else
                             'NAND boot-content PPC entry; boot0/1/2 and IOS execution excluded'))
    if original_nand is not None:
        receipt['writable_nand_profile'] = str(args.nand_profile.absolute())
        receipt['original_nand_hashes'] = original_nand
    (work / 'run.json').write_text(json.dumps(receipt, indent=2) + '\n')
    with (work / 'host.log').open('xb') as log:
        result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                                timeout=args.timeout,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    after = {name: identity(path) for name,path in paths.items()}
    if original_nand is not None and nand_profile.snapshot(args.nand) != original_nand:
        raise ValueError('Original NAND inputs changed during execution')
    if before != after:
        raise ValueError('Discovery inputs changed during execution')
    receipt.update(exit_code=result.returncode, log=identity(work / 'host.log'))
    if (work / 'mem1.bin').exists():
        receipt['mem1'] = identity(work / 'mem1.bin')
    (work / 'run.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(dict(work_root=str(work),exit_code=result.returncode,log=str(work/'host.log'))))
    for line in (work/'host.log').read_text().splitlines():
        if line.startswith('{') or line.startswith('static PPC miss ') or line.startswith('[dsp-aot] summary '):
            print(line)
    return result.returncode


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--nand', type=Path, required=True)
    p.add_argument('--processor', choices=('ppc', 'starlet'), default='ppc')
    p.add_argument('--ppc-mode', choices=('discovery','static','mixed-discovery'), default='discovery',
                   help='Static executable uses a cycle budget and stops at its first native miss')
    p.add_argument('--title', default='0000000100000002')
    p.add_argument('--executable', type=Path, required=True)
    p.add_argument('--work-root', type=Path, required=True)
    p.add_argument('--steps', type=int, default=10000000)
    p.add_argument('--stop-pc', type=lambda v: int(v, 0), default=0)
    p.add_argument('--timeout', type=int, default=120)
    p.add_argument('--dsp-rom', type=Path)
    p.add_argument('--dsp-coef', type=Path)
    p.add_argument('--nand-profile', type=Path, help='Create or reuse an isolated writable NAND copy')
    p.add_argument('--gx-library', type=Path, help='Wii-selected shared native GX service library')
    p.add_argument('--gx-software', action='store_true', help='Use an executable built with WIILLR_GX=software (headless reference scanout)')
    p.add_argument('--interactive',action='store_true',help='Normal visible window and normal audio by default')
    p.add_argument('--window-title',help='Clean game/window caption')
    p.add_argument('--audio-output',choices=('off','muted','on'),help='Actual host output policy; unattended default is off')
    p.add_argument('--native-ios37',action='store_true',
                   help='Explicit native IOS37 development boundary; no original IOS37 ARM execution')
    p.add_argument('--native-ios80',action='store_true',
                   help='Explicit native IOS80 development boundary; no original IOS80 ARM execution')
    args = p.parse_args()
    maximum = 1000000000 if args.processor == 'starlet' else (500000000000 if args.ppc_mode != 'discovery' else 5000000000)
    if not 0 < args.steps <= maximum or args.timeout <= 0:
        p.error('Positive bounded steps/timeout required')
    try:
        return discover(args)
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        p.exit(2, f'{error}\n')


if __name__ == '__main__':
    raise SystemExit(main())
