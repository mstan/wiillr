# SPDX-License-Identifier: MIT
"""ROM-free whole-text page preparation, including shared initialized data."""
import importlib.util,struct,tempfile,unittest,zlib
from pathlib import Path
spec=importlib.util.spec_from_file_location('prepare_ppc',Path(__file__).resolve().parents[1]/'tools/prepare_ppc.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class DolPages(unittest.TestCase):
    def image(self):
        data=bytearray(0x140)
        for index,offset,address,size in ((0,0x100,0x80001004,8),(1,0x108,0x90002000,4),(7,0x10c,0x8000100c,4)):
            for at,value in ((index*4,offset),(0x48+index*4,address),(0x90+index*4,size)):struct.pack_into('>I',data,at,value)
        struct.pack_into('>I',data,0xe0,0x80001004);data[0x100:0x110]=bytes.fromhex('600000004e8000204e80002012345678');return data
    def prepare(self,data):
        with tempfile.TemporaryDirectory() as root:
            path=Path(root)/'original.dol';path.write_bytes(data);return module.dol_text_pages(path)
    def test_all_text_and_shared_data(self):
        pages,record=self.prepare(self.image());self.assertEqual(record['pages'],2)
        main=next(value[0] for name,value in pages.items() if name.startswith('80001000'))
        self.assertEqual(main[4:16],bytes.fromhex('600000004e80002012345678'));self.assertEqual(main[:4],bytes(4))
        self.assertTrue(any(name.startswith('90002000') for name in pages))
        for name,(page,_) in pages.items():self.assertEqual(name[9:17],f'{zlib.crc32(page):08x}')
    def test_reject_bad_ranges_and_alias_overlap(self):
        for at,value in ((0,0xf0),(0x90,0x1000),(0x48,0xcc000000),(0x48,0x40001000),(0xe0,0x80002000),(0x48+7*4,0xc0001004)):
            image=self.image();struct.pack_into('>I',image,at,value)
            with self.assertRaises(ValueError):self.prepare(image)
    def test_text_changes_make_distinct_content_identity(self):
        a,_=self.prepare(self.image());image=self.image();image[0x103]^=1;b,_=self.prepare(image)
        self.assertNotEqual(set(a),set(b))
    def test_physical_entry_into_cached_text(self):
        image=self.image();struct.pack_into('>I',image,0xe0,0x1004)
        pages,record=self.prepare(image);self.assertEqual(record['entry'],0x1004);self.assertEqual(len(pages),2)

if __name__=='__main__':unittest.main()
