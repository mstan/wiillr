# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('release_inputs', Path(__file__).parents[1] / 'tools/release_inputs.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ReleaseInputs(unittest.TestCase):
    def archive(self, root, names):
        archive = root / 'inputs.zip'
        with zipfile.ZipFile(archive, 'w') as target:
            for name, value in names:
                entry = zipfile.ZipInfo()
                entry.filename = name
                target.writestr(entry, value)
        return archive, hashlib.sha256(archive.read_bytes()).hexdigest()

    def test_extracts_exact_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive, sha = self.archive(root, [('inputs.json', '{}'), ('deps/a.txt', 'source')])
            module.unpack(archive, root / 'output', sha)
            self.assertEqual((root / 'output/deps/a.txt').read_text(), 'source')

    def test_wrong_checksum_creates_no_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive, _ = self.archive(root, [('inputs.json', '{}')])
            with self.assertRaisesRegex(ValueError, 'checksum'):
                module.unpack(archive, root / 'output', '0' * 64)
            self.assertFalse((root / 'output').exists())

    def test_rejects_escape_before_writing_any_member(self):
        for name in ('../escape', '/absolute', 'C:/absolute', '..\\escape'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                archive, sha = self.archive(root, [('inputs.json', '{}'), (name, 'bad')])
                with self.assertRaisesRegex(ValueError, 'Invalid'):
                    module.unpack(archive, root / 'output', sha)
                self.assertEqual(list((root / 'output').iterdir()), [])

    def test_preserves_nonempty_destination(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive, sha = self.archive(root, [('inputs.json', '{}')])
            destination = root / 'output'
            destination.mkdir()
            (destination / 'save').write_text('keep')
            with self.assertRaisesRegex(ValueError, 'empty'):
                module.unpack(archive, destination, sha)
            self.assertEqual((destination / 'save').read_text(), 'keep')


if __name__ == '__main__':
    unittest.main()
