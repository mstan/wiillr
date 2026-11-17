/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/dol.h"
#include "wiillr/boot.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void) {
    CPUState cpu;
    CHECK(cpu_init(&cpu));
    u8 image[0x120] = {0};
    write_be32(image, 0x100);
    write_be32(image+0x48, 0x80003400);
    write_be32(image+0x90, 0x20);
    write_be32(image+0xd8, 0x80003410);
    write_be32(image+0xdc, 0x30);
    write_be32(image+0xe0, 0x3400);
    memset(image+0x100, 0xa5, 0x20);
    memset(cpu.ram+0x3400, 0xff, 0x40);
    u32 entry = 0xffffffff;
    CHECK(wii_dol_validate(&cpu,image,sizeof image,&entry) && entry==0x3400);
    CHECK(cpu.ram[0x3400]==0xff && cpu.ram[0x343f]==0xff);
    CHECK(wii_dol_load(&cpu, image, sizeof image, &entry));
    CHECK(entry == 0x3400 && cpu.ram[0x3400] == 0xa5 && cpu.ram[0x341f] == 0xa5);
    CHECK(cpu.ram[0x3420] == 0 && cpu.ram[0x343f] == 0);
    write_be32(image+0xe0, 0xc0003400);
    CHECK(wii_dol_load(&cpu, image, sizeof image, &entry));
    CHECK(entry == 0xc0003400);
    /* A bad later section must not modify already validated RAM. */
    write_be32(image+4, 0x100);
    write_be32(image+0x4c, 0xcc000000);
    write_be32(image+0x94, 4);
    memset(cpu.ram+0x3400, 0x3c, 0x40);
    CHECK(!wii_dol_load(&cpu, image, sizeof image, &entry));
    CHECK(cpu.ram[0x3400] == 0x3c && cpu.ram[0x343f] == 0x3c);
    write_be32(image+0x4c, 0xc0003400);
    CHECK(!wii_dol_load(&cpu, image, sizeof image, &entry));
    write_be32(image+0x94, 0);
    write_be32(image+0xe0, 0x3420);
    CHECK(!wii_dol_load(&cpu, image, sizeof image, &entry));
    write_be32(image+0xe0, 0x3400);
    CHECK(!wii_dol_load(&cpu, image, 0x110, &entry));
    write_be32(image+0x48, 0x817ffff0);
    CHECK(!wii_dol_load(&cpu, image, sizeof image, &entry));
    CHECK(cpu_alloc_mem2(&cpu,WII_MEM2_SIZE));
    CHECK(wii_native_ios_boot_memory(&cpu,37));
    CHECK(read_be32(cpu.ram+0x3140)==0x0025161f);
    CHECK(read_be32(cpu.ram+0x3128)==0x935e0000);
    CHECK(read_be32(cpu.ram+0x3148)==0x93600000);
    CHECK(read_be32(cpu.ram+0x314c)==0x93620000);
    CHECK(!wii_native_ios_boot_memory(&cpu,999));
    CHECK(read_be32(cpu.ram+0x3140)==0x0025161f);
    CHECK(wii_native_ios_boot_memory(&cpu,80));
    CHECK(read_be32(cpu.ram+0x3140)==0x00501b20);
    CHECK(read_be32(cpu.ram+0x3144)==0x00030310);
    CHECK(read_be32(cpu.ram+0x3128)==0x935e0000);
    CHECK(read_be32(cpu.ram+0x3148)==0x93600000);
    CHECK(read_be32(cpu.ram+0x314c)==0x93620000);
    CHECK(wii_ios4_boot_memory(&cpu));
    CHECK(read_be32(cpu.ram+0x3140)==0x00040003);
    CHECK(read_be32(cpu.ram+0x3128)==0x933e0000);
    cpu_free(&cpu);
    return 0;
}
