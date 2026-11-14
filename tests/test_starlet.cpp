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
        wiillr::Starlet serial(mem1,mem2);
        std::array<uint8_t,256> image;
        for(unsigned i=0;i<image.size();++i)image[i]=uint8_t(i^0x5a);
        const auto original=image;
        CHECK(faults([&]{serial.load_seeprom(std::span<const uint8_t>(image).first(255));}));
        serial.load_seeprom(image);
        serial.write32(0x0d8000fc,0);
        serial.write32(0x0d8000dc,0xffffff);
        serial.write32(0x0d8000e4,0x1c00); /* CS, CLK and DI outputs; DO input. */
        uint32_t pins=0;
        auto output=[&](uint32_t value) {pins=value;serial.write32(0x0d8000e0,pins);};
        auto select=[&](bool enabled) {output(enabled?0x400:0);};
        auto send=[&](uint32_t value,unsigned count) {
            for(unsigned i=count;i>0;--i) {
                const auto low=0x400u|((value>>(i-1)&1u)?0x1000u:0);
                output(low);output(low|0x800u);
            }
        };
        auto receive=[&](unsigned count) {
            uint32_t value=0;
            for(unsigned i=0;i<count;++i) {
                output(0x400);output(0xc00);
                value=(value<<1)|((serial.read32(0x0d8000e8)>>13)&1);
            }
            return value;
        };
        auto command=[&](uint32_t value) {select(false);select(true);send(value,11);};
        auto read=[&](unsigned address) {command(0x600|address);const auto value=receive(16);select(false);return value;};
        for(unsigned i=0;i<128;++i)CHECK(read(i)==(uint32_t(image[2*i])<<8|image[2*i+1]));
        command(0x6ff); /* Don't-care high address bit; sequential wrap. */
        CHECK(!(serial.read32(0x0d8000e8)&0x2000)); /* Actual READ dummy zero. */
        output(pins); /* Repeating a register value does not clock the chip. */
        CHECK(receive(32)==0xa4a55a5bu);select(false);
        auto program=[&](unsigned address,uint16_t value) {command(0x500|address);send(value,16);select(false);};
        program(3,0x1234);serial.advance(wiillr::Seeprom::program_cycles);
        CHECK(read(3)==0x5c5d); /* Power-on and EWDS protect programming. */
        command(0x4c0);select(false); /* EWEN */
        program(3,0x1234);select(true);
        CHECK(!(serial.read32(0x0d8000e8)&0x2000));
        CHECK(serial.seeprom_image()[6]==0x5c); /* No early store before completion. */
        serial.advance(wiillr::Seeprom::program_cycles-1);
        CHECK(!(serial.read32(0x0d8000e8)&0x2000));
        serial.advance(1);CHECK(serial.read32(0x0d8000e8)&0x2000);select(false);
        CHECK(read(3)==0x1234 && image==original);
        command(0x700|3);select(false); /* ERASE word */
        serial.advance(wiillr::Seeprom::program_cycles);CHECK(read(3)==0xffff);
        command(0x400);select(false); /* EWDS */
        program(3,0x5678);serial.advance(wiillr::Seeprom::program_cycles);CHECK(read(3)==0xffff);
        command(0x4c0);select(false);
        command(0x440);send(0xa55a,16);select(false); /* WRAL */
        serial.advance(wiillr::Seeprom::program_cycles);CHECK(read(0)==0xa55a && read(127)==0xa55a);
        command(0x480);select(false); /* ERAL */
        serial.advance(wiillr::Seeprom::program_cycles);CHECK(read(0)==0xffff && read(127)==0xffff);
        command(0x500|7);send(0xab,8);select(false); /* Incomplete write is cancelled. */
        serial.advance(wiillr::Seeprom::program_cycles);CHECK(read(7)==0xffff);
        CHECK(faults([&]{serial.read32(0x0d8000e8);})); /* No invented general board input. */
        CHECK(faults([&]{serial.read32(0x0d8000f8);}));
        serial.load_seeprom(original); /* Reattach resets write-enable and private state. */
        CHECK(read(3)==0x5c5d);
        program(3,0x9876);serial.advance(wiillr::Seeprom::program_cycles);CHECK(read(3)==0x5c5d);
    }
    {
        /* Exception return takes its state from SPSR, then ignores the low
         * PC bits for that state. Target bit0 must not act like BX. Cover
         * logical/arithmetic PC writes and LDM's common return helper. */
        const uint32_t returns[]={0xe1b0f00e,0xe25ef004,0xe8d08000};
        for(unsigned op=0;op<3;++op)for(unsigned thumb=0;thumb<2;++thumb)for(unsigned low=0;low<4;++low) {
            wiillr::Starlet returning(mem1,mem2);
            returning.coproc_write(15,0,1,0,0,0x520fa); /* Alignment checking on. */
            const uint32_t target=0x300|low;
            returning.cpu.R[13]=0x1000;returning.cpu.R[14]=target+(op==1?4:0);
            returning.cpu.R[15]=0x100;returning.cpu.R[0]=0x2000;
            returning.cpu.banked_sp[armv4t::Bank_User]=0x500;
            returning.cpu.banked_lr[armv4t::Bank_User]=0x9876;
            returning.cpu.banked_spsr[armv4t::Bank_Supervisor]=0xa000001f|(thumb?0x20:0);
            returning.write32(0x2000,target);
            returning.execute(armv4t::ArmDecoder::decode(returns[op],0x100));
            CHECK(returning.cpu.R[15]==(target&~(thumb?1u:3u)));
            CHECK(returning.cpu.cpsr.t==bool(thumb) && returning.cpu.thumb==bool(thumb));
            CHECK(returning.cpu.cpsr.mode==uint8_t(armv4t::Mode::System));
            CHECK(returning.cpu.cpsr.n && returning.cpu.cpsr.c && !returning.cpu.cpsr.z && !returning.cpu.cpsr.v);
            CHECK(!returning.cpu.cpsr.i && !returning.cpu.cpsr.f);
            CHECK(returning.cpu.R[13]==0x500 && returning.cpu.R[14]==0x9876);
            CHECK(returning.cpu.banked_sp[armv4t::Bank_Supervisor]==0x1000);
            CHECK(returning.cpu.banked_lr[armv4t::Bank_Supervisor]==target+(op==1?4:0));
            CHECK(!faults([&]{if(thumb)returning.fetch16(returning.cpu.R[15]);else returning.fetch32(returning.cpu.R[15]);}));
        }
    }
    {
        wiillr::Starlet usb(mem1,mem2);
        for(unsigned port=0;port<2;++port) {
            const uint32_t base=0x0d050000+port*0x10000;
            CHECK(usb.read32(base)==0x10 && usb.read32(base+0x10)==0);
            usb.write32(base+4,0x100); /* Route to old owner, then request ownership. */
            usb.write32(base+8,8);CHECK(usb.read32(base+0xc)==0x40000000);
            usb.write32(base+0x10,0xc0000000);usb.advance(1);
            const uint32_t interrupt=1u<<(5+port);
            CHECK(usb.ipc().arm_flags&interrupt);
            usb.write32(base+0x14,0x40000000); /* Disable does NOT acknowledge status. */
            CHECK(usb.read32(base+0x10)==0x80000000 && usb.read32(base+0x14)==0x80000000);
            CHECK(usb.read32(base+0xc)==0x40000000);
            usb.write32(0x0d800038,interrupt);usb.advance(1);CHECK(!(usb.ipc().arm_flags&interrupt));
            usb.write32(base+0x10,0x40000000);usb.advance(1);CHECK(usb.ipc().arm_flags&interrupt);
            usb.write32(base+0xc,0);CHECK(usb.read32(base+0xc)==0x40000000);
            usb.write32(base+0xc,0x40000000);CHECK(usb.read32(base+0xc)==0);
            usb.write32(base+0x18,0x123456ff);CHECK(usb.read32(base+0x18)==0x12345600);
            usb.write32(base+8,1);CHECK(usb.read32(base+8)==1);
            usb.advance(2429);CHECK(usb.read32(base+8)==1);
            usb.advance(1);CHECK(usb.read32(base+8)==0);
            CHECK(usb.read32(base+4)==0xc0 && usb.read32(base+0x10)==0 && usb.read32(base+0x18)==0);
            CHECK(faults([&]{usb.write32(base+4,0xbc);})); /* No silent descriptor/USB service success. */
            CHECK(faults([&]{usb.write32(base+8,2);}));
            CHECK(usb.read32(base+0x48)==0x02000302 && usb.read32(base+0x54)==0x100);
            CHECK(faults([&]{usb.read32(base+0x5c);})); /* Selected profile has only two ports. */
            CHECK(faults([&]{usb.write16(base+0x14,0xffff);}));
        }
    }
    {
        wiillr::Starlet flash(mem1,mem2);
        std::vector<uint8_t> media(wiillr::NandController::page_size*128,0xff);
        std::fill_n(media.data()+5*wiillr::NandController::page_size,0x800,0x5c);
        flash.load_nand(media);
        std::vector<std::pair<uint32_t,uint32_t>> stores;
        flash.observe_stores([](void* user,uint32_t at,uint32_t size) {
            static_cast<std::vector<std::pair<uint32_t,uint32_t>>*>(user)->emplace_back(at,size);
        },&stores);
        flash.write32(0x0d010008,0);flash.write32(0x0d01000c,5);
        flash.write32(0x0d010000,0x9f000000);flash.advance(243);
        flash.write32(0x0d010010,0x10001000);flash.write32(0x0d010014,0x10002000);
        flash.write32(0x0d010000,0xc030b840);flash.advance(6074);CHECK(!(flash.ipc().arm_flags&2));
        flash.advance(1);CHECK(flash.ipc().arm_flags&2);
        CHECK(flash.read32(0x10001000)==0x5c5c5c5c && flash.read32(0x10002000)==0xffffffff);
        CHECK(stores.size()==2 && stores[0]==std::make_pair(0x10001000u,0x800u) && stores[1]==std::make_pair(0x10002000u,0x50u));
    }
    {
        wiillr::Starlet idle(mem1,mem2);unsigned stores=0;
        idle.observe_stores([](void* p,uint32_t at,uint32_t size){if(at==0x10001080 && size==4)++*static_cast<unsigned*>(p);},&stores);
        idle.load_nand(std::vector<uint8_t>(wiillr::NandController::page_size*64,0xff));
        idle.write32(0x0d010000,0x9c600000);idle.advance(243);
        idle.write32(0x0d010000,0xc0d08000); // A real 2ms peripheral operation.
        const uint32_t base=0x0d060000;
        idle.write32(base+0x18,0x10001000);idle.write32(base+0x10,0x80000004);idle.write32(base+4,0x283);
        idle.advance(243000-1);CHECK(!stores && !(idle.ipc().arm_flags&0x40));
        idle.advance(1);CHECK(stores==1 && (idle.ipc().arm_flags&0x40));
        CHECK(idle.read32(0x10001080)==0x01000000); // Actual LE16 frame plus zero pad.
        CHECK((idle.read32(0x0d010000)&0x80000000) && !(idle.ipc().arm_flags&2));
        idle.advance(243000);CHECK(!(idle.read32(0x0d010000)&0x80000000) && (idle.ipc().arm_flags&2));
    }
    {
        wiillr::Starlet cache(mem1,mem2);
        cache.write32(0x10000500,0x12345678);
        for(uint32_t opcode:{0xee17ff7au,0xee17ff7eu}) {
            cache.cpu.R[15]=0x100;
            cache.cpu.cpsr.n=cache.cpu.cpsr.c=cache.cpu.cpsr.v=cache.cpu.cpsr.q=true;
            cache.cpu.cpsr.z=false;
            cache.execute(armv4t::ArmDecoder::decode(opcode,0x100));
            CHECK(cache.cpu.R[15]==0x104 && cache.cpu.cpsr.z);
            CHECK(!cache.cpu.cpsr.n && !cache.cpu.cpsr.c && !cache.cpu.cpsr.v && cache.cpu.cpsr.q);
            CHECK(cache.cpu.cpsr.mode==uint8_t(armv4t::Mode::Supervisor) && cache.cpu.cpsr.i);
            CHECK(cache.read32(0x10000500)==0x12345678);
        }
        CHECK(faults([&]{cache.coproc_read(15,0,7,9,3);}));
        cache.cpu.cpsr.mode=uint8_t(armv4t::Mode::User);
        CHECK(faults([&]{cache.coproc_read(15,0,7,10,3);}));
    }
    {
        wiillr::Starlet drain(mem1,mem2);
        CHECK(drain.read32(0x0d800188)==0 && drain.read32(0x0d80018c)==0x1000);
        drain.write32(0x10000500,0x12345678);
        drain.load_nand(std::vector<uint8_t>(wiillr::NandController::page_size,0xff));
        drain.write32(0x0d010000,0xc0ff8000); /* A pending timed flash reset. */
        drain.write32(0x0d800188,0x02000000);
        CHECK((drain.read32(0x0d80018c)&15)==0); /* Request alone cannot ACK. */
        drain.write32(0x0d800134,0x400);
        CHECK((drain.read32(0x0d80018c)&15)==0);
        drain.write32(0x0d800124,0x280);
        CHECK(drain.read32(0x0d80018c)==0x1009);
        drain.write32(0x0d8b0008,0x8000);
        CHECK(drain.read32(0x0d8b0008)==0x8000 && drain.read32(0x10000500)==0x12345678);
        CHECK((drain.read32(0x0d010000)&0x80000000) && !(drain.ipc().arm_flags&2));
        drain.write32(0x0d800134,0);CHECK(drain.read32(0x0d80018c)==0x1000);
        drain.write32(0x0d800134,0x400);CHECK(drain.read32(0x0d80018c)==0x1009);
        drain.write32(0x0d800188,0x02010000);CHECK(drain.read32(0x0d80018c)==0x1000);
        drain.write32(0x0d800188,0x02000000);CHECK(drain.read32(0x0d80018c)==0x1009);
        CHECK(faults([&]{drain.write32(0x0d800188,0x02000001);}));
        CHECK(drain.read32(0x0d800188)==0x02000000 && drain.read32(0x0d80018c)==0x1009);
        CHECK(faults([&]{drain.write32(0x0d80018c,0x1000);})); /* Status is not a writable ACK. */
        drain.write32(0x0d800188,0);CHECK(drain.read32(0x0d80018c)==0x1000);
        CHECK(!drain.ipc().arm_flags && !drain.ipc().ppc_flags && !drain.ipc().x && !drain.ipc().y);
        drain.advance(24299);CHECK(drain.read32(0x0d010000)&0x80000000);
        drain.advance(1);CHECK(!(drain.read32(0x0d010000)&0x80000000) && (drain.ipc().arm_flags&2));
    }
    {
        wiillr::Starlet hash(mem1,mem2);
        const uint32_t seed[]={0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
        const uint32_t expected[]={0xa9993e36,0x4706816a,0xba3e2571,0x7850c26c,0x9cd0d89d};
        for(unsigned i=0;i<5;++i)hash.write32(0x0d030008+i*4,seed[i]);
        std::fill_n(mem2.data()+0x1000,64,0);hash.write32(0x10001000,0x61626380);hash.write32(0x1000103c,24);
        hash.write32(0x0d030004,0x10001000);hash.write32(0x0d80003c,8);hash.write32(0x0d030000,0xc0000000);
        hash.advance(wiillr::Sha1Engine::block_cycles-1);
        CHECK(hash.read32(0x0d030008)==seed[0] && !wii_ipc_arm_interrupt(&hash.ipc()));
        hash.advance(1);CHECK(wii_ipc_arm_interrupt(&hash.ipc()));
        for(unsigned i=0;i<5;++i)CHECK(hash.read32(0x0d030008+i*4)==expected[i]);
        CHECK(hash.read32(0x0d030004)==0x10001040 && !(hash.read32(0x0d030000)&0x80000000));
        hash.write32(0x0d800038,8);hash.advance(100);CHECK(!wii_ipc_arm_interrupt(&hash.ipc()));
        CHECK(faults([&]{hash.write16(0x0d030008,0);}));
        CHECK(faults([&]{hash.write32(0x0d03001c,0);}));
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
    /* IOS DI permission changes only the DVDVIDEO control latch. It does
     * not send a drive command or create an IPC/interrupt completion. */
    const auto arm_flags=machine.ipc().arm_flags,ppc_flags=machine.ipc().ppc_flags;
    const auto ipc_x=machine.ipc().x,ipc_y=machine.ipc().y;
    machine.write32(0x0d800180,0x200106);
    CHECK(machine.read32(0x0d800180)==0x200106);
    machine.write32(0x0d800180,0x106);
    CHECK(machine.read32(0x0d800180)==0x106);
    CHECK(faults([&]{machine.write32(0x0d800180,0x400106);}));
    CHECK(machine.read32(0x0d800180)==0x106);
    CHECK(machine.ipc().arm_flags==arm_flags && machine.ipc().ppc_flags==ppc_flags);
    CHECK(machine.ipc().x==ipc_x && machine.ipc().y==ipc_y);
    CHECK(faults([&]{machine.write32(0x0d80601c,1);})); /* No drive transfer. */
    WiiHollywood native{};wii_hollywood_init(&native);
    CHECK(!wii_hollywood_write(&native,0x0d800180,native.compat^0x200000));
    return 0;
}
