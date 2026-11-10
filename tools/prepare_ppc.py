# SPDX-License-Identifier: MIT
"""Compile verified discovery pages; they are code inputs, never boot state."""
import argparse, hashlib, json, os
from pathlib import Path
import subprocess, sys, zlib

ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture',type=Path,required=True,action='append',
                   help='Verified original execution; repeat to retain earlier loader variants')
    p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--work-root',type=Path,required=True)
    args=p.parse_args();compiler=args.compiler.resolve(strict=True)
    pages={};captures=[];journals=[];title_identity=None
    for source in args.capture:
        capture=source.resolve();run=json.loads((capture/'run.json').read_text())
        if run.get('execution') not in ('PPC-discovery-interpreter','PPC-native-with-discovery','PPC-static-native') or run.get('exit_code') not in (0,1):
            raise ValueError('Expected an original-title execution with explicit miss capture')
        for key in ('log','mem1'):
            path=Path(run[key]['path'])
            if path.parent!=capture or sha(path)!=run[key]['sha256']:raise ValueError('Changed discovery '+key)
        for key in ('content','tmd'):
            item=run['inputs'][key]
            if sha(Path(item['path']))!=item['sha256']:raise ValueError('Changed original input '+key)
        current=(run['inputs']['content']['sha256'],run['inputs']['tmd']['sha256'],run['boot_profile'])
        if title_identity is not None and current!=title_identity:raise ValueError('Mixed title/profile identities')
        title_identity=current
        journal=(capture/'native-misses.jsonl').read_text();journals.append(journal.rstrip()+'\n')
        captures.append(dict(capture=str(capture),run_sha256=sha(capture/'run.json'),journal_sha256=sha(capture/'native-misses.jsonl')))
        blobs={p.name.lower():p for p in (capture/'pages').glob('*.bin')}
        validated=set()
        for line in journal.splitlines():
            row=json.loads(line)
            if row.get('event')!='native_miss':continue
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
    identity=dict(captures=captures,journal_sha256=sha(journal_path),
        compiler=dict(path=str(compiler),sha256=sha(compiler)),
        generator_sha256=sha(generator),pages={name:v[1] for name,v in pages.items()},
        generated={str(path.relative_to(work)):sha(path) for path in sorted((work/'generated').glob('**/*')) if path.is_file()},
        cpu='broadway',pc_namespaces='preserved',execution='unrun static candidate',accepted=False)
    (work/'prepared.json').write_text(json.dumps(identity,indent=2)+'\n')
    print(json.dumps(dict(work_root=str(work),variants=len(chunks),accepted=False)))
if __name__=='__main__':main()
