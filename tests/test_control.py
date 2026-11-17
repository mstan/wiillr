# SPDX-License-Identifier: GPL-3.0-or-later
"""ROM-free live control: pause/budget, RAM bounds and reconnect semantics."""
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time


def main():
    executable = Path(sys.argv[1])
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix='wiillr-control-') as directory:
        root = Path(directory)
        image = bytearray(0x104)
        for offset, value in ((0, 0x100), (0x48, 0x80004000), (0x90, 4),
                              (0xe0, 0x80004000), (0x100, 0x48000000)):
            struct.pack_into('>I', image, offset, value)
        (root / 'loop.dol').write_bytes(image)
        env = dict(os.environ, WIILLR_CONTROL_PORT=str(port), WIILLR_CONTROL_PAUSED='1')
        env.pop('WIILLR_INPUT_SCRIPT', None)
        env.pop('WIILLR_SYNC_CYCLES', None)
        process = subprocess.Popen([str(executable), str(root/'loop.dol'), '1000000',
                                    str(root/'mem1.bin'), '0'], env=env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        try:
            deadline = time.monotonic() + 10
            while True:
                try:
                    connection = socket.create_connection(('127.0.0.1', port), timeout=2)
                    break
                except OSError:
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError('Control process did not start')
                    time.sleep(.01)
            with connection:
                stream = connection.makefile('rb')
                def command(text):
                    connection.sendall(text.encode('ascii') + b'\n')
                    return json.loads(stream.readline())
                initial = command('status')
                assert initial['ok'] and initial['paused'] and initial['cycles'] == 0
                assert command('clock 1794355200')['ok']
                original = command('fingerprint')
                assert original['ok'] and original['mem2_bytes'] == 64 * 1024 * 1024
                assert command('video') == {'ok':True,'vidim':0}
                assert not command('video 0 0')['ok']  # no renderer/scanout attached
                assert not command('video 65536 65536')['ok']
                assert not command('settings')['ok']  # no host UI attached
                assert not command('raster')['ok']  # no native raster attached
                assert command('fingerprint') == original
                assert command('write 80005000 12345678')['ok']
                changed = command('fingerprint')
                assert changed['mem1_fnv1a64'] != original['mem1_fnv1a64']
                assert changed['mem2_fnv1a64'] == original['mem2_fnv1a64']
                assert command('read 80005000 4')['data'] == '12345678'
                assert not command('write 80005000 00zz')['ok']
                assert command('read 80005000 4')['data'] == '12345678'
                assert not command('read cd000000 4')['ok']
                assert not command('input 10000')['ok']
                assert command('run 1000')['ok']
                for _ in range(100):
                    state = command('status')
                    if state['paused']: break
                    time.sleep(.001)
                assert state['paused'] and 1000 <= state['cycles'] < 1064
                assert command('status')['cycles'] == state['cycles']
                assert not command('clock 1794355200')['ok']
                assert not command('until ' + str(state['cycles']))['ok']
                assert command('until 2000')['ok']
                for _ in range(100):
                    state = command('status')
                    if state['paused']: break
                    time.sleep(.001)
                assert state['paused'] and state['cycles'] == 2000
                stream.close()
            with socket.create_connection(('127.0.0.1', port), timeout=2) as reconnect:
                reconnect.sendall(b'sta')
                time.sleep(.01)
                reconnect.sendall(b'tus\n')
                with reconnect.makefile('rb') as stream:
                    assert json.loads(stream.readline())['paused']
                    reconnect.sendall(b'quit\n')
                    assert json.loads(stream.readline())['ok']
            assert process.wait(timeout=5) == 0
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)


if __name__ == '__main__':
    main()
