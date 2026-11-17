/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/disc.h"
extern "C" {
#include "wiillr/boot.h"
}
#include <array>
#include <cstdio>
#include <cstring>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %u: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char** argv){
    CHECK(argc==4);
    auto* disc=wii_disc_open(argv[1],argv[2]);CHECK(disc);
    CHECK(wii_disc_ios(disc)==35 && wii_disc_partition(disc)==0x50000);
    std::array<uint8_t,64> data{};
    const bool ok=wii_disc_read(disc,0x7bf0,data.data(),data.size());
    if(!std::strcmp(argv[3],"valid")){
        CHECK(ok);
        for(unsigned n=0;n<data.size();++n)CHECK(data[n]==(0x7bf0+n)%251);
        CHECK(!wii_disc_read(disc,UINT64_MAX,data.data(),1));
        CHECK(!wii_disc_read(disc,3*0x7c00,data.data(),1));
        CPUState cpu;CHECK(cpu_init(&cpu) && cpu_alloc_mem2(&cpu,WII_MEM2_SIZE));
        CHECK(wii_native_ios_boot_memory(&cpu,35) && wii_disc_boot_memory(&cpu,disc));
        const auto fst=read_be32(cpu.ram+0x38);
        CHECK(fst==read_be32(cpu.ram+0x34) && fst==read_be32(cpu.ram+0x3110));
        CHECK(read_be32(cpu.ram+(fst&0x1fffffffu))==0x01000000);
        CHECK(read_be32(cpu.ram+0x3198)==0x50000/4 && cpu.ram[0x319c]==0x80);
        CHECK(read_be32(cpu.ram+0x3188)==0x00230e18);
        CHECK(wii_native_ios_boot_memory(&cpu,37) && !wii_disc_boot_memory(&cpu,disc));
        cpu_free(&cpu);
    }else CHECK(!ok);
    wii_disc_close(disc);return 0;
}
