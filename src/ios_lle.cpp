/* SPDX-License-Identifier: MIT */
#include "wiillr/ios_lle.h"
#include "wiillr/starlet.hpp"
extern "C" {
#include "cpu/cpu.h"
#include "cpu/native_code.h"
#include "memory/memory.h"
}
#include <string>
#include <cstdio>
#include <stdexcept>
#include <memory>
#include <vector>

struct WiiIosLle {
    wiillr::Starlet machine;
    CPUState* ppc;
    GcnDi* di=nullptr;
    uint64_t origin,last_ppc,instructions=0;
    std::string error;
    WiiIosLle(CPUState& cpu,WiiIpc& ipc,uint64_t start):
        machine({cpu.ram,cpu.ram_size},{cpu.mem2,cpu.mem2_size}),ppc(&cpu),origin(start),last_ppc(start) {
        machine.attach_ipc(ipc,start);
        machine.observe_stores([](void* user,uint32_t address,uint32_t size) {
            auto& ppc=*static_cast<CPUState*>(user);
            if(!size)return;
            const bool boot=address>=wiillr::ExiBootMemory::ppc_base &&
                uint64_t(address)+size<=uint64_t(wiillr::ExiBootMemory::ppc_base)+wiillr::ExiBootMemory::size;
            if(!boot && address>=ppc.ram_size && !(address>=0x10000000u && address-0x10000000u<ppc.mem2_size))return;
            const uint32_t key=gcn_reservation_key(ppc.reserve_addr)&~31u;
            if(ppc.reserve_valid && key>=(address&~31u) && key<=((address+size-1)&~31u))ppc.reserve_valid=false;
            gcn_native_code_invalidate(address,size);
        },&cpu);
    }
};
extern "C" bool wii_ios_lle_attach_di(WiiIosLle* context,GcnDi* di) {
    if(!context || !di || context->di || context->instructions || !context->error.empty() ||
       di->core_clock!=729000000u || di->dma_address_mask!=~0x1fu)return false;
    if(!context->machine.attach_di({context,
        [](void* p,uint32_t offset) {
            auto& c=*static_cast<WiiIosLle*>(p);
            return gcn_di_read(c.di,c.ppc,GCN_DI_BASE+offset,4);
        },[](void* p,uint32_t offset,uint32_t value) {
            auto& c=*static_cast<WiiIosLle*>(p);
            gcn_di_write(c.di,c.ppc,GCN_DI_BASE+offset,value,4);
        },[](void* p) {return static_cast<WiiIosLle*>(p)->di->irq_level!=0;}}))return false;
    context->di=di;return true;
}
extern "C" WiiIosLle* wii_ios_lle_create(CPUState* cpu,WiiIpc* ipc,WiiHollywood* hardware,const uint8_t* ios,size_t ios_size,
    const uint8_t* otp,size_t otp_size,uint64_t origin) {
    if(!cpu || !ipc || !hardware || !ios || !ios_size || (!otp && otp_size) ||
       !cpu->ram || cpu->ram_size!=GC_MAIN_RAM_SIZE || !cpu->mem2 || cpu->mem2_size!=WII_MEM2_SIZE)return nullptr;
    try {
        if(cpu->rom_window && cpu->rom_window_size)
            throw std::runtime_error("Original Wii IOS cannot overlay a GameCube ROM window");
        auto context=std::make_unique<WiiIosLle>(*cpu,*ipc,origin);
        if(otp_size)context->machine.load_otp({otp,otp_size});
        context->machine.load_ios({ios,ios_size});
        context->machine.attach_hollywood(*hardware);
        return context.release();
    }catch(const std::exception& error) {std::fprintf(stderr,"original IOS initialization: %s\n",error.what());return nullptr;}
}
extern "C" bool wii_ios_lle_read_ppc_boot(const WiiIosLle* context,uint32_t address,uint8_t width,uint32_t* value) {
    return context && value && context->machine.read_ppc_boot(address,width,*value);
}
extern "C" bool wii_ios_lle_tick(WiiIosLle* context,uint64_t ppc_cycles) {
    if(!context || !context->error.empty())return false;
    if(ppc_cycles<context->last_ppc) {context->error="Original IOS requires monotonic PPC time";return false;}
    context->last_ppc=ppc_cycles;
    try {
        const uint64_t target=(ppc_cycles-context->origin)/3; /* Broadway729MHz : IOP243MHz. */
        while(context->machine.cycles()<target) {
            context->machine.step();++context->instructions;
        }
        return true;
    }catch(const std::exception& error) {context->error=error.what();return false;}
}
extern "C" bool wii_ios_lle_load_seeprom(WiiIosLle* context,const uint8_t* bytes,size_t size) {
    if(!context || !bytes || size!=256 || context->instructions || !context->error.empty())return false;
    try {context->machine.load_seeprom({bytes,size});return true;}
    catch(const std::exception&) {return false;}
}
extern "C" bool wii_ios_lle_load_nand(WiiIosLle* context,const uint8_t* bytes,size_t size) {
    if(!context || !bytes || (size!=0x21000000u && size!=0x21000400u) || context->instructions || !context->error.empty())return false;
    try {context->machine.load_nand(std::vector<uint8_t>(bytes,bytes+0x21000000u));return true;}
    catch(const std::exception&) {return false;}
}
extern "C" WiiIosLleStatus wii_ios_lle_status(const WiiIosLle* context) {
    if(!context) {WiiIosLleStatus status{};status.stopped=true;return status;}
    const auto& m=context->machine;
    return {context->instructions,m.cycles(),m.swi_entries(),m.irq_entries(),m.undefined_entries(),m.cpu.R[15],!context->error.empty()};
}
extern "C" const char* wii_ios_lle_error(const WiiIosLle* context) {
    return context?context->error.c_str():"Original IOS has not been attached";
}
extern "C" void wii_ios_lle_destroy(WiiIosLle* context) {delete context;}
