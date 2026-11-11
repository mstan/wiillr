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
    args=parser.parse_args()
    source=args.source.resolve(strict=True);output=args.output.resolve()
    # This page identity belongs to the supported WR9E original. Runtime
    # function-byte attestation remains mandatory before a replacement acts.
    if source.name!='overlay_800D5000_hF28561AD0793EB68.c':
        raise ValueError('MM9 screen entry requires its original verified page')
    header=source.parent.parent/'generated_abi.h'
    text=source.read_text(encoding='utf-8')
    include='#include "../generated_abi.h"'
    marker='\nlabel_800D5E50:\n'
    if text.count(include)!=1 or text.count(marker)!=1 or not header.is_file():
        raise ValueError('Prepared MM9 entry/header seam differs')
    text=text.replace(include,'#include "'+header.as_posix()+'"\n#include "wiillr/mm9_renderer_hook.h"',1)
    # Same-page BL shadow calls jump to this label directly. Publish the local
    # clock before the host boundary; an accepted HLE return retains its own
    # PC/cycles and discards only the optional host shadow stack. A decline
    # continues the unchanged original body at the same guest instruction.
    text=text.replace(marker,marker+
        '    ctx->cycles = dr_cycles;\n'
        '    ctx->pc = 0x800D5E50u;\n'
        '    if (wii_mm9_renderer_hook_call(ctx,0x800D5E50u)) return;\n'
        '    dr_cycles = ctx->cycles;\n',1)
    output.parent.mkdir(parents=True,exist_ok=True)
    encoded=text.encode('utf-8')
    if not output.exists() or output.read_bytes()!=encoded:output.write_bytes(encoded)
    receipt={'entry':'0x800D5E50','source':str(source),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
        'header':str(header),'header_sha256':hashlib.sha256(header.read_bytes()).hexdigest(),
        'output':str(output),'sha256':hashlib.sha256(encoded).hexdigest()}
    output.with_suffix('.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
