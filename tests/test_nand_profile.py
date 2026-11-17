# SPDX-License-Identifier: GPL-3.0-or-later
import sys
from pathlib import Path
import struct
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parents[1] / 'tools'))
import nand_profile


class NandProfileTests(unittest.TestCase):
    def test_copy_restart_and_original_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, profile = root / 'original', root / 'profile'
            (source / 'sys').mkdir(parents=True)
            (source / 'sys/uid.sys').write_bytes(struct.pack('>QI', 0x100000002, 0x1000))
            (source / 'save').write_bytes(b'original progress')
            before, uid = nand_profile.prepare(source, profile, '0000000100000002')
            self.assertEqual(uid, 0x1000)
            (profile / 'save').write_bytes(b'new progress')
            self.assertEqual(nand_profile.prepare(source, profile, '0000000100000002'), (before, uid))
            self.assertEqual((profile / 'save').read_bytes(), b'new progress')
            self.assertEqual((source / 'save').read_bytes(), b'original progress')
            (source / 'save').write_bytes(b'changed input')
            with self.assertRaisesRegex(ValueError, 'different original'):
                nand_profile.prepare(source, profile, '0000000100000002')

    def test_source_cannot_be_the_writable_profile(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary)
            for target in (source, source / 'nested'):
                with self.assertRaisesRegex(ValueError, 'separate roots'):
                    nand_profile.prepare(source, target, '0000000100000002')
