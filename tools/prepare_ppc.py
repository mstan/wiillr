# SPDX-License-Identifier: MIT
"""Compile verified discovery pages; they are code inputs, never boot state."""
import argparse, hashlib, json, os
from pathlib import Path
import subprocess, sys, zlib

ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture',type=Path,required=True)
    p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--work-root',type=Path,required=True)
    args=p.parse_args();capture=args.capture.resolve();compiler=args.compiler.resolve(strict=True)
    run=json.loads((capture/'run.json').read_text())
    if run.get('execution')!='PPC-discovery-interpreter' or run.get('exit_code')!=0:
        raise ValueError('Expected a completed original-title discovery')
    for key in ('log','mem1'):
        path=Path(run[key]['path'])
        if path.parent!=capture or sha(path)!=run[key]['sha256']:raise ValueError('Changed discovery '+key)
    for key in ('content','tmd'):
        item=run['inputs'][key]
        if sha(Path(item['path']))!=item['sha256']:raise ValueError('Changed original input '+key)
    pages={};blobs={p.name.lower():p for p in (capture/'pages').glob('*.bin')}
    for line in (capture/'native-misses.jsonl').read_text().splitlines():
        row=json.loads(line)
        if row.get('event')!='native_miss':continue
        address=row['page'];crc=row['page_crc32'];name=f'{address:08x}_{crc}.bin'
        path=blobs.get(name)
        if path is None:raise ValueError('Missing captured code page '+name)
        if name not in pages:
            data=path.read_bytes()
            if len(data)!=4096 or f'{zlib.crc32(data):08x}'!=crc:raise ValueError('Changed code page')
            pages[name]=(data,sha(path))
        offset=row['pc']-address
        if offset<0 or offset>4092 or pages[name][0][offset:offset+4].hex()!=row['raw']:
            raise ValueError('Miss instruction/page identity differs')
    if not pages:raise ValueError('No captured instructions')
    work=args.work_root.resolve();work.mkdir(parents=True,exist_ok=False)
    (work/'temp').mkdir()
    generator=ROOT/'external/gc-wii-core/tools/gen_overlay_module.py'
    env=dict(os.environ,TEMP=str(work/'temp'),TMP=str(work/'temp'),PYTHONDONTWRITEBYTECODE='1')
    command=[sys.executable,'-B',str(generator),'--journal',str(capture/'native-misses.jsonl'),
             '--pages',str(capture/'pages'),'--dolrecomp',str(compiler),'--output',str(work/'generated'),
             '--top-pages','0','--jobs','1','--cpu','broadway','--preserve-pc']
    with (work/'generate.log').open('w') as log:
        subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    chunks=sorted((work/'generated/chunks').glob('*.c'))
    if len(chunks)!=len(pages):raise ValueError('Some original code variants were not compiled')
    identity=dict(capture=str(capture),run_sha256=sha(capture/'run.json'),
        journal_sha256=sha(capture/'native-misses.jsonl'),compiler=dict(path=str(compiler),sha256=sha(compiler)),
        generator_sha256=sha(generator),pages={name:v[1] for name,v in pages.items()},
        generated={str(path.relative_to(work)):sha(path) for path in chunks},
        cpu='broadway',pc_namespaces='preserved',execution='unrun static candidate',accepted=False)
    (work/'prepared.json').write_text(json.dumps(identity,indent=2)+'\n')
    print(json.dumps(dict(work_root=str(work),variants=len(chunks),accepted=False)))
if __name__=='__main__':main()
