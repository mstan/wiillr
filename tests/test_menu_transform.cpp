// SPDX-License-Identifier: MIT
#include "wiillr/menu_transform.h"
extern "C" {
#include "cpu/cpu.h"
#include "memory/memory.h"
}
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
namespace {
constexpr uint32_t P=0x80022000,I=0x80021000,S=0x80020000,Q=0x80022400;
uint8_t* bytes(CPUState& c,uint32_t a){uint32_t size;return gcn_mem_resolve(&c,a,&size);}
void word(CPUState& c,uint32_t a,uint32_t v){write_be32(bytes(c,a),v);}
void real(CPUState& c,uint32_t a,float f){uint32_t bits;std::memcpy(&bits,&f,4);word(c,a,bits);}
bool unchanged_rejection(CPUState& c) {
    const CPUState before=c;const std::vector<uint8_t> ram(c.ram,c.ram+c.ram_size);
    const int flags=std::fetestexcept(FE_ALL_EXCEPT);
    return !wii_menu_transform_apply(&c) && !std::memcmp(&c,&before,sizeof c) &&
        !std::memcmp(c.ram,ram.data(),ram.size()) && flags==std::fetestexcept(FE_ALL_EXCEPT);
}
}
int main() {
    CPUState c{};CHECK(cpu_init(&c));c.msr=0x2000;c.hid2=PPC_HID2_PSE|PPC_HID2_LSQE;
    c.pc=0x8151f22c;c.gpr[1]=S;c.gpr[2]=0x8169c400;c.gpr[3]=P;c.gpr[4]=I;
    c.gpr[30]=P;c.gpr[31]=I;c.cycles=100;c.cycle_deadline=10000;
    for(unsigned n=0;n<32;n++){c.fpr[n]=double(n)/7;c.ps1[n]=-double(n)/11;}
    *bytes(c,P+0xcf)=1;*bytes(c,I+0x50)=0x80;
    real(c,P+0x44,2);real(c,P+0x48,3);
    real(c,P+0x2c,5);real(c,P+0x30,6);real(c,P+0x34,7);
    real(c,c.gpr[2]-28176,1);real(c,c.gpr[2]-28484,1);
    real(c,c.gpr[2]-28472,.017453292f);real(c,0x81697edc,1);
    word(c,c.gpr[2]-26056,0x3ff00000); // Original kernel_cos(0) double constant.
    const CPUState initial=c;const std::vector<uint8_t> initial_ram(c.ram,c.ram+c.ram_size);
    WiiMenuTransformPlan plan{};CHECK(wii_menu_transform_plan(&c,&plan));
    CHECK(plan.local[0]==2 && plan.local[5]==3 && plan.local[10]==1);
    CHECK(plan.global[3]==5 && plan.global[7]==6 && plan.global[11]==7);
    CHECK(!std::memcmp(&c,&initial,sizeof c));CHECK(!std::memcmp(c.ram,initial_ram.data(),initial_ram.size()));
    // A short device budget must retain the original interruptible body.
    c.cycle_deadline=c.cycles+626;CHECK(unchanged_rejection(c));
    c.cycle_deadline=c.cycles+627;CHECK(wii_menu_transform_apply(&c));
    CHECK(c.cycles==727 && c.pc==0x8151f3d4 && c.gpr[1]==S);
    CHECK(read_be32(bytes(c,P+0x84))==0x40000000 && read_be32(bytes(c,P+0x90))==0x40a00000);
    CHECK(read_be32(bytes(c,S-112))==S-80); // Original callee scratch remains observable.
    c=initial;std::memcpy(c.ram,initial_ram.data(),initial_ram.size());
    *bytes(c,I+0x50)=0xa0; // Aspect requested, pane excludes it: two extra branch cycles.
    c.cycle_deadline=c.cycles+628;CHECK(unchanged_rejection(c));
    c.cycle_deadline=c.cycles+629;CHECK(wii_menu_transform_apply(&c));CHECK(c.cycles==729);
    c=initial;std::memcpy(c.ram,initial_ram.data(),initial_ram.size());
    c.fpscr=0x82024004; // Original Menu context: nearest, NI, sticky status.
    CHECK(wii_menu_transform_apply(&c));CHECK((c.fpscr&0xff)==4 && c.cycles==727);
    for(unsigned test=0;test<15;test++) {
        c=initial;std::memcpy(c.ram,initial_ram.data(),initial_ram.size());
        switch(test) {
        case 0:word(c,P+0x38,0x80000000);break; // Negative-zero rotation is not certified.
        case 1:real(c,P+0x3c,1);break;
        case 2:c.gpr[1]=P;break;
        case 3:c.gpr[1]=I;break;
        case 4:word(c,P+0xc,Q);c.gpr[1]=Q+0x100;break;
        case 5:c.gpr[4]=P;break;
        case 6:word(c,P+0xc,P-0x30);break;
        case 7:c.gpr[1]=0x8151f100;break;
        case 8:*bytes(c,P+0xcf)=0;*bytes(c,I+0x50)=0;break;
        case 9:c.gqr[0]=1;break;
        case 10:c.fpscr=1;break;
        case 11:*bytes(c,P+0xcf)=5;*bytes(c,I+0x50)=0x20;
            real(c,P+0x44,std::numeric_limits<float>::max());real(c,I+0x44,2);break;
        case 12:real(c,P+0x44,1.e-40f);break; // Original GQR0 denormal flushing retained.
        case 13:word(c,c.gpr[2]-26056,0x40000000);break;
        case 14:c.fpscr=4;*bytes(c,P+0xcf)=5;*bytes(c,I+0x50)=0x20;
            real(c,P+0x44,std::numeric_limits<float>::min());real(c,I+0x44,.5f);break;
        }
        std::feraiseexcept(FE_DIVBYZERO);CHECK(unchanged_rejection(c));std::feclearexcept(FE_ALL_EXCEPT);
    }
    cpu_free(&c);std::puts("Menu transform admission/deadline/alias/FP contract passed");return 0;
}
