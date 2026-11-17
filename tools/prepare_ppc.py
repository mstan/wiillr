# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile verified discovery pages; they are code inputs, never boot state."""
import argparse, hashlib, json, os, re, struct
from pathlib import Path
import subprocess, sys, zlib

ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def capture_contract(run,journal):
    if run.get('code_mutations'):
        raise ValueError('Code-mutated QA capture cannot seed an original-code native catalog')
    strict_stop=run.get('execution')=='PPC-static-native' and run.get('exit_code') in (1,2)
    if run.get('execution') not in ('PPC-discovery-interpreter','PPC-native-with-discovery','PPC-static-native') or (run.get('exit_code') not in (0,1) and not strict_stop):
        raise ValueError('Expected an original-title execution with explicit miss capture')
    if strict_stop:
        summaries=[json.loads(line) for line in journal.splitlines() if line.strip() and json.loads(line).get('event')=='summary']
        if not summaries or summaries[-1].get('instructions')!=0 or not summaries[-1].get('unique_misses',0):
            raise ValueError('Strict-stop capture must record zero interpretation and a real native miss')
    return strict_stop

def catalog_pages(path):
    """Union previously prepared titles without changing their PC namespaces."""
    root=path.resolve(strict=True);receipt=root/'prepared.json'
    identity=json.loads(receipt.read_text())
    if identity.get('cpu')!='broadway' or identity.get('pc_namespaces')!='preserved':
        raise ValueError('Expected a preserved-namespace Broadway catalog')
    pages={}
    for name,digest in identity['pages'].items():
        if not re.fullmatch(r'[0-9a-f]{8}_[0-9a-f]{8}\.bin',name):
            raise ValueError('Invalid prepared code-page name')
        data=(root/'pages'/name).read_bytes()
        if len(data)!=4096 or hashlib.sha256(data).hexdigest()!=digest or f'{zlib.crc32(data):08x}'!=name[9:17]:
            raise ValueError('Changed prepared code page')
        pages[name]=(data,digest)
    if not pages:raise ValueError('Empty prepared catalog')
    return pages,dict(path=str(root),receipt_sha256=sha(receipt),pages=len(pages))

def dol_text_pages(path,code_ranges=()):
    """Original initialized DOL bytes, not an execution/boot-state capture.

    Fill page-sharing data sections too: the runtime verifies the complete
    page. Zero padding describes the original image only; changed live pages
    still need their own captured identity and never match by address alone.
    """
    data=path.read_bytes()
    if len(data)<0x100:raise ValueError('Short DOL header')
    word=lambda offset:struct.unpack_from('>I',data,offset)[0]
    sections=[];loaded={};text=[]
    for index in range(18):
        offset,address,size=word(index*4),word(0x48+index*4),word(0x90+index*4)
        if not size:continue
        physical=address&0x3fffffff
        capacity=0x1800000-physical if physical<0x1800000 else 0x14000000-physical if 0x10000000<=physical<0x14000000 else 0
        if address&0xc0000000==0x40000000 or offset<0x100 or offset>len(data) or size>len(data)-offset or size>capacity or address+size>0x100000000:
            raise ValueError('DOL section outside original file/main RAM')
        if index<7 and (address|size)&3:raise ValueError('Unaligned DOL text')
        if any(physical<old['physical']+old['size'] and old['physical']<physical+size for old in sections):
            raise ValueError('Overlapping DOL sections/aliases')
        section=dict(index=index,address=address,physical=physical,size=size,offset=offset);sections.append(section)
        if index<7:text.append(section)
        at=0
        while at<size:
            position=physical+at;base=position&~4095;within=position-base;length=min(size-at,4096-within)
            loaded.setdefault(base,bytearray(4096))[within:within+length]=data[offset+at:offset+at+length];at+=length
    entry=word(0xe0)
    physical_entry=entry&0x3fffffff
    if entry&3 or entry&0xc0000000==0x40000000 or not any(s['physical']<=physical_entry<s['physical']+s['size'] for s in text):
        raise ValueError('DOL entry outside text')
    # Some original loaders label initialized executable code as DOL data.
    # Explicit ranges include its original full pages without changing the
    # image, boot state, section metadata or runtime content verification.
    selected=list(text)
    for start,end in code_ranges:
        if start&4095 or end&4095 or start>=end or end>0x100000000 or not any(
                s['address']<=start and end<=s['address']+s['size'] for s in sections):
            raise ValueError('Code range must be complete pages inside one initialized DOL section')
        selected.append(dict(address=start,size=end-start))
    pages={}
    for section in selected:
        for address in range(section['address']&~4095,(section['address']+section['size']+4095)&~4095,4096):
            page=bytes(loaded[address&0x3fffffff]);name=f'{address:08x}_{zlib.crc32(page):08x}.bin'
            pages[name]=(page,hashlib.sha256(page).hexdigest())
    return pages,dict(path=str(path),sha256=sha(path),entry=entry,text_sections=text,code_ranges=list(code_ranges),pages=len(pages))
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture',type=Path,action='append',default=[],
                   help='Verified original execution; repeat to retain earlier loader variants')
    p.add_argument('--dol',type=Path,action='append',default=[],help='Original DOL: union every initialized text page with captured variants')
    p.add_argument('--dol-code-range',nargs=2,type=lambda value:int(value,0),action='append',default=[],metavar=('START','END'),help='Explicit page-aligned executable range inside each original initialized DOL')
    p.add_argument('--catalog',type=Path,action='append',default=[],help='Previously verified preparation root; repeat to combine title catalogs')
    p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--work-root',type=Path,required=True)
    args=p.parse_args();compiler=args.compiler.resolve(strict=True)
    if not args.capture and not args.dol and not args.catalog:p.error('Supply an original DOL, verified capture or prepared catalog')
    if args.dol_code_range and not args.dol:p.error('DOL code ranges require an original DOL')
    pages={};captures=[];journals=[];title_identity=None;dols=[];catalogs=[]
    for source in args.catalog:
        original,record=catalog_pages(source);catalogs.append(record)
        for name,value in original.items():
            if name in pages and pages[name][0]!=value[0]:raise ValueError('Conflicting prepared code-page identity')
            pages[name]=value
            journals.append(json.dumps(dict(event='prepared_code_page',pc=int(name[:8],16),receipt_sha256=record['receipt_sha256']))+'\n')
    for source in args.capture:
        capture=source.resolve();run=json.loads((capture/'run.json').read_text())
        if (capture/'code-mutations.json').exists():
            run['code_mutations']=str(capture/'code-mutations.json')
        journal=(capture/'native-misses.jsonl').read_text()
        strict_stop=capture_contract(run,journal)
        for key in ('log','mem1'):
            path=Path(run[key]['path'])
            if path.parent!=capture or sha(path)!=run[key]['sha256']:raise ValueError('Changed discovery '+key)
        for key in ('content','tmd'):
            item=run['inputs'][key]
            if sha(Path(item['path']))!=item['sha256']:raise ValueError('Changed original input '+key)
        current=(run['inputs']['content']['sha256'],run['inputs']['tmd']['sha256'],run['boot_profile'])
        if title_identity is not None and current!=title_identity:raise ValueError('Mixed title/profile identities')
        title_identity=current
        journals.append(journal.rstrip()+'\n')
        captures.append(dict(capture=str(capture),run_sha256=sha(capture/'run.json'),journal_sha256=sha(capture/'native-misses.jsonl')))
        blobs={p.name.lower():p for p in (capture/'pages').glob('*.bin')}
        validated=set();misses=0
        for line in journal.splitlines():
            row=json.loads(line)
            if row.get('event')!='native_miss':continue
            misses+=1
            address=row['page'];crc=row['page_crc32'];name=f'{address:08x}_{crc}.bin'
            path=blobs.get(name)
            if path is None:raise ValueError('Missing captured code page '+name)
            if name not in validated:
                data=path.read_bytes()
                if len(data)!=4096 or f'{zlib.crc32(data):08x}'!=crc:raise ValueError('Changed code page')
                if name in pages and data!=pages[name][0]:raise ValueError('Conflicting code-page identity')
                pages[name]=(data,hashlib.sha256(data).hexdigest());validated.add(name)
            data=pages[name][0]
            offset=row['pc']-address
            if offset<0 or offset>4092 or data[offset:offset+4].hex()!=row['raw']:
                raise ValueError('Miss instruction/page identity differs')
        if strict_stop and not misses:raise ValueError('Strict-stop capture contains no original code miss')
    for source in args.dol:
        original,record=dol_text_pages(source.resolve(strict=True),args.dol_code_range);dols.append(record)
        for name,value in original.items():
            if name in pages and pages[name][0]!=value[0]:raise ValueError('Conflicting original DOL page identity')
            pages[name]=value
            # The existing generator ranks all records with a PC. Mark static
            # inputs honestly; these are never execution/miss claims.
            journals.append(json.dumps(dict(event='static_text_page',pc=int(name[:8],16),source_sha256=record['sha256']))+'\n')
    if not pages:raise ValueError('No captured instructions')
    work=args.work_root.resolve();work.mkdir(parents=True,exist_ok=False)
    (work/'temp').mkdir()
    (work/'pages').mkdir()
    for name,(data,_) in pages.items():(work/'pages'/name).write_bytes(data)
    journal_path=work/'native-misses.jsonl'
    # The overlay generator discards its first line when starting a tail read.
    # Give it a blank boundary and include the complete combined capture.
    journal_path.write_text('\n'+''.join(journals))
    mib=1024*1024
    journal_mib=(journal_path.stat().st_size+mib-1)//mib
    generator=ROOT/'external/gc-wii-core/tools/gen_overlay_module.py'
    env=dict(os.environ,TEMP=str(work/'temp'),TMP=str(work/'temp'),PYTHONDONTWRITEBYTECODE='1')
    command=[sys.executable,'-B',str(generator),'--journal',str(journal_path),'--tail-mb',str(journal_mib),
             '--pages',str(work/'pages'),'--dolrecomp',str(compiler),'--output',str(work/'generated'),
             '--top-pages','0','--jobs','1','--cpu','broadway','--preserve-pc']
    with (work/'generate.log').open('w') as log:
        subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    chunks=sorted((work/'generated/chunks').glob('*.c'))
    if len(chunks)!=len(pages):raise ValueError('Some original code variants were not compiled')
    identity=dict(captures=captures,dol_inputs=dols,catalog_inputs=catalogs,journal_sha256=sha(journal_path),
        compiler=dict(path=str(compiler),sha256=sha(compiler)),
        generator_sha256=sha(generator),pages={name:v[1] for name,v in pages.items()},
        generated={str(path.relative_to(work)):sha(path) for path in sorted((work/'generated').glob('**/*')) if path.is_file()},
        cpu='broadway',pc_namespaces='preserved',execution='unrun static candidate',accepted=False)
    (work/'prepared.json').write_text(json.dumps(identity,indent=2)+'\n')
    print(json.dumps(dict(work_root=str(work),variants=len(chunks),accepted=False)))
if __name__=='__main__':main()
