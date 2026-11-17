/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "memory.hpp"
#include "pointer.hpp"
#include "wiillr/pointer.h"
#include <array>
#include <cstdio>
#define CHECK(x) do {if(!(x)) {std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}} while(0)
struct Banks {std::array<uint8_t,64> first{},second{};};
static int resolve(void* user,uint32_t address,uint32_t size,GcnllrNativeGxMemory* out) {
    auto& b=*static_cast<Banks*>(user);
    const auto physical=address&0x1fffffffu;
    for(auto base:{0u,0x10000000u})if(physical>=base && physical-base<64 && size<=64-(physical-base)) {
        const auto offset=physical-base;
        *out={(base ? b.second.data():b.first.data())+offset,physical,64-offset};return 1;
    }
    return 0;
}
static int locate(void* user,const void* pointer,uint32_t size,uint32_t* address) {
    auto& b=*static_cast<Banks*>(user);
    for(auto base:{0u,0x10000000u}) {
        auto first=reinterpret_cast<uintptr_t>(base ? b.second.data():b.first.data());
        auto at=reinterpret_cast<uintptr_t>(pointer);
        if(at>=first && at-first<64 && size<=64-(at-first)) {*address=base+uint32_t(at-first);return 1;}
    }
    return 0;
}
int main() {
    Banks banks;GcnllrNativeGxMemory memory{};uint32_t address;
    GcnllrNativeGxConfig config{};
    config.mem1=banks.first.data();config.mem1_size=64;
    CHECK(gcnllr::gx_memory(config,0xc000003c,4,memory));
    CHECK(memory.bytes==banks.first.data()+60 && memory.physical_address==60 && memory.available==4);
    CHECK(!gcnllr::gx_memory(config,0x20000000,4,memory));
    CHECK(!gcnllr::gx_memory(config,0x8000003f,2,memory));
    config.mem1=nullptr;config.mem1_size=0;config.user=&banks;
    config.resolve_memory=resolve;config.locate_memory=locate;
    CHECK(gcnllr::gx_memory(config,0x90000020,4,memory));
    memory.bytes[0]=0x73;
    CHECK(banks.second[32]==0x73 && banks.first[32]==0);
    CHECK(memory.physical_address==0x10000020);
    CHECK(gcnllr::gx_address(config,banks.second.data()+32,4,address) && address==0x10000020);
    CHECK(gcnllr::gx_memory(config,0xd0000020,4,memory) && memory.bytes==banks.second.data()+32);
    CHECK(!gcnllr::gx_memory(config,0x1000003f,2,memory));
    CHECK(!gcnllr::gx_address(config,banks.second.data()+63,2,address));
    CHECK(!gcnllr::gx_memory(config,0x08000000,4,memory));
    config.resolve_memory=+[](void*,uint32_t,uint32_t,GcnllrNativeGxMemory* out) {
        static uint8_t byte;*out={&byte,0xfffffff0,32};return 1;
    };
    CHECK(!gcnllr::gx_memory(config,0,4,memory));
    auto pointer=gcnllr::image_pointer(400,300,3,800,600,1600,1200,800,600);
    CHECK(pointer.valid && pointer.x==32768 && pointer.y==32768 && pointer.buttons==3);
    pointer=gcnllr::image_pointer(160,360,4,1280,720,1280,720,960,720);
    CHECK(pointer.valid && pointer.x==0 && pointer.y==32768 && pointer.buttons==4);
    CHECK(!gcnllr::image_pointer(159,360,1,1280,720,1280,720,960,720).valid);
    CHECK(!gcnllr::image_pointer(1120,360,1,1280,720,1280,720,960,720).valid);
    pointer=gcnllr::image_pointer(300,450,0,600,900,1200,1800,600,450);
    CHECK(pointer.valid && pointer.x==32768 && pointer.y==32768);
    CHECK(!gcnllr::image_pointer(300,224,1,600,900,1200,1800,600,450).valid);
    CHECK(!gcnllr::image_pointer(-1,300,1,800,600,800,600,800,600).valid);
    CHECK(!gcnllr::image_pointer(NAN,300,1,800,600,800,600,800,600).valid);
    CHECK(!gcnllr::image_pointer(400,300,1,0,600,800,600,800,600).valid);
    auto ir=wii_pointer_camera(32768,32768);CHECK(ir.x==512 && ir.y==280);
    ir=wii_pointer_camera(0,0);CHECK(ir.x==762 && ir.y==93);
    ir=wii_pointer_camera(65535,65535);CHECK(ir.x==262 && ir.y==467);
    ir=wii_pointer_camera(16384,16384);CHECK(ir.x==637 && ir.y==187);
    ir=wii_pointer_camera_view(32768,32768,true);CHECK(ir.x==512 && ir.y==280);
    ir=wii_pointer_camera_view(0,0,true);CHECK(ir.x==812 && ir.y==93);
    ir=wii_pointer_camera_view(65535,65535,true);CHECK(ir.x==212 && ir.y==467);
    ir=wii_pointer_camera_view(62258,25151,true);CHECK(ir.x==242 && ir.y==237);
    /* Both IR dots remain valid across the complete host mouse range. */
    for(unsigned n=0;n<=65535;n++) {
        ir=wii_pointer_camera_view(uint16_t(n),uint16_t(n),true);
        CHECK(ir.x>=100 && ir.x<=923 && ir.y<=767);
    }
    return 0;
}
