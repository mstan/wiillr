# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare two native Wii builds at matching guest cycles using a retained launch."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import time

from nand_profile import snapshot
from wii_control import Control


def identity(path):
    path = Path(path).resolve(strict=True)
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return dict(path=str(path), sha256=digest.hexdigest())


def wait_paused(control, process, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        state = control.command('status')
        if state['halted'] or process.poll() is not None:
            raise RuntimeError(f'Native execution stopped: {state}')
        if state['paused']:
            return state
        time.sleep(.02)
    raise TimeoutError('Guest checkpoint did not arrive')


def capture(args, receipt, executable, root, checkpoints, *, choose_boundaries=False,
            after_checkpoint=None, after_checkpoints=None, extra_cycles=0):
    root.mkdir()
    original_profile = Path(receipt['writable_nand_profile']).resolve(strict=True)
    profile = root / 'nand'
    before = snapshot(original_profile)
    shutil.copytree(original_profile, profile)
    if snapshot(profile) != before:
        raise ValueError('Copied NAND differs from the fixture')

    command = []
    for value in receipt['command']:
        path = Path(value)
        if path.is_absolute() and path.is_relative_to(original_profile):
            value = str(profile / path.relative_to(original_profile))
        command.append(value)
    command[0] = str(executable.resolve(strict=True))
    command[2] = str(checkpoints[-1] + extra_cycles + 1000000)
    command[3] = str(root / 'mem1.bin')
    command[4] = '0'
    at = command.index('--gx')
    selected_library = args.baseline_gx if root.name == 'baseline' else args.candidate_gx
    library = selected_library or args.gx_library or command[at + 1]
    software = str(library) == 'software'
    command[at + 1:at + 4] = ['software' if software else str(Path(library).resolve(strict=True)),
                                     str(root / 'gx-data'), str(root / 'gx-cache')]
    (root / 'gx-data').mkdir()
    (root / 'gx-cache').mkdir()
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    env = dict(os.environ, WIILLR_CONTROL_PORT=str(port), WIILLR_CONTROL_PAUSED='1',
               WIILLR_INPUT_SCRIPT=str(args.inputs.resolve(strict=True)),
               WIILLR_WINDOW='0',
               GCN_AUDIO='1' if getattr(args, 'host_audio_output', True) else '0',
               GCN_AUDIO_MUTE='1',
               GCN_AUDIO_CAPTURE=str(root / 'audio.wav'), GCN_AUDIO_CAPTURE_FRAMES='19200000',
               SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0', PYTHONDONTWRITEBYTECODE='1')
    env.pop('WIILLR_SYNC_CYCLES', None)
    env.pop('GCN_INTERP_JOURNAL', None)
    env.pop('GCN_INTERP_BLOB_DIR', None)
    if os.name == 'nt':
        env['PATH'] = 'C:/msys64/mingw64/bin' + os.pathsep + env.get('PATH', '')
    result = dict(executable=identity(executable),
                  gx_library=dict(provider='software') if software else identity(library),
                  inputs=identity(args.inputs), epoch=args.epoch, command=command,
                  host_audio_output=env['GCN_AUDIO'] == '1', checkpoints=[])
    if getattr(args, 'launcher', False):
        command.insert(1, '--launcher')
    process = None
    control = None
    try:
        with (root / 'host.log').open('xb') as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=env,
                                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            deadline = time.monotonic() + 45
            while control is None:
                try:
                    control = Control(port)
                except OSError:
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError('Native control did not start')
                    time.sleep(.05)
            control.command(f'clock {args.epoch}')
            for target in checkpoints:
                control.command(f'until {target}')
                state = wait_paused(control, process, args.timeout)
                if choose_boundaries and state['cycles'] < target:
                    raise ValueError(f'Baseline stopped before cycle {target}')
                if not choose_boundaries and state['cycles'] != target:
                    raise ValueError(f'Checkpoint {target} stopped at {state["cycles"]}')
                point = control.command('fingerprint')
                if any(point[key] for key in ('ppc_interpreter', 'ppc_misses',
                                               'dsp_interpreter', 'dsp_misses')):
                    raise ValueError('Oracle requires native execution with zero misses')
                result['checkpoints'].append(point)
                if after_checkpoint is not None:
                    after_checkpoint(control, process, target, point)
                if args.candidate_cost and root.name == 'candidate':
                    costs = control.command('cost 120')['cost']
                    (root / f'cost-{point["cycles"]}.json').write_text(json.dumps(costs, indent=2) + '\n')
                print(f'{root.name}: cycle {point["cycles"]}, fields {point["fields"]}', flush=True)
            if after_checkpoints is not None:
                after_checkpoints(control, process)
            if process.poll() is None:
                control.command('quit')
            elif not getattr(args, 'allow_clean_exit', False):
                raise RuntimeError('Native process stopped before capture exit')
            if process.wait(timeout=20):
                raise RuntimeError('Native process exited unsuccessfully')
    finally:
        if process is not None and process.poll() is None:
            if control is not None:
                try:
                    control.command('quit')
                except (OSError, RuntimeError):
                    pass
            # The runtime closes control before printing shutdown diagnostics.
            # A terminal guest failure can therefore reset this connection
            # while the process is still saving RAM and draining its devices.
            try:
                process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=10)
        if control is not None:
            control.close()
        result['fixture_unchanged'] = snapshot(original_profile) == before
        (root / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    if not result['fixture_unchanged']:
        raise ValueError('Original NAND fixture changed')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--launch', type=Path, required=True, help='Existing wii.py run.json')
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--gx-library', type=Path,
                        help="Override the GX library path, or 'software' for the build-selected CPU reference")
    parser.add_argument('--baseline-gx', type=Path)
    parser.add_argument('--candidate-gx', type=Path)
    parser.add_argument('--candidate-cost', action='store_true')
    parser.add_argument('--no-host-audio', dest='host_audio_output', action='store_false',
                        help='Disable host playback; retain DSP execution, PCM capture and fingerprints')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cycles', type=int, nargs='+', required=True)
    parser.add_argument('--epoch', type=int, default=1794355200)
    parser.add_argument('--timeout', type=int, default=600)
    args = parser.parse_args()
    if args.cycles != sorted(set(args.cycles)) or not 0 < args.cycles[0]:
        parser.error('Checkpoints must be positive, increasing and unique')
    if any(b - a > 50000000000 for a, b in zip([0] + args.cycles, args.cycles)):
        parser.error('Checkpoint gaps exceed the control budget')
    if not 0 <= args.epoch <= 0xffffffff:
        parser.error('Clock seed is outside the supported Unix epoch range')
    receipt = json.loads(args.launch.read_text())
    for name, row in receipt['inputs'].items():
        if name == 'executable' or (name == 'gx_library' and
                                    (args.gx_library or args.baseline_gx or args.candidate_gx)):
            continue
        if identity(row['path'])['sha256'] != row['sha256']:
            raise ValueError(f'Launch input changed: {row["path"]}')
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    baseline = capture(args, receipt, args.baseline, args.out / 'baseline', args.cycles,
                       choose_boundaries=True)
    boundaries = [point['cycles'] for point in baseline['checkpoints']]
    candidate = capture(args, receipt, args.candidate, args.out / 'candidate', boundaries)
    differences = []
    for index, (left, right) in enumerate(zip(baseline['checkpoints'], candidate['checkpoints'])):
        for key in sorted(left.keys() | right.keys()):
            if left.get(key) != right.get(key):
                differences.append(dict(cycle=boundaries[index], field=key,
                                        baseline=left.get(key), candidate=right.get(key)))
    result = dict(equal=not differences, differences=differences)
    (args.out / 'comparison.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return int(bool(differences))


if __name__ == '__main__':
    raise SystemExit(main())
