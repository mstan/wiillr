# SPDX-License-Identifier: MIT
"""Derive the verified IPL43U graphics-owner seams; prepared pages stay intact."""
import argparse
import hashlib
import json
import re
from pathlib import Path

PAGES = {
    'overlay_8133D000_hD5682C7A05DD06C3.c': [0x8133de74,0x8133de78,0x8133df24,0x8133df74,0x8133dfc0,0x8133dfe4],
    'overlay_8133E000_hFED53A5E2136774C.c': [0x8133e230,0x8133e2d4],
    'overlay_81334000_h703661867DD22FE3.c': [0x81334328,0x81334c7c],
    'overlay_81362000_h0F1F482724EA35E2.c': [0x81362a6c,0x81362acc,0x81362b4c,0x81362bfc,0x81362cf8,0x81362d3c,0x81362fbc],
    'overlay_815F7000_hE2E9E75E71DF4F7A.c': [0x815f7170,0x815f7ca4],
    'overlay_8136A000_h3A73618112D90D0E.c': [0x8136a79c,0x8136a83c,0x8136a88c],
    'overlay_81523000_h16D86AD8A1A74717.c': [0x8152373c],
    'overlay_813AB000_hE5B434BA7B0728DE.c': [0x813ab1e0],
    'overlay_813AD000_hB3D1F1BBBDE430F5.c': [0x813adfdc],
    'overlay_81355000_h9493E3CE6DADA3CB.c': [0x81355b68,0x81355b88],
    'overlay_81547000_h46D2BC75A4888175.c': [0x81547fa8,0x81547d20,0x81547c30,0x81547c94,0x81547ce0,0x81547f2c,0x81547f58,0x81547810],
    'overlay_81548000_h4C2A72241129E9E8.c': [0x81548010],
    'overlay_81363000_hFB1BC4FADE1427AD.c': [0x8136339c],
    'overlay_8151F000_h8FB635CCD040C90D.c': [0x8151f22c],
}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();source=args.source.resolve(strict=True);output=args.output.resolve()
    if source.name not in PAGES:raise ValueError('Original IPL43U frame page required')
    header=source.parent.parent/'generated_abi.h';text=source.read_text(encoding='utf-8');include='#include "../generated_abi.h"'
    if not header.is_file() or text.count(include)!=1:raise ValueError('Prepared IPL43U entry/header differs')
    text=text.replace(include,'#include "'+header.as_posix()+'"\n#include "wiillr/menu_frame_hook.h"',1)
    for address in PAGES[source.name]:
        marker=f'\nlabel_{address:08X}:\n'
        if text.count(marker)!=1:raise ValueError('Owner entry label missing or duplicated')
        cases=re.findall(rf'case 0x{address:08X}u:\s*(?:dr_cycles -= ([0-9]+)u;\s*)?goto label_{address:08X};',text)
        if len(cases)!=1:raise ValueError('Prepared entry clock offset differs')
        offset=int(cases[0] or 0)
        # A middle-of-block owner must publish already completed instructions;
        # a decline retains the emitter's deferred block-charge convention.
        text=text.replace(marker,marker+f'    ctx->cycles = dr_cycles + {offset}u;\n'+f'    ctx->pc = 0x{address:08X}u;\n'+
            f'    if (wii_menu_frame_entry(ctx,0x{address:08X}u)) return;\n    dr_cycles = ctx->cycles - '+str(offset)+'u;\n',1)
    output.parent.mkdir(parents=True,exist_ok=True);data=text.encode('utf-8')
    if not output.exists() or output.read_bytes()!=data:output.write_bytes(data)
    receipt={'entries':[hex(a) for a in PAGES[source.name]],'source':str(source),
        'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'header':str(header),
        'header_sha256':hashlib.sha256(header.read_bytes()).hexdigest(),'output':str(output),'sha256':hashlib.sha256(data).hexdigest()}
    output.with_suffix('.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
