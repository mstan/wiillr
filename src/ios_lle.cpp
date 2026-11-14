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
#include <memory>

struct WiiIosLle {
    wiillr::Starlet machine;
    uint64_t origin,last_ppc,instructions=0;
    std::string error;
    WiiIosLle(CPUState& cpu,WiiIpc& ipc,uint64_t start):
        machine({cpu.ram,cpu.ram_size},{cpu.mem2,cpu.mem2_size}),origin(start),last_ppc(start) {
        machine.attach_ipc(ipc,start);
        machine.observe_stores([](void* user,uint32_t address,uint32_t size) {
            auto& ppc=*static_cast<CPUState*>(user);
            if(!size)return;
            if(address>=ppc.ram_size && !(address>=0x10000000u && address-0x10000000u<ppc.mem2_size))return;
            const uint32_t key=gcn_reservation_key(ppc.reserve_addr)&~31u;
            if(ppc.reserve_valid && key>=(address&~31u) && key<=((address+size-1)&~31u))ppc.reserve_valid=false;
            gcn_native_code_invalidate(address,size);
        },&cpu);
    }
};
extern "C" WiiIosLle* wii_ios_lle_create(CPUState* cpu,WiiIpc* ipc,WiiHollywood* hardware,const uint8_t* ios,size_t ios_size,
    const uint8_t* otp,size_t otp_size,uint64_t origin) {
    if(!cpu || !ipc || !hardware || !ios || !ios_size || (!otp && otp_size) ||
       !cpu->ram || cpu->ram_size!=GC_MAIN_RAM_SIZE || !cpu->mem2 || cpu->mem2_size!=WII_MEM2_SIZE)return nullptr;
    try {
        auto context=std::make_unique<WiiIosLle>(*cpu,*ipc,origin);
        if(otp_size)context->machine.load_otp({otp,otp_size});
        context->machine.load_ios({ios,ios_size});
        context->machine.attach_hollywood(*hardware);
        return context.release();
    }catch(const std::exception& error) {std::fprintf(stderr,"original IOS initialization: %s\n",error.what());return nullptr;}
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
extern "C" WiiIosLleStatus wii_ios_lle_status(const WiiIosLle* context) {
    if(!context) {WiiIosLleStatus status{};status.stopped=true;return status;}
    const auto& m=context->machine;
    return {context->instructions,m.cycles(),m.swi_entries(),m.irq_entries(),m.undefined_entries(),m.cpu.R[15],!context->error.empty()};
}
extern "C" const char* wii_ios_lle_error(const WiiIosLle* context) {
    return context?context->error.c_str():"Original IOS has not been attached";
}
extern "C" void wii_ios_lle_destroy(WiiIosLle* context) {delete context;}
