# SPDX-License-Identifier: MIT
"""Verify/decrypt a local WAD into an owned NAND copy, preserving existing titles."""
import argparse,hashlib,json,os,struct
from pathlib import Path
from cryptography.hazmat.primitives.ciphers import Cipher,algorithms,modes
import nand_profile
from wii_inputs import wad_metadata

def decrypt(key,iv,data):
    operation=Cipher(algorithms.AES(key),modes.CBC(iv)).decryptor()
    return operation.update(data)+operation.finalize()
def signature_base(raw):
    if len(raw)<4:raise ValueError('Truncated signed ticket')
    try:return {0x10000:0x240,0x10001:0x140,0x10002:0x80}[int.from_bytes(raw[:4],'big')]
    except KeyError:raise ValueError('Unsupported ticket signature') from None
def install(wad,key_file,profile,*,key_index_override=None):
    profile=profile.resolve(strict=True);nand_profile.check_chain(profile)
    if (profile/'.wiillr-nand-profile').read_text()!=nand_profile.MAGIC+'\n':
        raise ValueError('Install only into a marked copied NAND profile')
    before=nand_profile.snapshot(profile)
    origin=json.loads((profile/'.wiillr-nand-source.json').read_text())
    source=Path(origin['source']).resolve()
    source_before=nand_profile.snapshot(source)
    if source==profile or source in profile.parents or profile in source.parents or source_before!=origin['files']:
        raise ValueError('Original NAND/profile provenance differs')
    info=wad_metadata(wad);raw=wad.read_bytes();sections=info['sections']
    section=lambda name:raw[sections[name]['offset']:sections[name]['offset']+sections[name]['size']]
    ticket=section('ticket');base=signature_base(ticket)
    if len(ticket)<base+0xb2:raise ValueError('Truncated ticket body')
    tid=int(info['title_id'],16)
    if int.from_bytes(ticket[base+0x9c:base+0xa4],'big')!=tid:
        raise ValueError('Ticket and TMD title identities differ')
    ticket_index=ticket[base+0xb1]
    index=ticket_index if key_index_override is None else key_index_override
    if index!=0:raise ValueError('This preparation needs a standard common-key ticket or an explicit offline decryption override')
    common=key_file.read_bytes()
    if len(common)!=16:raise ValueError('Common-key input must contain exactly16 binary bytes')
    title_key=decrypt(common,tid.to_bytes(8,'big')+bytes(8),ticket[base+0x7f:base+0x8f])
    del common
    contents=[];data=section('content');offset=0
    for item in info['contents']:
        amount=(item['size']+15)&~15
        if amount>len(data)-offset:raise ValueError('Truncated encrypted content')
        plain=decrypt(title_key,item['index'].to_bytes(2,'big')+bytes(14),data[offset:offset+amount])[:item['size']]
        if hashlib.sha1(plain).hexdigest()!=item['sha1']:raise ValueError('Content hash verification failed')
        contents.append((item,plain));offset+=(amount+63)&~63
    del title_key
    # Verify every content before changing this profile. Existing bytes and
    # native progress are retained; conflicting installed content is an error.
    prefix=f"title/{info['title_id'][:8]}/{info['title_id'][8:]}"
    outputs={f'{prefix}/content/title.tmd':section('tmd'),
             f"ticket/{info['title_id'][:8]}/{info['title_id'][8:]}.tik":ticket}
    mapping_path=profile/'shared1/content.map'
    mapping=mapping_path.read_bytes() if mapping_path.exists() else b''
    if len(mapping)%28:raise ValueError('Truncated shared content map')
    shared={mapping[i+8:i+28].hex():mapping[i:i+8].decode('ascii') for i in range(0,len(mapping),28)}
    occupied={v for v in shared.values()};shared_changed=False
    for item,plain in contents:
        if item['flags']&0x8000:
            name=shared.get(item['sha1'])
            if name is None:
                number=0
                while f'{number:08x}' in occupied:number+=1
                name=f'{number:08x}';occupied.add(name);shared[item['sha1']]=name
                mapping+=name.encode()+bytes.fromhex(item['sha1']);shared_changed=True
            if len(name)!=8 or any(c not in '0123456789abcdefABCDEF' for c in name):raise ValueError('Invalid shared content name')
            outputs[f'shared1/{name}.app']=plain
        else:outputs[f"{prefix}/content/{item['id']}.app"]=plain
    uid_path=profile/'sys/uid.sys';uids=uid_path.read_bytes()
    if len(uids)%12:raise ValueError('Truncated UID table')
    entries=[struct.unpack_from('>QI',uids,i) for i in range(0,len(uids),12)]
    if len({t for t,u in entries})!=len(entries) or len({u for t,u in entries})!=len(entries):raise ValueError('Duplicate UID identity')
    uid=next((u for t,u in entries if t==tid),None)
    if uid is None:
        uid=max([0xfff]+[u for t,u in entries])+1
        outputs['sys/uid.sys']=uids+struct.pack('>QI',tid,uid)
    if shared_changed:outputs['shared1/content.map']=mapping
    mutable={'sys/uid.sys','shared1/content.map'}
    for relative,value in outputs.items():
        target=profile/relative
        if target.exists() and relative not in mutable and target.read_bytes()!=value:
            raise ValueError('Conflicting installed title content: '+relative)
    for relative,value in outputs.items():
        target=profile/relative;target.parent.mkdir(parents=True,exist_ok=True)
        if target.exists() and target.read_bytes()==value:continue
        # Each replacement is atomic. All inputs were checked above; an I/O
        # failure leaves a resumable copied profile, never damages originals.
        temporary=target.with_name(target.name+'.wiillr-install-tmp')
        with temporary.open('xb') as out:out.write(value)
        os.replace(temporary,target)
        if target.read_bytes()!=value:raise OSError('Installed content verification failed')
    (profile/prefix/'data').mkdir(parents=True,exist_ok=True)
    if nand_profile.snapshot(source)!=source_before:raise ValueError('Original NAND changed')
    return dict(title=info['title_id'],ios=info['ios'],uid=uid,contents=len(contents),
        content_hashes_verified=True,signatures_verified=False,wad_sha256=hashlib.sha256(raw).hexdigest(),
        ticket_common_key_index=ticket_index,decryption_common_key_index=index,
        ticket_index_overridden=key_index_override is not None,native_ES_import_tested=False,
        profile=str(profile),outputs={name:hashlib.sha256(value).hexdigest() for name,value in outputs.items()},
        original_profile_files=len(before),booted=False,accepted=False)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--wad',type=Path,required=True);p.add_argument('--common-key',type=Path,required=True)
    p.add_argument('--profile',type=Path,required=True);p.add_argument('--receipt',type=Path,required=True)
    p.add_argument('--offline-key-index',type=int,choices=(0,),
                   help='Explicit offline decryption override; preserves the original untrusted ticket')
    args=p.parse_args()
    if args.receipt.exists():raise FileExistsError('Use a fresh installation receipt')
    result=install(args.wad,args.common_key,args.profile,key_index_override=args.offline_key_index)
    args.receipt.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({name:result[name] for name in ('title','ios','contents','content_hashes_verified','accepted')}))
if __name__=='__main__':main()
