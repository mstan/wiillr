/* SPDX-License-Identifier: MIT */
#include "wiillr/starlet.hpp"
#include <vector>
#include <cstdio>
#include <stdexcept>
#define CHECK(x) do {if(!(x)) {std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}} while(0)
static bool faults(auto&& operation) {
    try {operation();return false;}catch(const std::runtime_error&) {return true;}
}
int main() {
    std::vector<uint8_t> mem1(0x1800000),mem2(0x4000000);
    wiillr::Starlet machine(mem1,mem2);
    CHECK(machine.read32(0x0d800010)==0);
    machine.advance(127);CHECK(machine.read32(0x0d800010)==0);
    machine.advance(1);CHECK(machine.read32(0x0d800010)==1);
    CHECK(machine.read32(0x0d800010)==1);
    machine.advance((uint64_t(1)<<39)-128);CHECK(machine.read32(0x0d800010)==0);
    machine.write32(0x10000020,0x12345678);
    CHECK(mem2[0x20]==0x12 && machine.read16(0x10000022)==0x5678);
    machine.write32(0xfffffffcu,0xaabbccdd);
    CHECK(machine.fetch32(0xfffffffcu)==0xaabbccdd);
    CHECK(faults([&]{machine.read32(0x08000000);}));
    CHECK(faults([&]{machine.read32(0xffffffff);}));
    CHECK(faults([&]{machine.load_ios({});}));
    std::vector<uint8_t> binary(16+88);
    auto put32=[&](size_t at,uint32_t value) {for(unsigned n=0;n<4;++n)binary[at+n]=uint8_t(value>>(24-n*8));};
    auto put16=[&](size_t at,uint16_t value) {binary[at]=uint8_t(value>>8);binary[at+1]=uint8_t(value);};
    put32(0,16);put32(8,88);
    put32(16,0x7f454c46);binary[20]=1;binary[21]=2;binary[22]=1;
    put16(16+16,2);put16(16+18,40);put32(16+20,1);put32(16+24,0xffff0000);
    put32(16+28,52);put16(16+40,52);put16(16+42,32);put16(16+44,1);
    put32(16+52,1);put32(16+56,84);put32(16+60,0xffff0000);put32(16+64,0xffff0000);
    put32(16+68,4);put32(16+72,8);put32(16+76,5);put32(16+80,4);put32(16+84,0xe1a00000);
    CHECK(machine.load_ios(binary)==0xffff0000);
    CHECK(machine.fetch32(0xffff0000)==0xe1a00000 && machine.read32(0xffff0004)==0);
    put32(16+68,9);
    CHECK(faults([&]{machine.load_ios(binary);}));
    CHECK(machine.fetch32(0xffff0000)==0xe1a00000);
    CHECK(machine.read32(0x0d800214)==0x11);
    machine.write32(0x0d8b0008,0x8000);
    CHECK(machine.read32(0x0d8b0008)==0x8000);
    machine.write32(0x0d800070,1);
    CHECK(machine.read32(0x0d800070)==1);
    machine.write32(0x0d800140,0x31);
    CHECK(machine.read32(0x0d800140)==0x31);
    CHECK(faults([&]{machine.read32(0x0d800148);}));
    CHECK(machine.read32(0x0d80018c)==0x1000);
    machine.write32(0x0d80018c,0x1c00);
    CHECK(machine.read32(0x0d80018c)==0x1c00);
    CHECK(faults([&]{machine.write32(0x0d80018c,0);}));
    machine.write16(0x0d8b420a,0);
    machine.write16(0x0d8b420c,0x3400);
    CHECK(machine.read16(0x0d8b420c)==0x3400);
    machine.write16(0x0d8b420a,1);
    CHECK(machine.read16(0x0d8b420a)==1);
    machine.write16(0x0d8b4228,8);
    CHECK(machine.read16(0x0d8b422a)==8);
    machine.write16(0x0d8b4228,0);
    CHECK(machine.read16(0x0d8b422a)==0);
    auto cpwrite=[&](unsigned n,uint32_t value) {machine.coproc_write(15,0,n,0,0,value);};
    /* Section table maps virtual0x20000000 into physical MEM2. Client domain
     * permissions must distinguish a user write from a privileged write. */
    machine.write32(0x4800,0x10000c02); /* index0x200, AP3 domain0 */
    cpwrite(2,0x4000);cpwrite(3,1);cpwrite(1,0x500f9);
    CHECK(machine.read32(0x20000020)==0x12345678);
    machine.write32(0x20000024,0x98765432);
    CHECK(mem2[0x24]==0x98);
    /* Change the actual descriptor without a cached lookup. */
    mem1[0x4802]=0x08; /* AP2: user read-only */
    machine.cpu.cpsr.mode=uint8_t(armv4t::Mode::User);
    CHECK(machine.read32(0x20000020)==0x12345678);
    CHECK(faults([&]{machine.write32(0x20000020,0);}));
    machine.cpu.cpsr.mode=uint8_t(armv4t::Mode::Supervisor);
    CHECK(machine.coproc_read(15,0,5,0,0)==13);
    CHECK(machine.coproc_read(15,0,6,0,0)==0x20000020);
    machine.write32(0x20000020,0x11223344);
    CHECK(mem2[0x20]==0x11);
    CHECK(faults([&]{machine.read32(0x30000000);}));
    mem1[0x4802]=0; /* AP0: ROM/system permissions in SCTLR. */
    cpwrite(1,0x501f9);
    CHECK(machine.read32(0x20000020)==0x11223344);
    CHECK(faults([&]{machine.write32(0x20000020,0);}));
    machine.cpu.cpsr.mode=uint8_t(armv4t::Mode::User);
    CHECK(faults([&]{machine.read32(0x20000020);}));
    machine.cpu.cpsr.mode=uint8_t(armv4t::Mode::Supervisor);
    cpwrite(1,0x502f9);
    machine.cpu.cpsr.mode=uint8_t(armv4t::Mode::User);
    CHECK(machine.read32(0x20000020)==0x11223344);
    CHECK(faults([&]{machine.write32(0x20000020,0);}));
    machine.cpu.cpsr.mode=uint8_t(armv4t::Mode::Supervisor);
    cpwrite(1,0x500fa);
    CHECK(faults([&]{machine.read32(0x10000021);}));
    CHECK(machine.coproc_read(15,0,5,0,0)==1);
    CHECK(machine.coproc_read(15,0,6,0,0)==0x10000021);
    /* Coarse second-level small page, then fine-table tiny page. */
    cpwrite(1,0x500f8);
    machine.write32(0x4800,0x00008001);
    machine.write32(0x8000,0x10000ff2);
    cpwrite(1,0x500f9);
    CHECK(machine.read32(0x20000020)==0x11223344);
    cpwrite(1,0x500f8);
    machine.write32(0x4800,0x00008003);
    machine.write32(0x8000,0x10000033);
    cpwrite(1,0x500f9);
    CHECK(machine.read32(0x20000020)==0x11223344);
    CHECK(faults([&]{machine.coproc_read(14,0,1,0,0);}));
    cpwrite(1,0x500f8);
    CHECK(machine.read32(0x0d800180)==0x53102);
    machine.write32(0x0d8001d0,0);CHECK(machine.read32(0x0d8001d0)==0);
    machine.write32(0x0d800180,0x53002);
    machine.write32(0x0d8001cc,wii_audio_pll_preset(false));
    CHECK(machine.read32(0x0d8001cc)==wii_audio_pll_preset(false));
    CHECK(faults([&]{machine.write32(0x0d800180,0xffffffff);}));
    return 0;
}
