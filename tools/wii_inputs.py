# SPDX-License-Identifier: GPL-3.0-or-later
"""Read title metadata without decrypting content or modifying a NAND."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct


def title_metadata(data: bytes) -> dict:
    if len(data) < 4:
        raise ValueError("Truncated signed metadata")
    signature = struct.unpack_from(">I", data)[0]
    try:
        base = {0x10000: 0x240, 0x10001: 0x140, 0x10002: 0x80}[signature]
    except KeyError:
        raise ValueError("Unsupported metadata signature type") from None
    if len(data) < base + 0xA4:
        raise ValueError("Truncated title header")
    ios, title = struct.unpack_from(">QQ", data, base + 0x44)
    version, count, boot = struct.unpack_from(">HHH", data, base + 0x9C)
    if len(data) < base + 0xA4 + count * 36:
        raise ValueError("Truncated content table")
    contents = []
    for index in range(count):
        cid, slot, flags, size, digest = struct.unpack_from(">IHHQ20s", data, base + 0xA4 + index * 36)
        contents.append(dict(id=f"{cid:08x}", index=slot, flags=flags,
                             size=size, sha1=digest.hex()))
    if len({item['id'] for item in contents}) != count or len({item['index'] for item in contents}) != count:
        raise ValueError("Duplicate content ID or index")
    # DLC titles contain data rather than a boot executable. Their unused
    # boot index need not refer to an installed content record.
    if count and title >> 32 != 0x00010005 and boot not in {item['index'] for item in contents}:
        raise ValueError("Boot index is absent from content table")
    return dict(title_id=f"{title:016x}", ios=f"{ios:016x}", version=version,
                group_id=struct.unpack_from(">H", data, base + 0x58)[0],
                region=struct.unpack_from(">H", data, base + 0x5C)[0],
                boot_index=boot, contents=contents, signature_verified=False)


def wad_metadata(path: Path) -> dict:
    def aligned(size):
        return (size + 63) & ~63
    with path.open('rb') as stream:
        header = stream.read(32)
        if len(header) != 32:
            raise ValueError("Truncated WAD header")
        size, kind, cert, reserved, ticket, tmd, content, footer = struct.unpack('>8I', header)
        if size != 32 or kind >> 16 not in (0x4973, 0x6962) or reserved:
            raise ValueError("Unsupported installable WAD header")
        offset = aligned(size)
        sections = {}
        length = path.stat().st_size
        for name, amount in [('certificates', cert), ('ticket', ticket), ('tmd', tmd),
                             ('content', content), ('footer', footer)]:
            if offset > length or amount > length - offset:
                raise ValueError(f"Truncated WAD {name} section")
            sections[name] = dict(offset=offset, size=amount)
            offset += aligned(amount)
        stream.seek(sections['tmd']['offset'])
        result = title_metadata(stream.read(tmd))
        result['sections'] = sections
        result['content_verified'] = False
        return result


def nand_metadata(root: Path) -> dict:
    titles = []
    shared_map = {}
    mapping = root / 'shared1/content.map'
    if mapping.exists():
        raw = mapping.read_bytes()
        if len(raw) % 28:
            raise ValueError("Truncated shared content map")
        for offset in range(0, len(raw), 28):
            name = raw[offset:offset + 8].decode('ascii')
            if len(name) != 8 or any(c not in '0123456789abcdefABCDEF' for c in name):
                raise ValueError("Invalid shared content filename")
            shared_map[raw[offset + 8:offset + 28].hex()] = name
    for path in sorted(root.glob('title/*/*/content/title.tmd')):
        item = title_metadata(path.read_bytes())
        expected = path.parents[2].name + path.parents[1].name
        if item['title_id'] != expected:
            raise ValueError("NAND directory and title identity disagree")
        for content in item['contents']:
            if content['flags'] & 0x8000:
                name = shared_map.get(content['sha1'])
                target = root / 'shared1' / f'{name}.app' if name else None
            else:
                target = path.parent / (content['id'] + '.app')
            content['verified'] = bool(target and target.is_file()
                and target.stat().st_size == content['size']
                and hashlib.sha1(target.read_bytes()).hexdigest() == content['sha1'])
        item['content_verified'] = all(c['verified'] for c in item['contents'])
        titles.append(item)
    return dict(titles=titles, signatures_verified=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    for command in ('wad', 'nand'):
        sub.add_parser(command).add_argument('path', type=Path)
    args = parser.parse_args()
    try:
        result = wad_metadata(args.path) if args.command == 'wad' else nand_metadata(args.path)
    except (ValueError, OSError) as error:
        parser.exit(1, f'{error}\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
