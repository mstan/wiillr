import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('wii_inputs', Path(__file__).parents[1] / 'tools/wii_inputs.py')
inputs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inputs)


def tmd():
    data = bytearray(0x208)
    struct.pack_into('>I', data, 0, 0x10001)
    struct.pack_into('>QQ', data, 0x184, 0x100000004, 0x100000002)
    struct.pack_into('>HHH', data, 0x1DC, 1, 1, 0)
    struct.pack_into('>IHHQ20s', data, 0x1E4, 0, 0, 1, 4, bytes(20))
    return data


class MetadataTests(unittest.TestCase):
    def test_title_and_ios_are_read_from_metadata(self):
        result = inputs.title_metadata(tmd())
        self.assertEqual(result['title_id'], '0000000100000002')
        self.assertEqual(result['ios'], '0000000100000004')
        self.assertFalse(result['signature_verified'])

    def test_all_truncations_are_rejected(self):
        data = tmd()
        for length in range(len(data)):
            with self.assertRaises(ValueError):
                inputs.title_metadata(data[:length])

    def test_missing_boot_content_is_rejected(self):
        data = tmd()
        struct.pack_into('>H', data, 0x1E0, 9)
        with self.assertRaisesRegex(ValueError, 'Boot index'):
            inputs.title_metadata(data)

    def test_wad_offsets_and_truncation(self):
        data = bytearray(0x280)
        struct.pack_into('>8I', data, 0, 32, 0x49730000, 0, 0, 0, len(tmd()), 0, 0)
        data[0x40:0x248] = tmd()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'synthetic.wad'
            path.write_bytes(data)
            self.assertEqual(inputs.wad_metadata(path)['version'], 1)
            path.write_bytes(data[:0x247])
            with self.assertRaisesRegex(ValueError, 'Truncated'):
                inputs.wad_metadata(path)


if __name__ == '__main__':
    unittest.main()
