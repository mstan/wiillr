# SPDX-License-Identifier: MIT
"""Derive the verified IPL43U graphics-owner seams; prepared pages stay intact."""
import argparse
import hashlib
import json
import re
from pathlib import Path

CHARWRITER = {0x81516e34,0x81518144}
GRID={0x813aaa38,0x813ac4a4,0x813ac4e4,0x813ac4e8,0x813ac630,0x813ac63c,0x813ac674,0x813ac688,0x813ac948,0x813ace78,
      0x813ad39c,0x813ad55c,0x813ad69c,0x813ad8b8,0x813ad8e0,0x813ada94,0x813ade6c,0x813adf18,0x813adf48,
      0x813ae99c,0x813b01b8,0x813b069c,0x813b06e4,0x813b06e8}
PAGES = {
    'overlay_813AA000_hD60A45318DDA17BB.c':[0x813aaa38],
    'overlay_813AC000_h5230F2ABC59E8696.c':[0x813ac4a4,0x813ac4e4,0x813ac4e8,0x813ac630,0x813ac63c,0x813ac674,0x813ac688,0x813ac948,0x813ace78],
    'overlay_813AE000_h589D1C20D37F65D4.c':[0x813ae99c],
    'overlay_813B0000_h33B48A8D795853DD.c':[0x813b01b8,0x813b069c,0x813b06e4,0x813b06e8],
    'overlay_81516000_h04B1E64323F202E0.c': [0x81516e34],
    'overlay_81518000_hD0939295F2091B27.c': [0x81518144],
    'overlay_81445000_h660EB67BC70D8F51.c': [0x81445904],
    'overlay_8133D000_hD5682C7A05DD06C3.c': [0x8133de74,0x8133de78,0x8133df24,0x8133df74,0x8133dfc0,0x8133dfe4],
    'overlay_8133E000_hFED53A5E2136774C.c': [0x8133e230,0x8133e2d4],
    'overlay_81334000_h703661867DD22FE3.c': [0x81334328,0x81334c7c],
    'overlay_81362000_h0F1F482724EA35E2.c': [0x81362a6c,0x81362acc,0x81362b4c,0x81362bfc,0x81362cf8,0x81362d3c,0x81362fbc],
    'overlay_815F7000_hE2E9E75E71DF4F7A.c': [0x815f7170,0x815f7ca4,0x815f7ca8],
    'overlay_8136A000_h3A73618112D90D0E.c': [0x8136a79c,0x8136a83c,0x8136a88c],
    'overlay_81523000_h16D86AD8A1A74717.c': [0x8152373c],
    'overlay_813AB000_hE5B434BA7B0728DE.c': [0x813ab1e0],
    'overlay_813AD000_hB3D1F1BBBDE430F5.c': [0x813adfdc,0x813ad39c,0x813ad55c,0x813ad69c,0x813ad8b8,0x813ad8e0,0x813ada94,0x813ade6c,0x813adf18,0x813adf48],
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
    text=text.replace(include,'#include "'+header.as_posix()+'"\n#include "wiillr/menu_frame_hook.h"\n#include "wiillr/menu_grid.h"',1)
    if any(a in CHARWRITER for a in PAGES[source.name]):
        text=text.replace('#include "wiillr/menu_frame_hook.h"','#include "wiillr/menu_frame_hook.h"\n#include "wiillr/menu_text_hook.h"')
    for address in PAGES[source.name]:
        marker=f'\nlabel_{address:08X}:\n'
        if text.count(marker)!=1:raise ValueError('Owner entry label missing or duplicated')
        cases=re.findall(rf'case 0x{address:08X}u:\s*(?:dr_cycles -= ([0-9]+)u;\s*)?goto label_{address:08X};',text)
        if len(cases)!=1:raise ValueError('Prepared entry clock offset differs')
        offset=int(cases[0] or 0)
        service="wii_menu_grid_entry" if address in GRID else "wii_menu_text_hook_call" if address in CHARWRITER else "wii_menu_frame_entry"
        # A middle-of-block owner must publish already completed instructions;
        # a decline retains the emitter's deferred block-charge convention.
        text=text.replace(marker,marker+f'    ctx->cycles = dr_cycles + {offset}u;\n'+f'    ctx->pc = 0x{address:08X}u;\n'+
            f'    if ({service}(ctx,0x{address:08X}u)) return;\n    dr_cycles = ctx->cycles - '+str(offset)+'u;\n',1)
    output.parent.mkdir(parents=True,exist_ok=True);data=text.encode('utf-8')
    if not output.exists() or output.read_bytes()!=data:output.write_bytes(data)
    receipt={'entries':[hex(a) for a in PAGES[source.name]],'source':str(source),
        'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'header':str(header),
        'header_sha256':hashlib.sha256(header.read_bytes()).hexdigest(),'output':str(output),'sha256':hashlib.sha256(data).hexdigest()}
    output.with_suffix('.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
