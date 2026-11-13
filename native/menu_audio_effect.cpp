/* SPDX-License-Identifier: MIT */
#include "wiillr/menu_audio_effect.h"
extern "C" {
#include "cpu/cpu.h"
#include "memory/memory.h"
}
#include <array>
#include <cfenv>
#include <cmath>
#include <cstring>
#include <limits>
#include <initializer_list>

namespace {
/* Verified original callback 81555FC8: retain its setup and epilogue, replacing
 * only whole iterations of the three-channel sample loop below. Each accepted
 * sample reproduces all ring/PCM/stack stores and continuation-live registers.
 * Finite-domain admission and a write journal keep the original native body
 * available at the first unsupported sample or device deadline. */
constexpr uint32_t ENTRY=0x81556084,END=0x815564cc;
struct Environment {
    fenv_t saved{};bool valid=std::fegetenv(&saved)==0;
    ~Environment(){if(valid)std::fesetenv(&saved);}
};
struct Reader {
    CPUState* cpu;bool valid=true;
    const uint8_t* data(uint32_t address,uint32_t size) {
        uint32_t available=0;const auto* p=gcn_mem_resolve(cpu,address,&available);
        const uint32_t physical=address&0x1fffffffu;
        const bool ram=physical<cpu->ram_size || (physical>=0x10000000 && physical-0x10000000<cpu->mem2_size);
        if(!ram || !p || size>available || (address&3)){valid=false;return nullptr;}
        return p;
    }
    bool span(uint32_t address,uint32_t size){return data(address,size)!=nullptr;}
    uint32_t word(uint32_t address) {
        const auto* p=data(address,4);return p?read_be32(p):0;
    }
    static double decoded(const uint8_t* p,bool& valid) {
        const uint32_t u=read_be32(p),exp=(u>>23)&255,mantissa=u&0x7fffff;
        if(exp==255){valid=false;return 0;}
        /* Preserve scalar LFS denormals without depending on host DAZ. */
        if(!exp)return std::copysign(double(mantissa)*0x1p-149,(u>>31)?-1.:1.);
        float f;std::memcpy(&f,&u,4);return double(f);
    }
    double real(uint32_t address){const auto* p=data(address,4);return p?decoded(p,valid):0;}
};
struct Span {uint32_t address,size;};
bool separate(Span a,Span b) {
    const uint64_t p=a.address&0x1fffffffu,q=b.address&0x1fffffffu;
    return p+a.size<=q || q+b.size<=p;
}
struct Ops {
    bool ni,valid=true,inexact=false,track=false;
    double round(double value) {
        if(!std::isfinite(value)){valid=false;return 0;}
        if(ni && std::fabs(value)<0x1p-126){if(track && value!=0)inexact=true;return std::copysign(0.,value);}
        volatile float rounded=float(value);
        if(!std::isfinite(rounded)){valid=false;return 0;}
        if(track && double(rounded)!=value)inexact=true;
        return double(rounded);
    }
    double add(double a,double b){volatile double value=a+b;return round(value);}
    double sub(double a,double b){volatile double value=a-b;return round(value);}
    double mul(double a,double b){volatile double value=a*b;return round(value);}
};
uint32_t float_bits(double value){const float f=float(value);uint32_t u;std::memcpy(&u,&f,4);return u;}
struct Write {uint32_t address,value;};
struct Journal {
    std::array<Write,112> entries{};unsigned count=0;bool valid=true;
    void word(uint32_t address,uint32_t value){if(count==entries.size()){valid=false;return;}entries[count++]={address,value};}
    void real(uint32_t address,double value){word(address,float_bits(value));}
    void box(uint32_t address,uint32_t value,double source){word(address,0xfff80000u|(!value && std::signbit(source)?1u:0u));word(address+4,value);}
    void commit(CPUState* cpu) const {for(unsigned n=0;n<count;++n)mem_write32(cpu,entries[n].address,entries[n].value);}
};
struct Metadata {
    uint32_t state,sp,sample;
    bool input,aux;
    uint32_t main[3],input_ptr[3],aux_ptr[3];
    uint32_t delay[3],delay_index[3],delay_length;
    uint32_t pre[3],pre_index,pre_length;
    uint32_t comb[3][3],comb_index[3],comb_length[3];
    uint32_t allpass[3][2],allpass_index[2],allpass_length[2];
    uint32_t last[3],last_index[3],last_length[3];
    double delay_gain[3],comb_gain[3],damping[3],main_gain,aux_gain;
    double damp,gain,one_minus_damp,early,cross;
    /* Resolve the checked buffers once per batch. The original memory bank
     * cannot change before the device deadline; samples use typed views. */
    const uint8_t* main_data[3];const uint8_t* input_data[3];
    const uint8_t* delay_data[3];const uint8_t* pre_data[3];
    const uint8_t* comb_data[3][3];const uint8_t* allpass_data[3][2];const uint8_t* last_data[3];
};
bool prepare(CPUState* c,Metadata& m) {
    if(!c || c->pc!=ENTRY || c->gpr[2]!=0x8169c400 || (c->gpr[1]&7) ||
       !(c->msr&0x2000) || (c->fpscr&0xfb) || (c->fpscr&0x40000000) ||
       bool(c->fpscr&0x20000000)!=bool(c->fpscr&0x01f80700) ||
       c->gpr[0]!=0x43300000 || c->gpr[29] || c->gpr[30]!=3 || c->gpr[31]>=96 ||
       std::fegetround()!=FE_TONEAREST || c->fpr[0]!=0x1.000008p52)return false;
    volatile double exact_tiny=0x1p-149;volatile float rounded_tiny=float(exact_tiny);
    volatile double expanded_tiny=rounded_tiny;
    if(expanded_tiny!=0x1p-149)return false; // Preserve a portable fallback under host FTZ/DAZ.
    for(unsigned k:{1u,3u,4u,5u,6u})if(!std::isfinite(c->fpr[k]) || double(float(c->fpr[k]))!=c->fpr[k])return false;
    Reader r{c};m.state=c->gpr[4];m.sp=c->gpr[1];m.sample=c->gpr[31];
    if(!r.span(m.state,328) || !r.span(m.sp,120) || r.word(m.state+268) || r.word(c->gpr[2]-27900))return false;
    m.input=r.word(m.state+312)!=0;m.aux=r.word(m.state+316)!=0;
    m.delay_length=r.word(m.state+24);m.pre_length=r.word(m.state+60);m.pre_index=r.word(m.state+56);
    m.main_gain=r.real(m.state+320);m.aux_gain=m.aux?r.real(m.state+324):0;
    m.damp=c->fpr[1];m.gain=c->fpr[3];m.one_minus_damp=c->fpr[4];m.early=c->fpr[5];m.cross=c->fpr[6];
    for(unsigned n=0;n<3;++n) {
        m.main[n]=r.word(m.sp+44+n*4);m.input_ptr[n]=m.input?r.word(m.sp+8+n*4):0;
        m.aux_ptr[n]=m.aux?c->gpr[n==0?3:n==1?5:6]:0;
        m.delay[n]=r.word(m.state+n*4);m.delay_index[n]=r.word(m.state+12+n*4);
        m.delay_gain[n]=r.real(m.state+32+n*4);m.comb_gain[n]=r.real(m.state+140+n*4);
        m.pre[n]=m.pre_length?r.word(m.state+44+n*4):0;
        m.comb_index[n]=r.word(m.state+104+n*4);m.comb_length[n]=r.word(m.state+116+n*4);
        m.last[n]=r.word(m.state+200+n*4);m.last_index[n]=r.word(m.state+212+n*4);m.last_length[n]=r.word(m.state+224+n*4);
        m.damping[n]=r.real(m.state+252+n*4);
        for(unsigned j=0;j<3;++j)m.comb[n][j]=r.word(m.state+68+n*12+j*4);
        for(unsigned j=0;j<2;++j)m.allpass[n][j]=r.word(m.state+152+n*8+j*4);
    }
    for(unsigned n=0;n<2;++n){m.allpass_index[n]=r.word(m.state+176+n*4);m.allpass_length[n]=r.word(m.state+184+n*4);}
    if(!r.valid)return false;
    std::array<Span,24> rings{};std::array<Span,9> pcm{};unsigned ring_count=0,pcm_count=0;
    auto ring=[&](uint32_t p,uint32_t length,uint32_t index){
        if(!length || length>1048576 || index>=length || !r.span(p,length*4))return false;
        if(ring_count==rings.size())return false;rings[ring_count++]={p,length*4};return true;
    };
    auto samples=[&](uint32_t p){const uint32_t size=(96-m.sample)*4;if(!r.span(p,size) || pcm_count==pcm.size())return false;pcm[pcm_count++]={p,size};return true;};
    for(unsigned n=0;n<3;++n) {
        if(!samples(m.main[n]) || (m.input && !samples(m.input_ptr[n])) || (m.aux && !samples(m.aux_ptr[n])))return false;
        if(!ring(m.delay[n],m.delay_length,m.delay_index[n]) || !ring(m.last[n],m.last_length[n],m.last_index[n]))return false;
        if(m.pre_length && !ring(m.pre[n],m.pre_length,m.pre_index))return false;
        for(unsigned j=0;j<3;++j)if(!ring(m.comb[n][j],m.comb_length[j],m.comb_index[j]))return false;
        for(unsigned j=0;j<2;++j)if(!ring(m.allpass[n][j],m.allpass_length[j],m.allpass_index[j]))return false;
    }
    const Span state{m.state,328},stack{m.sp,120},code{0x81556000,4096},constant{c->gpr[2]-27900,4};
    if(!separate(state,stack) || !separate(state,code) || !separate(stack,code) || !separate(state,constant) || !separate(stack,constant))return false;
    for(unsigned n=0;n<ring_count;++n) {
        for(Span span:{state,stack,code,constant})if(!separate(rings[n],span))return false;
        for(unsigned j=0;j<n;++j)if(!separate(rings[n],rings[j]))return false;
        for(unsigned j=0;j<pcm_count;++j)if(!separate(rings[n],pcm[j]))return false;
    }
    for(unsigned j=0;j<pcm_count;++j)for(Span other:{state,stack,code,constant})if(!separate(pcm[j],other))return false;
    for(unsigned n=0;n<3;++n) {
        m.main_data[n]=r.data(m.main[n],(96-m.sample)*4);m.input_data[n]=m.input?r.data(m.input_ptr[n],(96-m.sample)*4):nullptr;
        m.delay_data[n]=r.data(m.delay[n],m.delay_length*4);m.pre_data[n]=m.pre_length?r.data(m.pre[n],m.pre_length*4):nullptr;
        m.last_data[n]=r.data(m.last[n],m.last_length[n]*4);
        for(unsigned j=0;j<3;++j)m.comb_data[n][j]=r.data(m.comb[n][j],m.comb_length[j]*4);
        for(unsigned j=0;j<2;++j)m.allpass_data[n][j]=r.data(m.allpass[n][j],m.allpass_length[j]*4);
    }
    return r.valid;
}
struct Sample {
    Journal writes;Metadata next;double out[3],raw[3],sum_lr,last_gain;
    uint32_t main_int[3],aux_int[3],r25,r26,r27,r28,r7,r8;
    uint64_t charge;
    bool inexact;
};
uint64_t sample_charge(const Metadata& m) {
    // Original emitted instruction charge, including the taken wrap branches.
    unsigned wraps=0;
    for(unsigned n=0;n<3;++n) {
        wraps+=m.delay_index[n]+1>=m.delay_length;
        wraps+=m.comb_index[n]+1>=m.comb_length[n];
        wraps+=m.last_index[n]+1>=m.last_length[n];
    }
    for(unsigned n=0;n<2;++n)wraps+=m.allpass_index[n]+1>=m.allpass_length[n];
    if(m.pre_length)wraps+=m.pre_index+1>=m.pre_length;
    return 442+(m.input?18:0)+(m.pre_length?20:0)+(m.aux?21:0)+wraps;
}
bool integer(Ops& op,double value,uint32_t& output) {
    if(!std::isfinite(value) || value<double(INT32_MIN) || value>double(INT32_MAX)){op.valid=false;return false;}
    const double rounded=std::trunc(value);if(op.track && rounded!=value)op.inexact=true;
    output=uint32_t(int32_t(rounded));return true;
}
bool sample(CPUState* c,const Metadata& m,Sample& p) {
    p.next=m;bool valid=true;Ops op{bool(c->fpscr&4)};op.track=!(c->fpscr&0x02000000);unsigned wraps=0;
    auto read=[&](const uint8_t* p,uint32_t i){return Reader::decoded(p+i*4,valid);};
    auto advance=[&](uint32_t index,uint32_t length,uint32_t address){const uint32_t next=index+1;if(next>=length)++wraps;const uint32_t value=next<length?next:0;p.writes.word(address,value);return value;};
    for(unsigned ch=0;ch<3;++ch) {
        uint32_t input=read_be32(m.main_data[ch]);
        if(m.input){input+=read_be32(m.input_data[ch]);p.next.input_ptr[ch]+=4;p.next.input_data[ch]+=4;p.writes.word(m.sp+8+ch*4,p.next.input_ptr[ch]);}
        const double in=op.round(double(int32_t(input)));
        const double a=read(m.delay_data[ch],m.delay_index[0]),b=read(m.delay_data[ch],m.delay_index[1]),d=read(m.delay_data[ch],m.delay_index[2]);
        double early=op.add(op.mul(m.delay_gain[0],a),op.mul(m.delay_gain[1],b));
        p.writes.real(m.delay[ch]+m.delay_index[2]*4,in);early=op.add(op.mul(m.delay_gain[2],d),early);
        const double pre=m.pre_length?read(m.pre_data[ch],m.pre_index):in;
        if(m.pre_length)p.writes.real(m.pre[ch]+m.pre_index*4,in);
        double sum=0;
        for(unsigned j=0;j<3;++j){const double old=read(m.comb_data[ch][j],m.comb_index[j]);sum=op.add(sum,old);p.writes.real(m.comb[ch][j]+m.comb_index[j]*4,op.add(op.mul(old,m.comb_gain[j]),pre));}
        for(unsigned j=0;j<2;++j){const double old=read(m.allpass_data[ch][j],m.allpass_index[j]);const double value=op.add(op.mul(old,m.gain),sum);p.writes.real(m.allpass[ch][j]+m.allpass_index[j]*4,value);sum=op.sub(old,op.mul(value,m.gain));}
        const double damp=op.add(op.mul(m.one_minus_damp,sum),op.mul(m.damp,m.damping[ch]));
        p.next.damping[ch]=damp;p.writes.real(m.state+252+ch*4,damp);
        const double old=read(m.last_data[ch],m.last_index[ch]);const double value=op.add(op.mul(old,m.gain),damp);
        p.writes.real(m.last[ch]+m.last_index[ch]*4,value);const double late=op.sub(old,op.mul(value,m.gain));
        p.writes.real(m.sp+20+ch*4,late);
        p.next.last_index[ch]=advance(m.last_index[ch],m.last_length[ch],m.state+212+ch*4);
        p.out[ch]=op.add(op.mul(late,m.early),early);p.writes.real(m.sp+20+ch*4,p.out[ch]);
        if(ch==2){p.r25=m.allpass[ch][0];p.r26=m.allpass[ch][1];p.r27=m.last_index[ch]+1;p.r28=m.last_length[ch];}
    }
    p.sum_lr=op.add(p.out[0],p.out[1]);
    p.raw[0]=op.add(p.out[0],op.mul(op.add(p.out[1],p.out[2]),m.cross));
    p.raw[1]=op.add(p.out[1],op.mul(op.add(p.out[0],p.out[2]),m.cross));
    p.raw[2]=op.add(p.out[2],op.mul(p.sum_lr,m.cross));
    for(unsigned ch=0;ch<3;++ch) {
        p.writes.real(m.sp+32+ch*4,p.raw[ch]);
        const double scaled=op.mul(p.raw[ch],m.main_gain);if(!integer(op,scaled,p.main_int[ch]))return false;
        p.writes.box(m.sp+(ch==0?64:ch==1?56:72),p.main_int[ch],scaled);p.writes.word(m.main[ch],p.main_int[ch]);
        p.next.main[ch]+=4;p.next.main_data[ch]+=4;p.writes.word(m.sp+44+ch*4,p.next.main[ch]);
    }
    if(m.aux)for(unsigned ch=0;ch<3;++ch) {
        const double scaled=op.mul(p.raw[ch],m.aux_gain);if(!integer(op,scaled,p.aux_int[ch]))return false;
        p.writes.box(m.sp+(ch==0?72:ch==1?64:56),p.aux_int[ch],scaled);p.writes.word(m.aux_ptr[ch],p.aux_int[ch]);p.next.aux_ptr[ch]+=4;
    }
    for(unsigned j=0;j<3;++j)p.next.delay_index[j]=advance(m.delay_index[j],m.delay_length,m.state+12+j*4);
    if(m.pre_length)p.next.pre_index=advance(m.pre_index,m.pre_length,m.state+56);
    for(unsigned j=0;j<3;++j)p.next.comb_index[j]=advance(m.comb_index[j],m.comb_length[j],m.state+104+j*4);
    for(unsigned j=0;j<2;++j)p.next.allpass_index[j]=advance(m.allpass_index[j],m.allpass_length[j],m.state+176+j*4);
    p.next.sample++;p.r7=m.allpass_length[1];p.r8=m.allpass_index[1]+1;p.last_gain=m.aux?m.aux_gain:m.main_gain;
    p.charge=442+(m.input?18:0)+(m.pre_length?20:0)+(m.aux?21:0)+wraps;
    p.inexact=op.inexact;
    return valid && op.valid && p.writes.valid;
}
void publish(CPUState* c,const Metadata& before,const Sample& p) {
    /* All operands/results are finite and output conversions fit signed32.
     * With exception enables clear, only sticky inexact can accumulate.
     * Replay the final scalar multiply/conversion for FI/FR/FPRF precisely. */
    if(p.inexact){if(!(c->fpscr&0x02000000))c->fpscr|=0x80000000;c->fpscr|=0x02000000;ppc_fpscr_updated(c);}
    c->gpr[7]=p.r7;c->gpr[8]=p.r8;c->gpr[9]=before.state+8;c->gpr[10]=before.main[1];
    c->gpr[11]=p.main_int[0];c->gpr[12]=before.main[0];
    c->gpr[25]=p.r25;c->gpr[26]=p.r26;c->gpr[27]=p.r27;c->gpr[28]=p.r28;c->gpr[31]=p.next.sample;
    if(before.aux){c->gpr[3]=p.next.aux_ptr[0];c->gpr[5]=p.next.aux_ptr[1];c->gpr[6]=p.next.aux_ptr[2];}
    const double values[]={p.raw[2],p.raw[1],p.raw[0],p.out[0],p.out[2],p.out[1],p.sum_lr};
    for(unsigned n=0;n<7;++n)c->fpr[7+n]=c->ps1[7+n]=values[n];
    c->fpr[2]=c->ps1[2]=p.last_gain;ppc_fmuls(c,2,7,2);uint64_t box;
    if(ppc_fctiw(c,c->fpr[2],true,&box))std::memcpy(c->fpr+2,&box,8);
    c->ctr=0;c->cr=(c->cr&0x0fffffff)|(((p.next.sample<96?8u:2u)|((c->xer>>31)&1u))<<28);
    c->pc=p.next.sample<96?ENTRY:END;
}
}
extern "C" uint32_t wii_menu_audio_effect_batch(CPUState* c) {
    if(!c || c->cycles>c->cycle_deadline || c->cycle_deadline-c->cycles<442)return 0;
    Environment environment;if(!environment.valid)return 0;
    Metadata m{};if(!prepare(c,m))return 0;uint32_t count=0;
    while(m.sample<96) {
        const uint64_t charge=sample_charge(m);
        if(c->cycles>c->cycle_deadline || charge>c->cycle_deadline-c->cycles)break;
        /* Compute without guest writes; an unsupported next sample leaves
         * its original instruction stream and all intermediate data intact.
         * Check the metadata-derived charge before reading DMA sample data. */
        Sample p{};if(!sample(c,m,p) || p.charge!=charge || c->cycles>c->cycle_deadline || charge>c->cycle_deadline-c->cycles)break;
        p.writes.commit(c);publish(c,m,p);c->cycles+=charge;m=p.next;++count;
    }
    return count;
}
