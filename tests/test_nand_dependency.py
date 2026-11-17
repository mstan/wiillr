# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic IOS graph: merge identities while preserving player-owned files."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import nand_profile
from install_nand_dependency import install

TITLE = '0000000100000042'


class DependencyTests(unittest.TestCase):
    def fixture(self, root):
        donor, original, profile = root / 'donor', root / 'original', root / 'profile'
        for path in (donor / 'sys', donor / 'shared1', donor / 'ticket/00000001',
                     donor / 'title/00000001/00000042/content', original / 'sys', original / 'shared1'):
            path.mkdir(parents=True, exist_ok=True)
        (donor / 'sys/uid.sys').write_bytes(struct.pack('>QI', int(TITLE, 16), 9000))
        (original / 'sys/uid.sys').write_bytes(struct.pack('>QI', 0x100000002, 4096))
        (original / 'progress').write_bytes(b'current player progress')
        (original / 'settings').write_bytes(b'current player settings')
        (original / 'license').write_bytes(b'existing unrelated license')
        contents = [b'original boot data', b'original shared first', b'original shared reused']
        tmd = bytearray(0x1e4 + 3 * 36)
        struct.pack_into('>I', tmd, 0, 0x10001)
        struct.pack_into('>QQ', tmd, 0x184, 0, int(TITLE, 16))
        struct.pack_into('>HHH', tmd, 0x1dc, 7, 3, 0)
        for index, data in enumerate(contents):
            struct.pack_into('>IHHQ20s', tmd, 0x1e4 + index * 36,
                             index + 1, index, 1 if not index else 0x8001,
                             len(data), hashlib.sha1(data).digest())
        (donor / 'title/00000001/00000042/content/title.tmd').write_bytes(tmd)
        (donor / 'title/00000001/00000042/content/00000001.app').write_bytes(contents[0])
        ticket = bytearray(0x2a4)
        struct.pack_into('>I', ticket, 0, 0x10001)
        struct.pack_into('>Q', ticket, 0x1dc, int(TITLE, 16))
        (donor / 'ticket/00000001/00000042.tik').write_bytes(ticket)
        (donor / 'shared1/00000000.app').write_bytes(contents[1])
        (donor / 'shared1/00000002.app').write_bytes(contents[2])
        (donor / 'shared1/content.map').write_bytes(
            b'00000000' + hashlib.sha1(contents[1]).digest() +
            b'00000002' + hashlib.sha1(contents[2]).digest())
        unrelated = b'other installed shared data'
        (original / 'shared1/00000000.app').write_bytes(unrelated)
        (original / 'shared1/00000001.app').write_bytes(contents[2])
        (original / 'shared1/content.map').write_bytes(
            b'00000000' + hashlib.sha1(unrelated).digest() +
            b'00000001' + hashlib.sha1(contents[2]).digest())
        nand_profile.prepare(original, profile, '0000000100000002')
        return donor, profile, contents

    def test_hash_merge_uid_allocation_and_progress_preservation(self):
        with tempfile.TemporaryDirectory() as directory:
            donor, profile, contents = self.fixture(Path(directory))
            source_before, before = nand_profile.snapshot(donor), nand_profile.snapshot(profile)
            old_uid = (profile / 'sys/uid.sys').read_bytes()
            old_map = (profile / 'shared1/content.map').read_bytes()
            result = install(donor, profile, TITLE)
            self.assertEqual(result['uid'], 4097)  # Source UID9000 is not copied.
            self.assertEqual(result['shared_reused'], 1)
            self.assertEqual((profile / 'shared1/00000002.app').read_bytes(), contents[1])
            self.assertEqual((profile / 'sys/uid.sys').read_bytes(), old_uid + struct.pack('>QI', int(TITLE, 16), 4097))
            self.assertTrue((profile / 'shared1/content.map').read_bytes().startswith(old_map))
            after = nand_profile.snapshot(profile)
            for name, value in before.items():
                if name not in ('sys/uid.sys', 'shared1/content.map'):
                    self.assertEqual(after[name], value, name)
            self.assertEqual(nand_profile.snapshot(donor), source_before)
            self.assertEqual(install(donor, profile, TITLE)['changed'], {})

    def test_existing_title_uid_and_original_license_survive(self):
        with tempfile.TemporaryDirectory() as directory:
            donor, profile, _ = self.fixture(Path(directory))
            ticket = bytearray((donor / 'ticket/00000001/00000042.tik').read_bytes())
            ticket[0x1d0] = 9  # A different original ticket ID for this title.
            path = profile / 'ticket/00000001/00000042.tik'
            path.parent.mkdir(parents=True)
            path.write_bytes(ticket)
            with (profile / 'sys/uid.sys').open('ab') as stream:
                stream.write(struct.pack('>QI', int(TITLE, 16), 8192))
            result = install(donor, profile, TITLE)
            self.assertEqual(result['uid'], 8192)
            self.assertTrue(result['existing_license_preserved'])
            self.assertEqual(path.read_bytes(), ticket)

    def test_bad_source_graph_never_changes_destination(self):
        for damaged in ('payload', 'ticket', 'tmd', 'map'):
            with self.subTest(damaged=damaged), tempfile.TemporaryDirectory() as directory:
                donor, profile, _ = self.fixture(Path(directory))
                paths = {'payload': 'shared1/00000002.app', 'ticket': 'ticket/00000001/00000042.tik',
                         'tmd': 'title/00000001/00000042/content/title.tmd', 'map': 'shared1/content.map'}
                (donor / paths[damaged]).write_bytes(b'corrupt')
                before = nand_profile.snapshot(profile)
                errors = {'payload': 'content size/hash', 'ticket': 'ticket',
                          'tmd': 'metadata signature', 'map': 'shared content map'}
                with self.assertRaisesRegex(ValueError, errors[damaged]):
                    install(donor, profile, TITLE)
                self.assertEqual(nand_profile.snapshot(profile), before)

    def test_destination_conflicts_are_detected_before_any_write(self):
        for damaged in ('uid', 'map', 'payload', 'tmd'):
            with self.subTest(damaged=damaged), tempfile.TemporaryDirectory() as directory:
                donor, profile, _ = self.fixture(Path(directory))
                if damaged == 'uid':
                    path = profile / 'sys/uid.sys'
                    path.write_bytes(path.read_bytes() * 2)
                elif damaged == 'map':
                    path = profile / 'shared1/content.map'
                    path.write_bytes(path.read_bytes() * 2)
                elif damaged == 'payload':
                    (profile / 'shared1/00000001.app').write_bytes(b'wrong existing content')
                else:
                    path = profile / 'title/00000001/00000042/content/title.tmd'
                    path.parent.mkdir(parents=True)
                    path.write_bytes(b'previous installed metadata')
                before = nand_profile.snapshot(profile)
                errors = {'uid': 'Duplicate UID', 'map': 'Duplicate shared',
                          'payload': 'Conflicting installed', 'tmd': 'Conflicting installed'}
                with self.assertRaisesRegex(ValueError, errors[damaged]):
                    install(donor, profile, TITLE)
                self.assertEqual(nand_profile.snapshot(profile), before)

    def test_source_and_unmarked_destination_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            donor, profile, _ = self.fixture(Path(directory))
            with self.assertRaisesRegex(ValueError, 'separate roots'):
                install(donor, donor, TITLE)
            (profile / '.wiillr-nand-profile').write_text('not an owned profile')
            before = nand_profile.snapshot(profile)
            with self.assertRaisesRegex(ValueError, 'marked copied'):
                install(donor, profile, TITLE)
            self.assertEqual(nand_profile.snapshot(profile), before)


if __name__ == '__main__':
    unittest.main()
