# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic content: verification failures must leave copied NAND unchanged."""
import hashlib,importlib.util,json,struct,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import nand_profile
HAS_CRYPTO=importlib.util.find_spec('cryptography') is not None
if HAS_CRYPTO:
    from cryptography.hazmat.primitives.ciphers import Cipher,algorithms,modes
    from install_wad import install

@unittest.skipUnless(HAS_CRYPTO,'optional input-preparation cryptography dependency')
class WadInstallTests(unittest.TestCase):
    def fixture(self,root,index=0,corrupt=False):
        source=root/'source';(source/'sys').mkdir(parents=True)
        (source/'sys/uid.sys').write_bytes(struct.pack('>QI',0x100000002,4096))
        profile=root/'profile';nand_profile.prepare(source,profile,'0000000100000002')
        tid=0x1000154455354;plain=b'synthetic title';key=bytes(range(16));title_key=bytes(range(16,32))
        key_file=root/'synthetic-key.bin';key_file.write_bytes(key)
        def encrypt(key,iv,data):
            operation=Cipher(algorithms.AES(key),modes.CBC(iv)).encryptor()
            return operation.update(data)+operation.finalize()
        ticket=bytearray(0x2a4);struct.pack_into('>I',ticket,0,0x10001)
        ticket[0x1bf:0x1cf]=encrypt(key,tid.to_bytes(8,'big')+bytes(8),title_key)
        struct.pack_into('>Q',ticket,0x1dc,tid);ticket[0x1f1]=index
        tmd=bytearray(0x208);struct.pack_into('>I',tmd,0,0x10001)
        struct.pack_into('>QQ',tmd,0x184,0x100000025,tid)
        struct.pack_into('>HHH',tmd,0x1dc,1,1,0)
        struct.pack_into('>IHHQ20s',tmd,0x1e4,1,0,1,len(plain),hashlib.sha1(plain).digest())
        content=encrypt(title_key,bytes(16),plain+bytes((-len(plain))%16))
        if corrupt:content=bytes([content[0]^1])+content[1:]
        header=struct.pack('>8I',32,0x49730000,0,0,len(ticket),len(tmd),len(content),0)
        def align(data):return data+bytes((-len(data))%64)
        wad=root/'synthetic.wad';wad.write_bytes(align(header)+align(ticket)+align(tmd)+align(content))
        return source,profile,key_file,wad,bytes(ticket),plain
    def test_bad_content_has_no_install_side_effects(self):
        with tempfile.TemporaryDirectory() as directory:
            source,profile,key,wad,*_=self.fixture(Path(directory),corrupt=True)
            before=nand_profile.snapshot(profile);original=nand_profile.snapshot(source)
            with self.assertRaisesRegex(ValueError,'Content hash'):install(wad,key,profile)
            self.assertEqual(nand_profile.snapshot(profile),before)
            self.assertEqual(nand_profile.snapshot(source),original)
    def test_offline_override_preserves_ticket_and_native_save(self):
        with tempfile.TemporaryDirectory() as directory:
            source,profile,key,wad,ticket,plain=self.fixture(Path(directory),index=152)
            before=nand_profile.snapshot(profile);original=nand_profile.snapshot(source)
            with self.assertRaisesRegex(ValueError,'common-key ticket'):install(wad,key,profile)
            self.assertEqual(nand_profile.snapshot(profile),before)
            result=install(wad,key,profile,key_index_override=0)
            self.assertFalse(result['signatures_verified']);self.assertTrue(result['ticket_index_overridden'])
            self.assertFalse(result['native_ES_import_tested'])
            self.assertEqual((profile/'ticket/00010001/54455354.tik').read_bytes(),ticket)
            self.assertEqual((profile/'title/00010001/54455354/content/00000001.app').read_bytes(),plain)
            save=profile/'title/00010001/54455354/data/progress.bin';save.write_bytes(b'owner progress')
            install(wad,key,profile,key_index_override=0)
            self.assertEqual(save.read_bytes(),b'owner progress')
            self.assertEqual(nand_profile.snapshot(source),original)
if __name__=='__main__':unittest.main()
