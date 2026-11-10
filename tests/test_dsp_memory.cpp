/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "Common/CommonTypes.h"
#include "engine_internal.hpp"
#include "Core/DSP/DSPHost.h"
#include <cstdio>
#include <stdexcept>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
struct Observations { unsigned prepared{},written{};uint32_t address{},bytes{}; };
static bool prepare(void* p,uint32_t at,uint32_t bytes,bool) {
    auto& o=*static_cast<Observations*>(p);++o.prepared;o.address=at;o.bytes=bytes;return true;
}
static bool written(void* p,uint32_t at,uint32_t bytes) {
    auto& o=*static_cast<Observations*>(p);++o.written;o.address=at;o.bytes=bytes;return true;
}
int main() {
    uint8_t mem1[64]{},mem2[64]{},aram[64]{};
    Observations o;
    GcnDspEngine fixture;
    auto& c=fixture.config;
    c.mem1=mem1;c.mem1_size=sizeof mem1;c.mem2=mem2;c.mem2_size=sizeof mem2;
    c.aram=aram;c.aram_size=sizeof aram;c.wii=true;
    c.user=&o;c.prepare=prepare;c.written=written;
    gcn_dsp_owner=&fixture;
    CHECK(DSP::Host::IsWiiHost());
    mem1[3]=0x42;aram[3]=0x11;
    CHECK(DSP::Host::ReadHostMemory(3)==0x42);
    DSP::Host::WriteHostMemory(0x5a,0x10000003);
    CHECK(mem2[3]==0x5a && o.address==0x10000003 && o.written==1);
    mem2[4]=0x12;mem2[5]=0x34;mem2[6]=0xab;mem2[7]=0xcd;
    uint16_t words[3]={0,0,0xdddd};
    DSP::Host::DMAToDSP(words,0x10000004,5);
    CHECK(words[0]==0x1234 && words[1]==0xabcd && words[2]==0xdddd);
    CHECK(o.address==0x10000004 && o.bytes==4);
    words[0]=0x9876;words[1]=0x5432;
    DSP::Host::DMAFromDSP(words,0x10000008,4);
    CHECK(mem2[8]==0x98 && mem2[9]==0x76 && mem2[10]==0x54 && mem2[11]==0x32);
    CHECK(o.written==2 && o.address==0x10000008);
    bool rejected=false;
    try { DSP::Host::DMAToDSP(words,0x1000003f,2); } catch(const std::runtime_error&) { rejected=true; }
    CHECK(rejected);
    c.wii=false;
    CHECK(!DSP::Host::IsWiiHost());
    CHECK(DSP::Host::ReadHostMemory(3)==0x11);
    gcn_dsp_owner=nullptr;
    return 0;
}
