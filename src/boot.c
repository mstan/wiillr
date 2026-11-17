/* SPDX-License-Identifier: GPL-3.0-or-later */
/* IOS4 v3 / IOS35 v3608 / IOS37 v5663 / IOS80 v6944 launch-memory layout, recorded in:
 * dolphin-emu/dolphin Source/Core/Core/IOS/{IOS,VersionInfo}.cpp.
 * Values are boot-contract metadata, never captured execution state. The
 * original ARM kernel implementations remain separate execution routes. */
#include "wiillr/boot.h"
#include <string.h>

bool wii_native_ios_boot_memory(CPUState* cpu,u32 major) {
    if(major!=4 && major!=35 && major!=37 && major!=80)return false;
    if (!cpu || !cpu->ram || cpu->ram_size != GC_MAIN_RAM_SIZE ||
        !cpu->mem2 || cpu->mem2_size != WII_MEM2_SIZE) return false;
    u32 values[] = {
        0x01800000,0x01800000,0x81800000,0,0x81800000,0xdeadbeef,
        0x04000000,0x04000000,0x93400000,0x90000800,0x933e0000,0xdeadbeef,
        0x933e0000,0x93400000,0x11,0xdeadbeef,
        0x00040003,0x00081006,0xdeadbeef,0xdeadbeef,0xdeadbeef,0xdeadbeef,
        0x0000ff01,0xdeadbeef,0
    };
    if(major==35 || major==37 || major==80) {
        values[8]=values[13]=0x93600000;
        values[10]=values[12]=0x935e0000;
        values[16]=major==35 ? 0x00230e18:major==37 ? 0x0025161f:0x00501b20;
        values[17]=major==80 ? 0x00030310:0x00030110;
        values[18]=0x93600000;values[19]=0x93620000;
    }
    memset(cpu->ram,0,0x4000);
    for (unsigned i=0;i<sizeof values/sizeof *values;++i)
        write_be32(cpu->ram+0x3100+i*4,values[i]);
    write_be32(cpu->ram+0x28,GC_MAIN_RAM_SIZE);
    write_be32(cpu->ram+0x34,0x81800000);
    write_be32(cpu->ram+0xf0,GC_MAIN_RAM_SIZE);
    return true;
}
bool wii_ios4_boot_memory(CPUState* cpu){return wii_native_ios_boot_memory(cpu,4);}

bool wii_disc_boot_memory(CPUState* cpu,WiiDisc* disc) {
    u8 boot[0x440];
    if(!cpu || !cpu->ram || cpu->ram_size!=GC_MAIN_RAM_SIZE || !disc ||
       wii_disc_ios(disc)!=(read_be32(cpu->ram+0x3140)>>16) ||
       !wii_disc_boot_header(disc,boot,sizeof boot))return false;
    const u64 fst_size=(u64)read_be32(boot+0x428)*4;
    const u64 fst_offset=(u64)read_be32(boot+0x424)*4;
    if(!fst_size || fst_size>0x100000u)return false;
    const u32 fst_address=(GC_MAIN_RAM_SIZE-(u32)fst_size)&~31u;
    if(!wii_disc_read(disc,fst_offset,cpu->ram+fst_address,(size_t)fst_size))return false;
    memcpy(cpu->ram,boot,0x20);memcpy(cpu->ram+0x3180,boot,4);
    write_be32(cpu->ram+0x20,0x0d15ea5e);write_be32(cpu->ram+0x24,1);
    write_be32(cpu->ram+0x34,0x80000000u+fst_address);
    /* Wii OSInit takes MEM1 arena bounds from IOS low memory too. Leaving
     * this at 0x81800000 lets the game's heap erase the live FST. */
    write_be32(cpu->ram+0x3110,0x80000000u+fst_address);
    write_be32(cpu->ram+0x38,0x80000000u+fst_address);write_be32(cpu->ram+0x3c,(u32)fst_size);
    write_be32(cpu->ram+0x2c,0x23);
    write_be32(cpu->ram+0x3184,0x80000000u);
    write_be32(cpu->ram+0x3188,read_be32(cpu->ram+0x3140));
    write_be32(cpu->ram+0x3194,0);
    write_be32(cpu->ram+0x3198,(u32)(wii_disc_partition(disc)/4));
    cpu->ram[0x319c]=wii_disc_size(disc)>0x118280000ull?0x81:0x80;
    write_be32(cpu->ram+0xf8,243000000u);write_be32(cpu->ram+0xfc,729000000u);
    return true;
}
