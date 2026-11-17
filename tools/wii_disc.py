# SPDX-License-Identifier: GPL-3.0-or-later
"""Read an original encrypted Wii ISO without modifying it or publishing keys."""
from __future__ import annotations

import argparse
from functools import lru_cache
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct

SECTOR = 0x8000
PAYLOAD = 0x7C00


def u32(data: bytes, offset: int = 0) -> int:
    return struct.unpack_from('>I', data, offset)[0]


class Disc:
    def __init__(self, path: Path, common_key: bytes):
        if len(common_key) != 16:
            raise ValueError('Expected a 16-byte common key')
        self.stream = path.open('rb')
        self.size = path.stat().st_size
        try:
            self.header = self.raw(0, 0x100)
            if u32(self.header, 0x18) != 0x5D1C9EA3:
                raise ValueError('Expected a raw Wii ISO')
            self.partitions = []
            groups = self.raw(0x40000, 32)
            for group in range(4):
                count, address = struct.unpack_from('>II', groups, group * 8)
                if count > 256:
                    raise ValueError('Excessive partition count')
                table = self.raw(address * 4, count * 8)
                for index in range(count):
                    offset, kind = struct.unpack_from('>II', table, index * 8)
                    self.partitions.append(dict(offset=offset * 4, type=kind))
            games = [p for p in self.partitions if p['type'] == 0]
            if len(games) != 1:
                raise ValueError('Expected exactly one game partition')
            self.partition = games[0]['offset']
            header = self.raw(self.partition, 0x2C0)
            if header[0x1F1] != 0:
                raise ValueError('Unsupported ticket common-key index')
            self.title_id = header[0x1DC:0x1E4].hex()
            self.key = self.decrypt(common_key, header[0x1DC:0x1E4] + bytes(8), header[0x1BF:0x1CF])
            self.data_offset = self.partition + u32(header, 0x2B8) * 4
            encrypted_size = u32(header, 0x2BC) * 4
            if encrypted_size % SECTOR or self.data_offset + encrypted_size > self.size:
                raise ValueError('Truncated or unaligned game partition')
            self.data_size = encrypted_size // SECTOR * PAYLOAD
            tmd_size = u32(header, 0x2A4)
            if not 0x1E4 <= tmd_size <= 0x100000:
                raise ValueError('Invalid partition TMD size')
            self.tmd = self.raw(self.partition + u32(header, 0x2A8) * 4, tmd_size)
            from wii_inputs import title_metadata
            self.metadata = title_metadata(self.tmd)
            if self.metadata['title_id'] != self.title_id:
                raise ValueError('Ticket and TMD title IDs disagree')
            self.boot = self.read(0, 0x440)
            if self.boot[:6] != self.header[:6]:
                raise ValueError('Disc and decrypted partition IDs disagree')
        except BaseException:
            self.close()
            raise

    @staticmethod
    def decrypt(key: bytes, iv: bytes, data: bytes) -> bytes:
        from Crypto.Cipher import AES
        return AES.new(key, AES.MODE_CBC, iv).decrypt(data)

    def raw(self, offset: int, size: int) -> bytes:
        if offset < 0 or size < 0 or offset > self.size or size > self.size - offset:
            raise ValueError('Read outside original disc')
        self.stream.seek(offset)
        data = self.stream.read(size)
        if len(data) != size:
            raise ValueError('Truncated original disc')
        return data

    @lru_cache(maxsize=64)
    def sector(self, index: int) -> bytes:
        block = self.raw(self.data_offset + index * SECTOR, SECTOR)
        hashes = self.decrypt(self.key, bytes(16), block[:0x400])
        data = self.decrypt(self.key, block[0x3D0:0x3E0], block[0x400:])
        for number in range(31):
            if hashlib.sha1(data[number * 0x400:(number + 1) * 0x400]).digest() != hashes[number * 20:(number + 1) * 20]:
                raise ValueError(f'Partition H0 mismatch at sector {index}, block {number}')
        return data

    def read(self, offset: int, size: int) -> bytes:
        if offset < 0 or size < 0 or offset > self.data_size or size > self.data_size - offset:
            raise ValueError('Read outside decrypted partition')
        result = bytearray()
        while size:
            index, within = divmod(offset, PAYLOAD)
            amount = min(size, PAYLOAD - within)
            result += self.sector(index)[within:within + amount]
            offset += amount
            size -= amount
        return bytes(result)

    def files(self) -> list[dict]:
        offset, size = u32(self.boot, 0x424) * 4, u32(self.boot, 0x428) * 4
        if not 12 <= size <= 32 * 1024 * 1024:
            raise ValueError('Invalid FST size')
        fst = self.read(offset, size)
        count = u32(fst, 8)
        if fst[0] != 1 or count < 1 or count * 12 > size:
            raise ValueError('Invalid FST root')
        strings = fst[count * 12:]
        result, stack = [], [('', count, 0)]
        for index in range(1, count):
            while stack and index >= stack[-1][1]:
                stack.pop()
            word, start, length = struct.unpack_from('>III', fst, index * 12)
            name_offset = word & 0xFFFFFF
            end = strings.find(b'\0', name_offset)
            if not stack or end < 0:
                raise ValueError('Invalid FST entry')
            name = strings[name_offset:end].decode('utf-8')
            if not name or name in ('.', '..') or '/' in name or '\\' in name or ':' in name:
                raise ValueError('Unsafe FST name')
            path = stack[-1][0] + name
            if word >> 24 == 1:
                if not index < length <= stack[-1][1] or start != stack[-1][2]:
                    raise ValueError('Invalid FST directory')
                stack.append((path + '/', length, index))
            elif word >> 24 == 0:
                if start * 4 + length > self.data_size:
                    raise ValueError('FST file outside partition')
                result.append(dict(path=path, offset=start * 4, size=length))
            else:
                raise ValueError('Unknown FST entry type')
        if len({f['path'].casefold() for f in result}) != len(result):
            raise ValueError('Duplicate FST file paths')
        return result

    def dol(self, offset: int, size: int | None = None) -> bytes:
        header = self.read(offset, 0x100)
        extents = []
        for index in range(18):
            start, length = u32(header, index * 4), u32(header, 0x90 + index * 4)
            if length:
                if start < 0x100:
                    raise ValueError('DOL section overlaps header')
                extents.append(start + length)
        extent = max(extents, default=0)
        if not 0x100 <= extent <= 0x5800000 or (size is not None and extent > size):
            raise ValueError('Invalid DOL extent')
        return self.read(offset, extent if size is None else size)

    def inspect(self) -> dict:
        files = self.files()
        targets = [dict(path='sys/main.dol', offset=u32(self.boot, 0x420) * 4)]
        targets += [f for f in files if f['path'].lower().endswith('.dol')]
        for target in targets:
            data = self.dol(target['offset'], target.get('size'))
            target.update(size=len(data), sha1=hashlib.sha1(data).hexdigest(), sha256=hashlib.sha256(data).hexdigest(), entry=f'{u32(data, 0xE0):08x}')
        return dict(schema=1, disc_id=self.header[:6].decode('ascii'), revision=self.header[7],
                    disc_size=self.size, partitions=self.partitions, game_partition=self.partition,
                    data_offset=self.data_offset, data_size=self.data_size, title_id=self.title_id,
                    ios=self.metadata['ios'], group_id=self.metadata['group_id'], executables=targets,
                    files=files, integrity='H0 verified for accessed sectors; signatures/H1-H3 not verified')

    def close(self):
        self.stream.close()
        self.sector.cache_clear()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iso', type=Path)
    parser.add_argument('--common-key', type=Path, required=True)
    parser.add_argument('--extract', type=Path, help='New private directory for executable inputs and inspection.json')
    args = parser.parse_args()
    with Disc(args.iso, args.common_key.read_bytes()) as disc:
        report = disc.inspect()
        if args.extract:
            args.extract.mkdir(parents=True, exist_ok=False)
            for item in report['executables']:
                destination = args.extract.joinpath(*PurePosixPath(item['path']).parts)
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(disc.dol(item['offset'], item['size']))
            (args.extract / 'inspection.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({k: v for k, v in report.items() if k != 'files'}, indent=2))


if __name__ == '__main__':
    main()
