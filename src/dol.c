/* SPDX-License-Identifier: MIT */
#include "wiillr/dol.h"
#include "memory/memory.h"
#include <string.h>

static u8* ram_span(CPUState* cpu, u32 address, u32 size) {
    /* Only title RAM, never MMIO, firmware windows or locked cache. */
    u32 physical = address & 0x3fffffffu;
    if (physical >= cpu->ram_size &&
        !(cpu->mem2 && physical >= 0x10000000u &&
          physical - 0x10000000u < cpu->mem2_size))
        return NULL;
    u32 available;
    u8* result = gcn_mem_resolve(cpu, address, &available);
    return result && size <= available ? result : NULL;
}

static bool dol(CPUState* cpu, const u8* data, size_t size, u32* entry,bool load) {
    if (!cpu || !data || !entry || size < 0x100)
        return false;
    u32 pc = read_be32(data + 0xe0);
    u8* entry_bytes = ram_span(cpu, pc, 4);
    if ((pc & 3u) || !entry_bytes)
        return false;
    bool executable_entry = false;
    for (unsigned i = 0; i < 18; ++i) {
        u32 offset = read_be32(data + i * 4);
        u32 address = read_be32(data + 0x48 + i * 4);
        u32 length = read_be32(data + 0x90 + i * 4);
        if (!length) continue;
        if (offset < 0x100 || offset > size || length > size - offset)
            return false;
        u8* destination = ram_span(cpu, address, length);
        if (!destination || (i < 7 && ((address | length) & 3u)))
            return false;
        if (i < 7 && (uintptr_t)entry_bytes >= (uintptr_t)destination &&
            (uintptr_t)entry_bytes - (uintptr_t)destination <= length - 4)
            executable_entry = true;
        /* Two initialized sections must not overwrite each other, even when
         * their effective addresses name different aliases of the same RAM. */
        for (unsigned j = 0; j < i; ++j) {
            u32 previous_size = read_be32(data + 0x90 + j * 4);
            if (!previous_size) continue;
            u8* previous = ram_span(cpu, read_be32(data + 0x48 + j * 4), previous_size);
            if ((uintptr_t)destination < (uintptr_t)previous + previous_size &&
                (uintptr_t)previous < (uintptr_t)destination + length)
                return false;
        }
    }
    u32 bss_size = read_be32(data + 0xdc);
    u8* bss = bss_size ? ram_span(cpu, read_be32(data + 0xd8), bss_size) : NULL;
    if (!executable_entry || (bss_size && !bss))
        return false;
    if(!load){*entry=pc;return true;}
    if (bss_size) memset(bss, 0, bss_size);
    for (unsigned i = 0; i < 18; ++i) {
        u32 length = read_be32(data + 0x90 + i * 4);
        if (length) memcpy(ram_span(cpu, read_be32(data + 0x48 + i * 4), length),
                           data + read_be32(data + i * 4), length);
    }
    *entry = pc;
    return true;
}
bool wii_dol_validate(CPUState* cpu,const u8* data,size_t size,u32* entry){return dol(cpu,data,size,entry,false);}
bool wii_dol_load(CPUState* cpu,const u8* data,size_t size,u32* entry){return dol(cpu,data,size,entry,true);}
