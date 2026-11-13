/* SPDX-License-Identifier: MIT */
#include "wiillr/starlet.hpp"
#include "arm_decode.h"
#include "thumb_decode.h"
#include <vector>
#include <cstdio>
#include <stdexcept>
#define CHECK(x) do {if(!(x)) {std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}} while(0)
static bool faults(auto&& operation) {
    try {operation();return false;}catch(const std::runtime_error&) {return true;}
}
int main() {
    std::vector<uint8_t> mem1(0x1800000),mem2(0x4000000);
    {
        wiillr::Starlet exception(mem1,mem2);
        exception.coproc_write(15,0,1,0,0,0x500f8); /* Explicit low-vector case. */
        exception.write32(8,0xe1b0f00e); /* Original handler-style MOVS PC,LR. */
        exception.write32(0x100,0xef000001);
        exception.cpu.cpsr.mode=uint8_t(armv4t::Mode::User);
        exception.cpu.cpsr.i=false;exception.cpu.cpsr.f=false;
        exception.cpu.cpsr.n=true;exception.cpu.cpsr.c=true;
        exception.cpu.R[13]=0x1000;exception.cpu.R[14]=0x2345;
        exception.cpu.banked_sp[armv4t::Bank_Supervisor]=0x400;
        exception.cpu.R[15]=0x100;
        exception.execute(armv4t::ArmDecoder::decode(exception.fetch32(0x100),0x100));
        CHECK(exception.cpu.R[15]==8 && exception.cpu.R[14]==0x104);
        CHECK(exception.cpu.R[13]==0x400 && exception.cpu.cpsr.i && !exception.cpu.cpsr.f);
        CHECK(exception.cpu.banked_spsr[armv4t::Bank_Supervisor]==0xa0000010);
        exception.execute(armv4t::ArmDecoder::decode(exception.fetch32(8),8));
        CHECK(exception.cpu.R[15]==0x104 && exception.cpu.R[13]==0x1000);
        CHECK(exception.cpu.cpsr.mode==uint8_t(armv4t::Mode::User) && !exception.cpu.cpsr.i);
        /* ARM926 CP15.V changes the actual exception vector fetch. */
        exception.cpu.cpsr.mode=uint8_t(armv4t::Mode::Supervisor);
        exception.coproc_write(15,0,1,0,0,0x520f8);
        exception.write32(0xffff0008,0xe1b0f00e);
        exception.write16(0x200,0xdfab);
        exception.cpu.cpsr.mode=uint8_t(armv4t::Mode::User);
        exception.cpu.cpsr.t=true;exception.cpu.thumb=true;exception.cpu.R[15]=0x200;
        exception.execute(armv4t::ThumbDecoder::decode(exception.fetch16(0x200),0x200));
        CHECK(exception.cpu.R[15]==0xffff0008 && exception.cpu.R[14]==0x202);
        CHECK(!exception.cpu.cpsr.t && exception.swi_entries()==2);
        exception.execute(armv4t::ArmDecoder::decode(exception.fetch32(0xffff0008),0xffff0008));
        CHECK(exception.cpu.R[15]==0x202 && exception.cpu.cpsr.t && exception.cpu.thumb);
    }
    {
        wiillr::Starlet irq(mem1,mem2);
        irq.write32(0xffff0018,0xe25ef004); /* SUBS PC,LR,#4 */
        irq.write16(0x200,0x46c0); /* Thumb NOP */
        irq.cpu.cpsr.mode=uint8_t(armv4t::Mode::User);
        irq.cpu.cpsr.t=irq.cpu.thumb=true;irq.cpu.cpsr.i=false;irq.cpu.cpsr.f=false;
        irq.cpu.R[13]=0x1000;irq.cpu.R[15]=0x200;
        irq.cpu.banked_sp[armv4t::Bank_IRQ]=0x400;
        WiiIpc shared;wii_ipc_init(&shared);irq.attach_ipc(shared,9000);
        irq.write32(0x0d80000c,0x10);irq.write32(0x0d80003c,0x80000000);
        CHECK(wii_ipc_write(&shared,0,0x123400,9000));
        CHECK(wii_ipc_write(&shared,4,0x11,9000));
        irq.advance(399);CHECK(!wii_ipc_arm_interrupt(&shared));
        irq.advance(1);CHECK(wii_ipc_arm_interrupt(&shared));
        CHECK(irq.read32(0x0d800000)==0x123400);
        irq.step();
        CHECK(irq.cpu.R[15]==0xffff0018 && irq.cpu.R[14]==0x206);
        CHECK(irq.cpu.R[13]==0x400 && irq.cpu.cpsr.i && !irq.cpu.cpsr.f);
        CHECK(irq.cpu.banked_spsr[armv4t::Bank_IRQ]==0x30 && irq.irq_entries()==1);
        irq.write32(0x0d80000c,4);irq.write32(0x0d800038,0x80000000);
        irq.step();
        CHECK(irq.cpu.R[15]==0x202 && irq.cpu.cpsr.t && irq.cpu.thumb);
        CHECK(irq.cpu.R[13]==0x1000 && !irq.cpu.cpsr.i);
        irq.write32(0x0d800008,0x123400);irq.write32(0x0d80000c,1);
        irq.write32(0x0d800034,0x40000000);
        irq.advance(400);
        uint32_t reply=0;CHECK(wii_ipc_read(&shared,8,&reply) && reply==0x123400);
        CHECK(wii_ipc_ppc_interrupt(&shared));
    }
    {
        wiillr::Starlet exception(mem1,mem2);
        exception.write32(0xffff0004,0xe1b0f00e); /* MOVS PC,LR */
        exception.write16(0x300,0xde00); /* Undefined Thumb instruction */
        exception.cpu.cpsr.mode=uint8_t(armv4t::Mode::User);
        exception.cpu.cpsr.t=exception.cpu.thumb=true;exception.cpu.R[15]=0x300;
        exception.step();
        CHECK(exception.cpu.R[15]==0xffff0004 && exception.cpu.R[14]==0x302);
        CHECK(exception.undefined_entries()==1);
        exception.step();CHECK(exception.cpu.R[15]==0x302 && exception.cpu.cpsr.t);
        exception.cpu.cpsr.mode=uint8_t(armv4t::Mode::Supervisor);
        exception.cpu.cpsr.t=exception.cpu.thumb=false;
        /* An unmapped fetch takes the original prefetch-abort vector. */
        exception.coproc_write(15,0,2,0,0,0x4000);
        exception.coproc_write(15,0,3,0,0,0xffffffff);
        exception.write32(0x4800,0);
        exception.coproc_write(15,0,1,0,0,0x520f9);
        exception.cpu.R[15]=0x20000000;exception.step();
        CHECK(exception.cpu.R[15]==0xffff000c && exception.cpu.R[14]==0x20000004);
        CHECK(exception.prefetch_aborts()==1 && exception.coproc_read(15,0,5,0,1)==5);
        /* Decode is supplied separately to test a real data permission miss. */
        exception.coproc_write(15,0,1,0,0,0x520f8);
        exception.cpu.R[0]=0x20000000;exception.cpu.R[15]=0x400;
        exception.coproc_write(15,0,1,0,0,0x520f9);
        exception.execute(armv4t::ArmDecoder::decode(0xe5901000,0x400));
        CHECK(exception.cpu.R[15]==0xffff0010 && exception.cpu.R[14]==0x408);
        CHECK(exception.data_aborts()==1 && exception.coproc_read(15,0,6,0,0)==0x20000000);
    }
    {
        wiillr::Starlet hardware(mem1,mem2);
        hardware.write32(0x0d800014,3);hardware.write32(0x0d80003c,1);
        hardware.advance(383);CHECK(!(hardware.ipc().arm_flags&1));
        hardware.advance(1);CHECK(wii_ipc_arm_interrupt(&hardware.ipc()));
        hardware.write32(0x0d800038,1);CHECK(!wii_ipc_arm_interrupt(&hardware.ipc()));
        hardware.write32(0x0d8001ec,0x80000005);
        CHECK(faults([&]{hardware.read32(0x0d8001f0);}));
        std::array<uint8_t,128> otp;for(unsigned n=0;n<otp.size();++n)otp[n]=uint8_t(n);
        hardware.load_otp(otp);CHECK(hardware.read32(0x0d8001f0)==0x14151617);
        CHECK(faults([&]{hardware.load_otp(std::span<const uint8_t>(otp).first(127));}));
        CHECK(faults([&]{hardware.write32(0x0d8001ec,0x40000005);}));
        CHECK(hardware.read32(0x0d8001f0)==0x14151617);
        hardware.write32(0x0d8000fc,0x20);hardware.write32(0x0d8000dc,0xffffff);
        hardware.write32(0x0d8000e0,0x24);hardware.write32(0x0d8000c0,0x20);
        CHECK(hardware.read32(0x0d8000e0)==0x24 && hardware.read32(0x0d8000c0)==0x20);
        hardware.write32(0x0d8000fc,0);
        CHECK(hardware.read32(0x0d8000c0)==0 && hardware.read32(0x0d8000e0)==0x24);
        CHECK(hardware.read32(0x0d8000dc)==0xffffff);
        CHECK(faults([&]{hardware.read32(0x0d8000e8);}));
        CHECK(faults([&]{hardware.write32(0x0d010000,0x80000000);}));
    }
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
    CHECK(machine.read32(0x0d800180)==0);
    machine.write32(0x0d8001d0,0);CHECK(machine.read32(0x0d8001d0)==0);
    machine.write32(0x0d800180,0x100);
    machine.write32(0x0d8001cc,wii_audio_pll_preset(false));
    CHECK(machine.read32(0x0d8001cc)==wii_audio_pll_preset(false));
    CHECK(faults([&]{machine.write32(0x0d800180,0xffffffff);}));
    return 0;
}
