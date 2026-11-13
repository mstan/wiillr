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
    parser.add_argument('--gameplay',action='store_true')
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
        pages['overlay_800EA000_hA12005351E132861.c']=[0x800ea2e4]
        pages['overlay_800A7000_hF8A0C39E65D5F103.c']=[0x800a7764,0x800a77c4]
        pages['overlay_80083000_h748FAA87F633CA31.c']=[0x80083730]
        pages['overlay_80113000_hB4BBDB2F39E5B955.c']=[0x801131a8]
        markers['overlay_8009D000_hD295BDE95AA6CA50.c']=[0x8009dcb8,0x8009de3c]
    if args.gameplay:
        if args.frame_owner:raise ValueError('Gameplay and graphics seams are selected separately')
        pages={'overlay_80089000_h85E6611173071FCD.c':[]}
        markers={}
    if source.name not in pages:raise ValueError('MM9 native entry requires its original verified page')
    header=source.parent.parent/'generated_abi.h'
    text=source.read_text(encoding='utf-8')
    include='#include "../generated_abi.h"'
    if text.count(include)!=1 or not header.is_file():
        raise ValueError('Prepared MM9 entry/header seam differs')
    owner_header='wiillr/mm9_mods.h' if args.gameplay else 'wiillr/mm9_renderer_hook.h'
    text=text.replace(include,'#include "'+header.as_posix()+'"\n#include "'+owner_header+'"',1)
    if args.gameplay:
        # The original rlwinm. sets CR for the following pause branch. Raise
        # its existing predicate BEFORE that instruction, never after CR was
        # computed. Native draw/audio/input work remains on the original path.
        marker='\nlabel_80089F90:\n'
        if text.count(marker)!=1:raise ValueError('Original stage pause predicate differs')
        text=text.replace(marker,marker+
            '    if (wii_mm9_mods_stage_frozen(ctx)) ctx->gpr[31] = 1u;\n',1)
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
    if source.name=='overlay_800EA000_hA12005351E132861.c':
        # Preserve scan/allocation/flags and the original instruction clock.
        # Admission, retention and respawn share the selected horizontal view.
        for address,register,immediate,operator in (
                (0x800ea358,0,-144,'-'),(0x800ea374,0,-144,'-'),(0x800ea394,15,144,'+')):
            marker=f'\nlabel_{address:08X}:\n'
            begin=text.index(marker)+len(marker);end=text.index('\nlabel_',begin)
            original=f'ctx->gpr[{register}] = ctx->gpr[4] + (u32)(s32)({immediate});'
            section=text[begin:end]
            if section.count(original)!=1:raise ValueError('Original stage spawn bound differs')
            section=section.replace(original,
                f'ctx->gpr[{register}] = ctx->gpr[4] {operator} wii_mm9_renderer_spawn_radius();')
            text=text[:begin]+section+text[end:]
        for address,original,replacement in (
            (0x800ea66c,'ctx->gpr[8] = ctx->gpr[10] + (u32)(s32)(384);',
                'ctx->gpr[8] = ctx->gpr[10] + wii_mm9_renderer_spawn_radius() + 240u;'),
            (0x800ea670,'u32 val_b = (u32)(0x0300u);',
                'u32 val_b = 2u * (wii_mm9_renderer_spawn_radius() + 240u);'),
            (0x800ea690,'ctx->gpr[8] = ctx->gpr[10] + (u32)(s32)(152);',
                'ctx->gpr[8] = ctx->gpr[10] + wii_mm9_renderer_spawn_radius() + 8u;'),
            (0x800ea694,'u32 val_b = (u32)(0x0130u);',
                'u32 val_b = 2u * (wii_mm9_renderer_spawn_radius() + 8u);'),
            (0x800ea6c0,'ctx->gpr[5] = ctx->gpr[5] + (u32)(s32)(152);',
                'ctx->gpr[5] = ctx->gpr[5] + wii_mm9_renderer_spawn_radius() + 8u;'),
            (0x800ea6c8,'u32 val_b = (u32)(0x0130u);',
                'u32 val_b = 2u * (wii_mm9_renderer_spawn_radius() + 8u);'),
            (0x800ea758,'ctx->gpr[0] = ctx->gpr[3] + (u32)(s32)(144);',
                'ctx->gpr[0] = ctx->gpr[3] + wii_mm9_renderer_spawn_radius();'),
            (0x800ea75c,'u32 val_b = (u32)(0x0120u);',
                'u32 val_b = 2u * wii_mm9_renderer_spawn_radius();')):
            marker=f'\nlabel_{address:08X}:\n'
            begin=text.index(marker)+len(marker);end=text.index('\nlabel_',begin)
            section=text[begin:end]
            if section.count(original)!=1:raise ValueError('Original stage retention bound differs')
            text=text[:begin]+section.replace(original,replacement)+text[end:]
    output.parent.mkdir(parents=True,exist_ok=True)
    encoded=text.encode('utf-8')
    if not output.exists() or output.read_bytes()!=encoded:output.write_bytes(encoded)
    receipt={'entries':[hex(a) for a in pages[source.name]],'markers':[hex(a) for a in markers.get(source.name,[])],
        'source':str(source),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
        'header':str(header),'header_sha256':hashlib.sha256(header.read_bytes()).hexdigest(),
        'output':str(output),'sha256':hashlib.sha256(encoded).hexdigest()}
    output.with_suffix('.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
