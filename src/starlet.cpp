/* SPDX-License-Identifier: MIT */
#include "wiillr/starlet.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <atomic>

namespace {
uint32_t be32(const uint8_t* p) {return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
uint16_t be16(const uint8_t* p) {return uint16_t(p[0])<<8|p[1];}
[[noreturn]] void unsupported(const char* operation,uint32_t pc,uint32_t address) {
    char message[128];
    std::snprintf(message,sizeof message,"Starlet unsupported %s pc=%08x address=%08x",operation,pc,address);
    throw std::runtime_error(message);
}
}
namespace wiillr {
Starlet::Starlet(std::span<uint8_t> mem1,std::span<uint8_t> mem2):mem1_(mem1),mem2_(mem2) {
    wii_hollywood_init(&hollywood_);
    cpu.cpsr.mode=uint8_t(armv4t::Mode::Supervisor);
    cpu.cpsr.i=true;cpu.cpsr.f=true;
}
uint8_t* Starlet::physical(uint32_t address,uint32_t size) {
    if(address<=mem1_.size() && size<=mem1_.size()-address)return mem1_.data()+address;
    if(address>=0x10000000u && address-0x10000000u<=mem2_.size() &&
       size<=mem2_.size()-(address-0x10000000u))return mem2_.data()+address-0x10000000u;
    if(address>=0xffff0000u && uint64_t(address)+size<=0x100000000ull)
        return sram_.data()+(address-0xffff0000u);
    return nullptr;
}
uint32_t Starlet::translate(uint32_t original,bool writing) {
    uint32_t address=original<0x02000000u ? original|fcse_:original;
    if(!(sctl_&1))return address;
    auto table_word=[&](uint32_t at) {
        auto* p=physical(at,4);
        if(!p)unsupported("page-table read",cpu.R[15],at);
        return be32(p);
    };
    const uint32_t first=table_word((ttbr_&0xffffc000u)|((address>>18)&0x3ffcu));
    const uint32_t type=first&3,domain=(first>>5)&15;
    uint32_t result,ap;
    bool section=type==2;
    auto fault=[&](uint32_t status) {
        dfsr_=(domain<<4)|status;far_=original;
        unsupported("MMU fault",cpu.R[15],original);
    };
    if(!type)fault(5);
    const uint32_t access=(dacr_>>(domain*2))&3;
    if(access==0 || access==2)fault(section ? 9:11);
    if(section) {result=(first&0xfff00000u)|(address&0xfffffu);ap=(first>>10)&3;}
    else {
        const bool fine=type==3;
        const uint32_t second=table_word(fine ? (first&0xfffff000u)|((address>>8)&0xffcu):
                                                     (first&0xfffffc00u)|((address>>10)&0x3fcu));
        switch(second&3) {
        case 1:result=(second&0xffff0000u)|(address&0xffffu);ap=(second>>(4+((address>>14)&3)*2))&3;break;
        case 2:result=(second&0xfffff000u)|(address&0xfffu);ap=(second>>(4+((address>>10)&3)*2))&3;break;
        case 3:if(!fine)fault(7);result=(second&0xfffffc00u)|(address&0x3ffu);ap=(second>>4)&3;break;
        default:fault(7);return 0;
        }
    }
    if(access==1) {
        bool privileged=cpu.cpsr.mode!=uint8_t(armv4t::Mode::User),allowed=false;
        switch(ap) {
        case 0: {
            /* ARM DDI0198E table3-12: AP0 never permits writes. S alone
             * permits privileged reads; R alone permits reads in both modes.
             * S+R is unpredictable and deliberately rejected. */
            const uint32_t sr=(sctl_>>8)&3;
            allowed=!writing && ((sr==1 && privileged) || sr==2);
            break;
        }
        case 1:allowed=privileged;break;
        case 2:allowed=privileged || !writing;break;
        case 3:allowed=true;break;
        }
        if(!allowed)fault(section ? 13:15);
    }
    return result;
}
void Starlet::check_alignment(uint32_t address,uint32_t size) {
    if((sctl_&2) && (address&(size-1))) {
        dfsr_=1;far_=address;
        unsupported("alignment fault",cpu.R[15],address);
    }
}
uint32_t Starlet::load_ios(std::span<const uint8_t> b) {
    auto fits=[](uint64_t offset,uint64_t size,uint64_t total) {return offset<=total && size<=total-offset;};
    if(b.size()<16)throw std::runtime_error("Truncated IOS image header");
    const uint64_t offset=uint64_t(be32(b.data()))+be32(b.data()+4),length=be32(b.data()+8);
    if(!fits(offset,length,b.size()) || length<52)throw std::runtime_error("Truncated IOS ELF");
    const auto e=b.subspan(size_t(offset),size_t(length));
    if(std::memcmp(e.data(),"\x7f" "ELF",4) || e[4]!=1 || e[5]!=2 || e[6]!=1 ||
       be16(e.data()+18)!=40 || be32(e.data()+20)!=1 || be16(e.data()+40)!=52)
        throw std::runtime_error("Expected big-endian ARM ELF32 IOS kernel");
    const uint32_t entry=be32(e.data()+24),table=be32(e.data()+28);
    const uint32_t stride=be16(e.data()+42),count=be16(e.data()+44);
    if(stride!=32 || !count || !fits(table,uint64_t(stride)*count,e.size()) || (entry&3))
        throw std::runtime_error("Invalid IOS program-header table/entry");
    bool executable=false;
    for(uint32_t n=0;n<count;++n) {
        const auto* p=e.data()+table+n*stride;
        if(be32(p)!=1)continue;
        const uint32_t at=be32(p+4),dest=be32(p+12),files=be32(p+16),memory=be32(p+20);
        if(files>memory || !fits(at,files,e.size()) || !physical(dest,memory))
            throw std::runtime_error("Invalid IOS loadable segment");
        if((be32(p+24)&1) && entry>=dest && uint64_t(entry)+4<=uint64_t(dest)+files)
            executable=true;
    }
    if(!executable)throw std::runtime_error("IOS entry is not backed by executable bytes");
    /* All validation precedes stores. Zero BSS first so initialized bytes win
     * if a producer deliberately shares a physical page between segments. */
    for(uint32_t n=0;n<count;++n) {
        const auto* p=e.data()+table+n*stride;
        if(be32(p)==1)std::memset(physical(be32(p+12),be32(p+20)),0,be32(p+20));
    }
    for(uint32_t n=0;n<count;++n) {
        const auto* p=e.data()+table+n*stride;
        if(be32(p)==1)std::memcpy(physical(be32(p+12),be32(p+16)),e.data()+be32(p+4),be32(p+16));
    }
    cpu.R[15]=entry;
    return entry;
}
uint8_t Starlet::read8(uint32_t at) {
    auto* p=physical(translate(at,false),1);if(!p)unsupported("read8",cpu.R[15],at);return *p;
}
uint16_t Starlet::read16(uint32_t at) {
    check_alignment(at,2);
    const auto bus_address=translate(at,false);
    if(bus_address==0x0d8b4228u)return flush_request_;
    if(bus_address==0x0d8b422au)return flush_ack_;
    if(bus_address==0x0d8b420au)return protect_enabled_;
    if(bus_address==0x0d8b420cu)return protect_start_;
    if(bus_address==0x0d8b420eu)return protect_end_;
    auto* p=physical(bus_address,2);if(!p)unsupported("read16",cpu.R[15],at);return be16(p);
}
uint32_t Starlet::read32(uint32_t at) {
    check_alignment(at,4);
    const auto bus_address=translate(at,false);
    /* HW_TIMER: free-running AHB/128 counter (243MHz /128). MINI's udelay
     * uses this same approximately1.9MHz timebase; reads do not advance time.
     * Alarm/interrupt routing remains unsupported until actually implemented. */
    if(bus_address==0x0d800010u)return uint32_t(cycles_/128);
    uint32_t hollywood_value;if(wii_hollywood_read(&hollywood_,bus_address,&hollywood_value))return hollywood_value;
    if(bus_address==0x0d800214u)return 0x11; /* selected Hollywood1.1 board profile */
    if(bus_address==0x0d8b0008u)return ahb_flush_mask_;
    if(bus_address==0x0d800070u)return exi_control_;
    if(bus_address==0x0d8b0010u)return ahb_prefetch_;
    if(bus_address==0x0d80018cu)return spare1_;
    if(bus_address>=0x0d800100u && bus_address<=0x0d800144u && !(bus_address&3))
        return arbiter_config_[(bus_address-0x0d800100u)/4];
    auto* p=physical(bus_address,4);if(!p)unsupported("read32",cpu.R[15],at);return be32(p);
}
void Starlet::write8(uint32_t at,uint8_t v) {
    auto* p=physical(translate(at,true),1);if(!p)unsupported("write8",cpu.R[15],at);*p=v;
}
void Starlet::write16(uint32_t at,uint16_t v) {
    check_alignment(at,2);
    const auto bus_address=translate(at,true);
    if(bus_address==0x0d8b420au) {
        /* This isolated IOS-entry boundary holds the PPC in reset. Retain the
         * programmed protection state; a combined machine must enforce these
         * bounds on its PPC bus before releasing that processor. */
        protect_enabled_=v;
        return;
    }
    if(bus_address==0x0d8b420cu) {protect_start_=v;return;}
    if(bus_address==0x0d8b420eu) {protect_end_=v;return;}
    if(bus_address==0x0d8b4228u) {
        /* MEM_FLUSHREQ/ACK (fail0verflow/mini hollywood.h, memory.c).
         * This discovery bus owns coherent arrays and has no deferred device
         * writers. Publish completed stores before acknowledging the barrier.
         * Integration with asynchronous devices must drain those writers. */
        flush_request_=v;
        std::atomic_thread_fence(std::memory_order_seq_cst);
        flush_ack_=v;
        return;
    }
    auto* p=physical(bus_address,2);if(!p)unsupported("write16",cpu.R[15],at);p[0]=uint8_t(v>>8);p[1]=uint8_t(v);
}
void Starlet::write32(uint32_t at,uint32_t v) {
    check_alignment(at,4);
    const auto bus_address=translate(at,true);
    if(wii_hollywood_write(&hollywood_,bus_address,v))return;
    if(bus_address==0x0d80018cu) {
        /* IOS initializes the decoupled graphics/PI reset lines. They have no
         * attached PPC/GX in this isolated route. Boot-ROM remapping requires
         * a supplied boot0 and a different entry boundary, so reject it. */
        if(!(v&0x1000u) || ((v^spare1_)&~0xc00u))
            unsupported("SPARE1 mapping/control change",cpu.R[15],v);
        spare1_=v;
        return;
    }
    if(bus_address>=0x0d800100u && bus_address<=0x0d800144u && !(bus_address&3)) {
        /* AHB master/CPU/DMA arbitration configuration. The isolated discovery
         * machine has one synchronous bus master, so priorities cannot change
         * an access result. Performance counters are deliberately unsupported. */
        arbiter_config_[(bus_address-0x0d800100u)/4]=v;
        return;
    }
    if(bus_address==0x0d800070u) {exi_control_=v;return;}
    if(bus_address==0x0d8b0010u) {ahb_prefetch_=v;return;} /* prefetch configuration; synchronous memory */
    if(bus_address==0x0d8b0008u) {
        /* The AHB flush routing mask used by the IOS/mini coherent-memory
         * handshake. Stores on this discovery bus have already completed. */
        std::atomic_thread_fence(std::memory_order_seq_cst);
        ahb_flush_mask_=v;
        return;
    }
    auto* p=physical(bus_address,4);if(!p)unsupported("write32",cpu.R[15],at);
    p[0]=uint8_t(v>>24);p[1]=uint8_t(v>>16);p[2]=uint8_t(v>>8);p[3]=uint8_t(v);
}
void Starlet::coproc_write(uint32_t cp,uint32_t op,uint32_t crn,uint32_t crm,uint32_t op2,uint32_t value) {
    if(cpu.cpsr.mode==uint8_t(armv4t::Mode::User))unsupported("unprivileged coprocessor write",cpu.R[15],cp);
    if(cp==15 && op==0) {
        if(crm==0 && op2==0) {
            switch(crn) {
            case 1:if(!(value&0x80u))unsupported("little-endian SCTLR",cpu.R[15],value);sctl_=value;return;
            case 2:ttbr_=value;return;
            case 3:dacr_=value;return;
            case 5:dfsr_=value;return;
            case 6:far_=value;return;
            case 13:fcse_=value&0xfe000000u;return;
            }
        }
        /* There are no retained cache lines or deferred stores in this
         * development bus. Explicit cache/TLB maintenance orders real bytes;
         * code identity changes advance the instruction epoch. */
        if(crn==7 && ((crm==5 && (op2==0 || op2==1 || op2==4)) ||
            (crm==6 && op2<=2) || (crm==7 && op2==0) ||
            ((crm==10 || crm==14) && (op2==1 || op2==2 || op2==4)))) {++instruction_epoch_;return;}
        if(crn==8 && (crm==5 || crm==6 || crm==7) && op2<=1) {++instruction_epoch_;return;}
    }
    std::fprintf(stderr,"Starlet MCR cp=%u op1=%u c%u,c%u op2=%u value=%08x\n",cp,op,crn,crm,op2,value);
    unsupported("MCR",cpu.R[15],crn);
}
uint32_t Starlet::coproc_read(uint32_t cp,uint32_t op,uint32_t crn,uint32_t crm,uint32_t op2) {
    if(cpu.cpsr.mode==uint8_t(armv4t::Mode::User))unsupported("unprivileged coprocessor read",cpu.R[15],cp);
    if(cp==15 && op==0 && crm==0 && op2==0) {
        switch(crn) {
        case 0:return 0x41069265u; /* ARM926EJ-S r0p5 processor ID */
        case 1:return sctl_;
        case 2:return ttbr_;
        case 3:return dacr_;
        case 5:return dfsr_;
        case 6:return far_;
        case 13:return fcse_;
        }
    }
    std::fprintf(stderr,"Starlet MRC cp=%u op1=%u c%u,c%u op2=%u\n",cp,op,crn,crm,op2);
    unsupported("MRC",cpu.R[15],crn);
}
void Starlet::coproc_cdp(uint32_t cp,uint32_t op,uint32_t crn,uint32_t crm,uint32_t op2) {
    std::fprintf(stderr,"Starlet CDP cp=%u op1=%u c%u,c%u op2=%u\n",cp,op,crn,crm,op2);
    unsupported("CDP",cpu.R[15],crn);
}
uint32_t Starlet::fetch32(uint32_t at) {return read32(at);}
uint16_t Starlet::fetch16(uint32_t at) {return read16(at);}
}
