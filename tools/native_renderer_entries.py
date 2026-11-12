# SPDX-License-Identifier: MIT
"""Derive the attested MM9 entry seam without changing its prepared catalog."""
import argparse
import hashlib
import json
from pathlib import Path


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--frame-owner',action='store_true')
    args=parser.parse_args()
    source=args.source.resolve(strict=True);output=args.output.resolve()
    # This page identity belongs to the supported WR9E original. Runtime
    # function-byte attestation remains mandatory before a replacement acts.
    pages={'overlay_800D5000_hF28561AD0793EB68.c':[0x800d5e50]}
    markers={}
    if args.frame_owner:
        pages['overlay_800D5000_hF28561AD0793EB68.c'].append(0x800d5990)
        pages['overlay_80099000_hB4A449361DFA7467.c']=[0x8009905c,0x80099200,0x80099394,0x800995f0]
        pages['overlay_8009D000_hD295BDE95AA6CA50.c']=[]
        pages['overlay_800E8000_hB0FDEF9B22C93FE2.c']=[0x800e8024,0x800e8700,0x800e88d8]
        pages['overlay_800A7000_hF8A0C39E65D5F103.c']=[0x800a7764,0x800a77c4]
        pages['overlay_80083000_h748FAA87F633CA31.c']=[0x80083730]
        pages['overlay_80113000_hB4BBDB2F39E5B955.c']=[0x801131a8]
        markers['overlay_8009D000_hD295BDE95AA6CA50.c']=[0x8009dcb8,0x8009de3c]
    if source.name not in pages:raise ValueError('MM9 native entry requires its original verified page')
    header=source.parent.parent/'generated_abi.h'
    text=source.read_text(encoding='utf-8')
    include='#include "../generated_abi.h"'
    if text.count(include)!=1 or not header.is_file():
        raise ValueError('Prepared MM9 entry/header seam differs')
    text=text.replace(include,'#include "'+header.as_posix()+'"\n#include "wiillr/mm9_renderer_hook.h"',1)
    # Same-page BL shadow calls jump to this label directly. Publish the local
    # clock before the host boundary; an accepted HLE return retains its own
    # PC/cycles and discards only the optional host shadow stack. A decline
    # continues the unchanged original body at the same guest instruction.
    for address in pages[source.name]:
        marker=f'\nlabel_{address:08X}:\n'
        if text.count(marker)!=1:raise ValueError('Native entry label missing or duplicated')
        text=text.replace(marker,marker+
            '    ctx->cycles = dr_cycles;\n'
            f'    ctx->pc = 0x{address:08X}u;\n'
            f'    if (wii_mm9_renderer_hook_call(ctx,0x{address:08X}u)) return;\n'
            '    dr_cycles = ctx->cycles;\n',1)
    for address in markers.get(source.name,[]):
        marker=f'\nlabel_{address:08X}:\n'
        if text.count(marker)!=1:raise ValueError('Native frame marker missing or duplicated')
        text=text.replace(marker,marker+f'    wii_mm9_renderer_frame_marker(ctx,0x{address:08X}u);\n',1)
    output.parent.mkdir(parents=True,exist_ok=True)
    encoded=text.encode('utf-8')
    if not output.exists() or output.read_bytes()!=encoded:output.write_bytes(encoded)
    receipt={'entries':[hex(a) for a in pages[source.name]],'markers':[hex(a) for a in markers.get(source.name,[])],
        'source':str(source),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
        'header':str(header),'header_sha256':hashlib.sha256(header.read_bytes()).hexdigest(),
        'output':str(output),'sha256':hashlib.sha256(encoded).hexdigest()}
    output.with_suffix('.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
