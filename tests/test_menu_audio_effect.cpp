// SPDX-License-Identifier: MIT
#include "wiillr/menu_audio_effect.h"
extern "C" {
#include "cpu/cpu.h"
#include "memory/memory.h"
}
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
namespace {
constexpr uint32_t S=0x80010008,P=0x80020000;
uint8_t* bytes(CPUState& c,uint32_t a){uint32_t n;return gcn_mem_resolve(&c,a,&n);}
void word(CPUState& c,uint32_t a,uint32_t v){write_be32(bytes(c,a),v);}
bool unchanged(CPUState& c) {
    const auto before=c;const std::vector<uint8_t> ram(c.ram,c.ram+c.ram_size);
    const std::vector<uint8_t> mem2(c.mem2,c.mem2+c.mem2_size);const int flags=std::fetestexcept(FE_ALL_EXCEPT);
    return !wii_menu_audio_effect_batch(&c) && !std::memcmp(&c,&before,sizeof c) &&
        !std::memcmp(c.ram,ram.data(),ram.size()) && !std::memcmp(c.mem2,mem2.data(),mem2.size()) && flags==std::fetestexcept(FE_ALL_EXCEPT);
}
}
int main() {
    CHECK(wii_menu_audio_effect_batch(nullptr)==0);CPUState c{};CHECK(cpu_init(&c));CHECK(cpu_alloc_mem2(&c,0x100000));
    c.pc=0x81556084;c.gpr[0]=0x43300000;c.gpr[1]=S;c.gpr[2]=0x8169c400;c.gpr[4]=P;c.gpr[30]=3;
    c.msr=0x2000;c.fpscr=4;c.fpr[0]=0x1.000008p52;c.fpr[4]=c.ps1[4]=1;c.cycles=100;c.cycle_deadline=100000;
    uint32_t next=0x90010000;
    auto ring=[&](uint32_t field){word(c,field,next);next+=16;};
    word(c,P+24,1);
    for(unsigned j=0;j<3;++j){word(c,P+116+j*4,1);word(c,P+224+j*4,1);}
    for(unsigned j=0;j<2;++j)word(c,P+184+j*4,1);
    for(unsigned ch=0;ch<3;++ch) {
        word(c,S+44+ch*4,0x90020000+ch*0x200);ring(P+ch*4);ring(P+200+ch*4);
        for(unsigned j=0;j<3;++j)ring(P+68+ch*12+j*4);
        for(unsigned j=0;j<2;++j)ring(P+152+ch*8+j*4);
    }
    const auto initial=c;const std::vector<uint8_t> ram(c.ram,c.ram+c.ram_size),mem2(c.mem2,c.mem2+c.mem2_size);
    auto restore=[&](){c=initial;std::memcpy(c.ram,ram.data(),ram.size());std::memcpy(c.mem2,mem2.data(),mem2.size());};
    // All eleven live ring cursors wrap; the original native charge is453.
    c.cycle_deadline=552;CHECK(unchanged(c));c.cycle_deadline=553;CHECK(wii_menu_audio_effect_batch(&c)==1);
    CHECK(c.cycles==553 && c.gpr[31]==1 && c.pc==0x81556084 && c.fpscr==4);
    CHECK(read_be32(bytes(c,S+44))==0x90020004);
    restore();c.cycle_deadline=100+3*453-1;CHECK(wii_menu_audio_effect_batch(&c)==2);
    CHECK(c.cycles==100+2*453 && c.cycles<c.cycle_deadline && c.gpr[31]==2);
    restore();CHECK(wii_menu_audio_effect_batch(&c)==96);CHECK(c.pc==0x815564cc && c.gpr[31]==96 && c.cycles==100+96*453);
    for(unsigned ch=0;ch<3;++ch){CHECK(read_be32(bytes(c,S+44+ch*4))==0x90020000+ch*0x200+384);CHECK(read_be32(bytes(c,P+212+ch*4))==0);}
    for(unsigned test=0;test<13;++test) {
        restore();
        switch(test) {
        case 0:c.fpscr=1;break;
        case 1:c.gpr[31]=96;break;
        case 2:word(c,P,read_be32(bytes(c,P+200)));break; // Aliased delay/all-pass storage.
        case 3:word(c,P+320,0x7fc00001);break;
        case 4:word(c,P+24,0);break;
        case 5:c.cycle_deadline=99;break;
        case 6:c.gpr[1]+=4;break;
        case 7:word(c,P+268,1);break;
        case 8:c.fpscr|=8;break; // Enabled guest exception.
        case 9:c.fpr[0]=0;break;
        case 10:word(c,read_be32(bytes(c,P)),0x7f800000);break;
        case 11:word(c,S+44,0x90100000);break; // Outside the allocated MEM2 bank.
        case 12:word(c,P+320,0x7f7fffff);word(c,P+32,0x3f800000);word(c,read_be32(bytes(c,P)),0x4f000000);break;
        }
        std::feraiseexcept(FE_DIVBYZERO);CHECK(unchanged(c));std::feclearexcept(FE_ALL_EXCEPT);
    }
    // A later unsupported sample commits its valid prefix, then declines
    // without publishing any part of the unsupported sample.
    restore();word(c,P+24,4);word(c,read_be32(bytes(c,P))+8,0x7fc12345);
    CHECK(wii_menu_audio_effect_batch(&c)==2 && c.gpr[31]==2);CHECK(unchanged(c));
    for(int rounding:{FE_DOWNWARD,FE_UPWARD}) {
        restore();CHECK(std::fesetround(rounding)==0);CHECK(unchanged(c));CHECK(std::fegetround()==rounding);
    }
    CHECK(std::fesetround(FE_TONEAREST)==0);
    cpu_free(&c);std::puts("Menu audio sample-batch deadline/MEM2/fallback contract passed");return 0;
}
