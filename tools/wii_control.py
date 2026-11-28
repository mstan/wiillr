# SPDX-License-Identifier: GPL-3.0-or-later
"""Local development control for a running wiillr process; never edits guest flags."""
import argparse
import json
import sys
sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parent))
import maintainer_tools
from debug_transport import JsonLineClient
from pathlib import Path


def validate_reply(result, command):
    if not isinstance(result, dict):
        raise RuntimeError('Invalid control response')
    if not result.get('ok'):
        raise RuntimeError(result.get('error', 'Control command failed'))
    return result


class Control(JsonLineClient):
    def __init__(self, port):
        # Display is bounded at 16 MiB RGBA, hex encoded plus JSON metadata.
        super().__init__(port, encode=lambda value: value.encode('ascii'),
                         validate=validate_reply, max_response=32 * 1024 * 1024 + 4096,
                         connect_timeout=10, read_timeout=10)


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
