/* SPDX-License-Identifier: MIT */
#include "wiillr/menu_transform.h"
extern "C" {
#include "cpu/cpu.h"
#include "memory/memory.h"
}
#include <cfenv>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>

namespace {
struct FloatingEnvironment {
    fenv_t saved{};bool valid=std::fegetenv(&saved)==0;
    ~FloatingEnvironment(){if(valid)std::fesetenv(&saved);}
};
struct Reader {
    CPUState* cpu;bool valid=true;
    uint32_t word(uint32_t address) {
        uint32_t available=0;const auto* p=gcn_mem_resolve(cpu,address,&available);
        if(!p || available<4 || (address&3)){valid=false;return 0;}
        return read_be32(p);
    }
    float real(uint32_t address) {
        const auto bits=word(address);float value;std::memcpy(&value,&bits,4);
        /* GQR0 loads/stores in the original paired path flush denormals.
         * Keep those uncommon values on that original path. */
        if(!std::isfinite(value) || std::fpclassify(value)==FP_SUBNORMAL)valid=false;
        return value;
    }
    void matrix(uint32_t address,float* output){for(unsigned n=0;n<12;++n)output[n]=real(address+n*4);}
    const uint8_t* byte(uint32_t address) {
        uint32_t available=0;const auto* p=gcn_mem_resolve(cpu,address,&available);
        if(!p || !available)valid=false;
        return p && available?p:nullptr;
    }
};
/* Match the retained native emitter's single-precision multiply followed by
 * add, with a distinct rounding point. Do not silently contract into FMA. */
float multiply(float a,float b){volatile float value=a*b;return value;}
float add(float a,float b){volatile float value=a+b;return value;}
float madd(float a,float b,float c){return add(multiply(a,b),c);}
void concat(const float* a,const float* b,float* output) {
    float result[12];
    for(unsigned row=0;row<3;++row)for(unsigned column=0;column<4;++column) {
        float value=multiply(b[column],a[row*4]);
        value=madd(b[4+column],a[row*4+1],value);
        value=madd(b[8+column],a[row*4+2],value);
        /* The actual PS concat performs its homogeneous pair operation for
         * columns2/3, including the zero product in column2. */
        if(column>=2)value=madd(column==3?1.f:0.f,a[row*4+3],value);
        result[row*4+column]=value;
    }
    std::memcpy(output,result,sizeof result);
}
bool separate(uint32_t a,uint32_t size,uint32_t b,uint32_t other) {
    const uint64_t p=a&0x1fffffffu,q=b&0x1fffffffu;
    return p+size<=q || q+other<=p;
}
}
extern "C" bool wii_menu_transform_plan(CPUState* cpu,WiiMenuTransformPlan* output) {
    if(!cpu || !output || !(cpu->msr&0x2000u) || (cpu->hid2&(PPC_HID2_PSE|PPC_HID2_LSQE))!=(PPC_HID2_PSE|PPC_HID2_LSQE) || (cpu->fpscr&0xffu) || cpu->gqr[0] ||
       std::fegetround()!=FE_TONEAREST)return false;
    FloatingEnvironment environment;if(!environment.valid)return false;
    Reader r{cpu};WiiMenuTransformPlan plan{};
    plan.pane=cpu->gpr[30];plan.info=cpu->gpr[31];
    if(!plan.pane || !plan.info || !separate(plan.pane,0xd0,plan.info,0x54))return false;
    /* Only positive-zero angles are certified provisionally. In particular,
     * negative-zero and all animated nonzero rotations retain original code. */
    for(unsigned n=0;n<3;++n)if(r.word(plan.pane+0x38+n*4)!=0)return false;
    const auto* flags=r.byte(plan.info+0x50);
    const auto* pane_flags=r.byte(plan.pane+0xcf);
    if(!flags || !pane_flags)return false;
    plan.info_flags=*flags;plan.pane_flags=*pane_flags;
    plan.scale[0]=r.real(plan.pane+0x44);plan.scale[1]=r.real(plan.pane+0x48);
    std::memcpy(plan.input_scale,plan.scale,sizeof plan.scale);
    if((*flags&0x20) && (*pane_flags&4)) {
        plan.aspect[0]=r.real(plan.info+0x44);plan.aspect[1]=r.real(plan.info+0x48);
        plan.scale[0]=multiply(plan.scale[0],plan.aspect[0]);
        plan.scale[1]=multiply(plan.scale[1],plan.aspect[1]);
    }
    const float scale[12]={plan.scale[0],0,0,0,0,plan.scale[1],0,0,0,0,1,0};
    for(float value:plan.scale)if(!std::isfinite(value) || std::fpclassify(value)==FP_SUBNORMAL)return false;
    const float x[12]={1,0,0,0,0,1,-0.f,0,0,0,1,0};
    const float y[12]={1,0,0,0,0,1,0,0,-0.f,0,1,0};
    const float z[12]={1,-0.f,0,0,0,1,0,0,0,0,1,0};
    float first[12];concat(x,scale,first);concat(y,first,plan.intermediate);
    concat(z,plan.intermediate,plan.composed);std::memcpy(plan.rotation,z,sizeof z);
    std::memcpy(plan.local,plan.composed,sizeof plan.local);
    for(unsigned row=0;row<3;++row){plan.translation[row]=r.real(plan.pane+0x2c+row*4);plan.local[row*4+3]=add(plan.translation[row],plan.composed[row*4+3]);}
    plan.parent=r.word(plan.pane+0xc);float basis[12];
    if(plan.parent) {
        if(!separate(plan.parent+0x84,48,plan.pane+0x54,96))return false;
        r.matrix(plan.parent+0x84,basis);concat(basis,plan.local,plan.global);
    } else if(*flags&0x80)std::memcpy(plan.global,plan.local,sizeof plan.global);
    else {r.matrix(plan.info+4,basis);concat(basis,plan.local,plan.global);}
    for(const auto* matrix:{plan.rotation,plan.intermediate,plan.composed,plan.local,plan.global})
        for(unsigned n=0;n<12;++n)if(!std::isfinite(matrix[n]) || std::fpclassify(matrix[n])==FP_SUBNORMAL)return false;
    if(!r.valid)return false;
    *output=plan;return true;
}

namespace {
uint32_t bits(float value){uint32_t result;std::memcpy(&result,&value,4);return result;}
void store(CPUState* c,uint32_t a,uint32_t value){mem_write32(c,a,value);}
void store_double(CPUState* c,uint32_t a,double value){uint64_t u;std::memcpy(&u,&value,8);mem_write64(c,a,u);}
void store_matrix(CPUState* c,uint32_t a,const float* m,unsigned n=12){for(unsigned k=0;k<n;++k)store(c,a+k*4,bits(m[k]));}
void pair(CPUState* c,unsigned reg,float a,float b){c->fpr[reg]=a;c->ps1[reg]=b;}
void concat_registers(CPUState* c,const float* a,const float* b,const float* out) {
    pair(c,0,out[10],out[11]);pair(c,1,a[2],a[3]);pair(c,2,out[8],out[9]);
    pair(c,3,a[6],a[7]);pair(c,4,a[8],a[9]);pair(c,5,a[10],a[11]);
    for(unsigned k=0;k<6;++k)pair(c,6+k,b[k*2],b[k*2+1]);
    pair(c,12,out[0],out[1]);pair(c,13,out[2],out[3]);
    c->ps1[14]=out[5];c->ps1[15]=out[7];c->ps1[31]=1;
}
}
extern "C" bool wii_menu_transform_apply(CPUState* c) {
    if(!c || c->pc!=0x8151f22cu || c->gpr[2]!=0x8169c400u)return false;
    const auto old30=c->gpr[30],old31=c->gpr[31];
    c->gpr[30]=c->gpr[3];c->gpr[31]=c->gpr[4];
    WiiMenuTransformPlan p{};const bool planned=wii_menu_transform_plan(c,&p);
    c->gpr[30]=old30;c->gpr[31]=old31;
    const auto paired_normal=[](double value){return std::isfinite(value) &&
        (value==0 || std::fabs(value)>=std::numeric_limits<float>::min());};
    if(!planned || (!(p.pane_flags&1) && !(p.info_flags&0x10)) ||
       !paired_normal(c->fpr[31]) || !paired_normal(c->ps1[30]))return false;
    const uint32_t sp=c->gpr[1],size=112+232;
    if((sp&15) || !separate(sp-112,size,p.pane,0xd0) || !separate(sp-112,size,p.info,0x54) ||
       (p.parent && !separate(sp-112,size,p.parent+0x84,48)))return false;
    const uint32_t immutable[][2]={{0x8151f000,4096},{0x81540000,8192},{0x8160a000,4096},{0x8160c000,4096},
        {0x816954bc,0x148},{0x81695e38,8},{0x81697ed8,8}};
    for(const auto& span:immutable)if(!separate(sp-112,size,span[0],span[1]) ||
        !separate(p.pane+0x54,96,span[0],span[1]))return false;
    uint32_t available=0;if(!gcn_mem_resolve(c,sp-112,&available) || available<size)return false;
    const uint64_t charge=(p.parent?650u:(p.info_flags&0x80)?622u:652u)+
        ((p.info_flags&0x20)?((p.pane_flags&4)?10u:2u):0u)+(p.pane_flags&1?5u:8u);
    if(c->cycles>c->cycle_deadline || charge>c->cycle_deadline-c->cycles)return false;
    Reader r{c};
    if(r.word(c->gpr[2]-28172)!=0 || r.word(c->gpr[2]-28176)!=0x3f800000 ||
       r.word(c->gpr[2]-28484)!=0x3f800000 || !(r.real(c->gpr[2]-28472)>0) ||
       r.word(c->gpr[2]-26056)!=0x3ff00000 || r.word(c->gpr[2]-26052)!=0 ||
       r.word(0x81697ed8)!=0 || r.word(0x81697edc)!=0x3f800000 || !r.valid)return false;
    /* Original scalar operations supply the architectural sticky/rounding
     * state. Paired operations in the retained native body do not alter it. */
    if((p.info_flags&0x20)&&(p.pane_flags&4)) {
        c->fpr[3]=p.input_scale[0];c->fpr[2]=p.aspect[0];ppc_fmuls(c,2,3,2);
        c->fpr[1]=p.input_scale[1];c->fpr[0]=p.aspect[1];ppc_fmuls(c,0,1,0);
    }
    c->fpr[3]=p.translation[2];ppc_frsp(c,3,3);
    /* Preserve the actual SDK callees' stack footprint, including their
     * conversion scratch and saved nonvolatile registers. Untouched gaps
     * remain untouched; no captured caller state is restored. */
    store(c,sp-112,sp-80);mem_write64(c,sp-104,0);
    mem_write64(c,sp-96,UINT64_C(0xfff8000000000000));mem_write64(c,sp-88,UINT64_C(0xfff8000000000000));
    store(c,sp-80,sp-48);mem_write64(c,sp-72,0);store(c,sp-64,sp);
    store_double(c,sp-56,c->fpr[14]);store_double(c,sp-48,c->fpr[15]);
    store(c,sp-40,p.pane);store(c,sp-36,p.info);store_double(c,sp-32,c->fpr[30]);
    store_double(c,sp-24,c->fpr[31]);store_double(c,sp-16,c->fpr[31]);
    store(c,sp-8,bits(float(c->fpr[31])));store(c,sp-4,0x3f800000);store(c,sp+4,0x8151f30c);
    store_matrix(c,sp+8,p.scale,2);store_matrix(c,sp+16,p.rotation);
    store_matrix(c,sp+64,p.intermediate);store_matrix(c,sp+112,p.composed);
    store_matrix(c,p.pane+0x54,p.local);store_matrix(c,p.pane+0x84,p.global);
    if(p.parent || !(p.info_flags&0x80)) {
        float basis[12];r.matrix(p.parent?p.parent+0x84:p.info+4,basis);
        concat_registers(c,basis,p.local,p.global);
        c->gpr[0]=p.parent?0x8151f30c:0;c->gpr[3]=p.parent?p.parent+0x84:p.info+4;
        c->gpr[4]=p.pane+0x54;c->gpr[5]=p.pane+0x84;c->gpr[6]=0x81697ed8;
        c->lr=p.parent?0x8151f350:0x8151f3d4;
    }else {
        concat_registers(c,p.rotation,p.intermediate,p.composed);
        for(unsigned k=0;k<3;++k)pair(c,1+k,p.translation[k],p.translation[k]);
        pair(c,4,p.composed[0],p.composed[1]);pair(c,5,p.composed[2],p.local[3]);
        pair(c,6,p.composed[4],p.composed[5]);pair(c,7,p.composed[6],p.local[7]);
        pair(c,8,p.composed[10],p.local[11]);pair(c,9,p.composed[8],p.composed[9]);
        const unsigned regs[]={27,12,11,10,9,8,7,6,5,4,3,0};
        for(unsigned k=0;k<12;++k)c->gpr[regs[k]]=bits(p.local[k]);
        c->lr=0x8151f334;
    }
    /* RotRad's paired save/restore rounds the nonvolatile upper lane. */
    c->ps1[30]=float(c->ps1[30]);c->gpr[30]=p.pane;c->gpr[31]=p.info;
    const uint32_t condition=p.parent?(int32_t(p.parent)<0?8u:4u):(p.info_flags&0x80)?4u:2u;
    c->cr=(c->cr&0x0fffffff)|((condition|((c->xer>>31)&1u))<<28);
    c->pc=0x8151f3d4;c->cycles+=charge;return true;
}
