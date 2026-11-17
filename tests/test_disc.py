# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent encrypted-sector fixtures; never require a retail disc or keys."""
from pathlib import Path
import hashlib
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from wii_disc import Disc, PAYLOAD
try:
    from Crypto.Cipher import AES
except ImportError:
    AES = None

NATIVE = None
if len(sys.argv) == 2 and not sys.argv[1].startswith('-'):
    NATIVE = sys.argv.pop()


def word(data, at, value):
    struct.pack_into('>I', data, at, value)


def fixture(path):
    common = bytes(range(16))
    key = bytes(range(16, 32))
    title = bytes.fromhex('0001000054455354')
    partition, base = 0x50000, 0x70000
    image = bytearray(base + 3 * 0x8000)
    image[:6] = b'TEST01'
    word(image, 0x18, 0x5D1C9EA3)
    word(image, 0x40000, 1)
    word(image, 0x40004, 0x40020 // 4)
    word(image, 0x40020, partition // 4)
    image[partition+0x1DC:partition+0x1E4] = title
    image[partition+0x1BF:partition+0x1CF] = AES.new(common, AES.MODE_CBC, title + bytes(8)).encrypt(key)
    word(image, partition + 0x2A4, 0x208)
    word(image, partition + 0x2A8, 0x2C0 // 4)
    word(image, partition + 0x2B8, 0x20000 // 4)
    word(image, partition + 0x2BC, 3 * 0x8000 // 4)
    tmd = partition + 0x2C0
    word(image, tmd, 0x10001)
    struct.pack_into('>QQ', image, tmd + 0x184, 0x100000023, int.from_bytes(title, 'big'))
    struct.pack_into('>HHH', image, tmd + 0x1DC, 1, 1, 0)
    payload = bytearray(i % 251 for i in range(3 * PAYLOAD))
    payload[:0x440] = bytes(0x440)
    payload[:6] = b'TEST01'
    word(payload, 0x420, 0x1000 // 4)
    word(payload, 0x424, 0x500 // 4)
    word(payload, 0x428, 40 // 4)
    payload[0x500:0x528] = bytes(40)
    struct.pack_into('>III', payload, 0x500, 0x01000000, 0, 2)
    struct.pack_into('>III', payload, 0x50C, 0, 0x1000 // 4, 0x120)
    payload[0x518:0x521] = b'game.dol\0'
    payload[0x1000:0x1120] = bytes(0x120)
    word(payload, 0x1000, 0x100)
    word(payload, 0x1048, 0x80004000)
    word(payload, 0x1090, 0x20)
    word(payload, 0x10E0, 0x80004000)
    for sector in range(3):
        data = payload[sector * PAYLOAD:(sector + 1) * PAYLOAD]
        hashes = bytearray(0x400)
        for block in range(31):
            hashes[block * 20:(block + 1) * 20] = hashlib.sha1(data[block * 1024:(block + 1) * 1024]).digest()
        encrypted_hashes = AES.new(key, AES.MODE_CBC, bytes(16)).encrypt(hashes)
        encrypted = AES.new(key, AES.MODE_CBC, encrypted_hashes[0x3D0:0x3E0]).encrypt(data)
        image[base + sector * 0x8000:base + (sector + 1) * 0x8000] = encrypted_hashes + encrypted
    path.write_bytes(image)
    return common, payload


@unittest.skipUnless(AES, 'Optional pycryptodome needed for original-disc inspection tests')
class DiscTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.iso = Path(self.temp.name) / 'fixture.iso'
        self.key, self.payload = fixture(self.iso)

    def test_cross_sector_and_executable_identity(self):
        with Disc(self.iso, self.key) as disc:
            self.assertEqual(disc.read(PAYLOAD - 16, 64), self.payload[PAYLOAD - 16:PAYLOAD + 48])
            report = disc.inspect()
            self.assertEqual(report['ios'], '0000000100000023')
            self.assertEqual(report['executables'][0]['sha256'], report['executables'][1]['sha256'])
            self.assertEqual(disc.read(disc.data_size, 0), b'')
            for offset, size in [(-1, 1), (0, -1), (disc.data_size, 1), (2**64, 1)]:
                with self.assertRaises(ValueError):
                    disc.read(offset, size)

    def test_corruption_and_wrong_key_rejected(self):
        with self.assertRaises(ValueError):
            Disc(self.iso, bytes(16))
        data = bytearray(self.iso.read_bytes())
        data[0x78400] ^= 1
        self.iso.write_bytes(data)
        with Disc(self.iso, self.key) as disc:
            with self.assertRaisesRegex(ValueError, 'H0 mismatch'):
                disc.read(PAYLOAD - 16, 64)

    def test_unsafe_fst_rejected(self):
        with Disc(self.iso, self.key) as disc:
            original = disc.read
            data = bytearray(original(0x500, 40))
            disc.read = lambda offset, size: bytes(data) if offset == 0x500 else original(offset, size)
            for name in (b'../x.dol', b'CON.dol', b'evil.dol '):
                data[24:40] = (name + b'\0').ljust(16, b'\0')
                with self.assertRaisesRegex(ValueError, 'Unsafe'):
                    disc.files()

    @unittest.skipUnless(NATIVE, 'Run with the native fixture executable to compare both readers')
    def test_native_reader(self):
        key = self.iso.with_suffix('.key')
        key.write_bytes(self.key)
        subprocess.run([NATIVE, str(self.iso), str(key), 'valid'], check=True)
        data = bytearray(self.iso.read_bytes())
        data[0x78400] ^= 1
        self.iso.write_bytes(data)
        subprocess.run([NATIVE, str(self.iso), str(key), 'corrupt'], check=True)


if __name__ == '__main__':
    unittest.main()
