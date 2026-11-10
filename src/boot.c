/* SPDX-License-Identifier: MIT */
/* IOS4 v3 launch-memory constants and layout, independently recorded in:
 * dolphin-emu/dolphin Source/Core/Core/IOS/{IOS,VersionInfo}.cpp.
 * Values are boot-contract metadata, never captured execution state. The
 * original IOS4 ARM implementation remains an outstanding execution route. */
#include "wiillr/boot.h"
#include <string.h>

bool wii_native_ios_boot_memory(CPUState* cpu,u32 major) {
    if(major!=4 && major!=37)return false;
    if (!cpu || !cpu->ram || cpu->ram_size != GC_MAIN_RAM_SIZE ||
        !cpu->mem2 || cpu->mem2_size != WII_MEM2_SIZE) return false;
    u32 values[] = {
        0x01800000,0x01800000,0x81800000,0,0x81800000,0xdeadbeef,
        0x04000000,0x04000000,0x93400000,0x90000800,0x933e0000,0xdeadbeef,
        0x933e0000,0x93400000,0x11,0xdeadbeef,
        0x00040003,0x00081006,0xdeadbeef,0xdeadbeef,0xdeadbeef,0xdeadbeef,
        0x0000ff01,0xdeadbeef,0
    };
    if(major==37) {
        values[8]=values[13]=0x93600000;
        values[10]=values[12]=0x935e0000;
        values[16]=0x0025161f;values[17]=0x00030110;
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
