# SPDX-License-Identifier: MIT
"""Local development control for a running wiillr process; never edits guest flags."""
import argparse
import json
import socket
from pathlib import Path


class Control:
    def __init__(self, port):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=10)
        self.stream = self.socket.makefile('rb')

    def command(self, command):
        self.socket.sendall(command.encode('ascii') + b'\n')
        # display is bounded at 16 MiB RGBA, encoded as hex plus JSON metadata.
        line = self.stream.readline(32 * 1024 * 1024 + 4096)
        if not line.endswith(b'\n'):
            raise RuntimeError('Missing or oversized control response')
        result = json.loads(line)
        if not result.get('ok'):
            raise RuntimeError(result.get('error', 'Control command failed'))
        return result

    def close(self):
        self.stream.close()
        self.socket.close()


def frame_image(frame):
    from PIL import Image
    data = bytes.fromhex(frame['data'])
    width, height, stride = (frame[key] for key in ('width', 'height', 'stride'))
    if frame.get('format') == 'rgba8':
        return Image.frombytes('RGBA', (width, height), data, 'raw', 'RGBA', stride).convert('RGB')
    output = bytearray(width * height * 3)
    def clip(value): return max(0, min(255, value))
    for y in range(height):
        for x in range(0, width, 2):
            y0, u, y1, v = data[y * stride + x * 2:y * stride + x * 2 + 4]
            for dx, luma in ((0, y0), (1, y1)):
                c, d, e = luma - 16, u - 128, v - 128
                at = (y * width + x + dx) * 3
                output[at:at+3] = bytes((clip((298*c+409*e+128)>>8),
                    clip((298*c-100*d-208*e+128)>>8), clip((298*c+516*d+128)>>8)))
    return Image.frombytes('RGB', (width, height), bytes(output))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--frame-output', type=Path)
    parser.add_argument('command', nargs='+')
    args = parser.parse_args()
    control = Control(args.port)
    try:
        result = control.command(' '.join(args.command))
        if args.frame_output:
            if args.frame_output.exists(): raise FileExistsError(args.frame_output)
            frame_image(result).save(args.frame_output)
            result.pop('data')
        print(json.dumps(result))
    finally:
        control.close()
