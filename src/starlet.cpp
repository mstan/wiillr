/* SPDX-License-Identifier: MIT */
#include "wiillr/starlet.hpp"
#include "interpreter.h"
#include "arm_decode.h"
#include "thumb_decode.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <atomic>

namespace {
class MemoryAbort final : public std::runtime_error {
public:explicit MemoryAbort(const char* message):std::runtime_error(message) {}
};
uint32_t be32(const uint8_t* p) {return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
uint16_t be16(const uint8_t* p) {return uint16_t(p[0])<<8|p[1];}
int usb_phy_index(uint32_t address) {
    switch(address) {
    case 0x0d0400a4:return 0; /* UTMI_CTRL */
    case 0x0d0400b0:return 1; /* USB_PHYCMN_CAL */
    case 0x0d0400b4:return 2; /* PHYADJ_CTRL */
    case 0x0d0400cc:return 3; /* USB_CHICKENBITS */
    default:return -1;
    }
}
[[noreturn]] void unsupported(const char* operation,uint32_t pc,uint32_t address) {
    char message[128];
    std::snprintf(message,sizeof message,"Starlet unsupported %s pc=%08x address=%08x",operation,pc,address);
    throw std::runtime_error(message);
}
}
namespace wiillr {
Starlet::Starlet(std::span<uint8_t> mem1,std::span<uint8_t> mem2):mem1_(mem1),mem2_(mem2) {
    /* An isolated original-kernel entry, not the native NAND-title handoff:
     * original IOS must program compatibility and audio clocks itself. */
    wii_ipc_init(&owned_ipc_);
    cpu.cpsr.mode=uint8_t(armv4t::Mode::Supervisor);
    cpu.cpsr.i=true;cpu.cpsr.f=true;
}
void Starlet::advance(uint64_t arm_cycles) {
    const uint64_t old_timer=cycles_/128;
    cycles_+=arm_cycles;
    for(unsigned i=0;i<ohci_.size();++i)if(ohci_[i].interrupt(cycles_))ipc_->arm_flags|=1u<<(5+i);
    const uint64_t ticks=cycles_/128-old_timer;
    const uint32_t distance=alarm_-uint32_t(old_timer);
    if(ticks>=(distance ? uint64_t(distance):0x100000000ull))ipc_->arm_flags|=1;
    wii_ipc_tick(ipc_,ppc_origin_+cycles_*3);
}
void Starlet::exception(armv4t::Mode mode,uint32_t vector,uint32_t link) {
    auto bank=[](uint8_t current) {
        using namespace armv4t;
        switch(Mode(current)) {
        case Mode::FIQ:return Bank_FIQ;case Mode::IRQ:return Bank_IRQ;
        case Mode::Supervisor:return Bank_Supervisor;case Mode::Abort:return Bank_Abort;
        case Mode::Undefined:return Bank_Undefined;default:return Bank_User;
        }
    };
    const auto old_bank=bank(cpu.cpsr.mode),new_bank=bank(uint8_t(mode));
    const uint32_t status=uint32_t(cpu.cpsr.n)<<31|uint32_t(cpu.cpsr.z)<<30|
        uint32_t(cpu.cpsr.c)<<29|uint32_t(cpu.cpsr.v)<<28|uint32_t(cpu.cpsr.q)<<27|
        uint32_t(cpu.cpsr.i)<<7|uint32_t(cpu.cpsr.f)<<6|uint32_t(cpu.cpsr.t)<<5|cpu.cpsr.mode;
    cpu.banked_sp[old_bank]=cpu.R[13];cpu.banked_lr[old_bank]=cpu.R[14];
    if(old_bank==armv4t::Bank_FIQ) {
        for(unsigned i=0;i<5;++i) {cpu.r8_12_fiq[i]=cpu.R[i+8];cpu.R[i+8]=cpu.r8_12_user[i];}
    }
    cpu.banked_spsr[new_bank]=status;
    cpu.R[13]=cpu.banked_sp[new_bank];cpu.R[14]=link;
    cpu.cpsr.mode=uint8_t(mode);cpu.cpsr.t=false;cpu.cpsr.i=true;cpu.thumb=false;
    cpu.R[15]=((sctl_&(1u<<13))?0xffff0000u:0)|vector;
}
uint32_t Starlet::step() {
    const uint32_t pc=cpu.R[15];
    try {
        const auto instruction=cpu.cpsr.t ? armv4t::ThumbDecoder::decode(fetch16(pc),pc):
            armv4t::ArmDecoder::decode(fetch32(pc),pc);
        return execute(instruction);
    }catch(const MemoryAbort&) {
        exception(armv4t::Mode::Abort,0xc,pc+4);++prefetch_aborts_;
        advance(3);return 3;
    }
}
uint32_t Starlet::execute(const armv4t::Instr& instruction) {
    uint32_t cost=0;
    armv4t::Interpreter::Result result;
    try {result=armv4t::Interpreter::step(cpu,*this,instruction,&cost);}
    catch(const MemoryAbort&) {
        exception(armv4t::Mode::Abort,0x10,instruction.pc+8);++data_aborts_;
        advance(3);return 3;
    }
    if(result==armv4t::Interpreter::Result::NotImplemented)
        unsupported("ARM/Thumb instruction",instruction.pc,instruction.raw);
    if(result==armv4t::Interpreter::Result::Undefined) {
        exception(armv4t::Mode::Undefined,4,instruction.pc+(instruction.thumb?2u:4u));
        ++undefined_entries_;
    }
    if(result==armv4t::Interpreter::Result::Swi) {
        exception(armv4t::Mode::Supervisor,8,instruction.pc+(instruction.thumb?2u:4u));
        ++swi_entries_;
    }
    advance(cost);
    if(!cpu.cpsr.i && wii_ipc_arm_interrupt(ipc_)) {
        exception(armv4t::Mode::IRQ,0x18,cpu.R[15]+4); /* IRQ preserves F. */
        ++irq_entries_;
    }
    return cost;
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
        if(fetching_)ifsr_=(domain<<4)|status;
        else {dfsr_=(domain<<4)|status;far_=original;}
        throw MemoryAbort("Starlet MMU translation/domain/permission fault");
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
        if(fetching_)ifsr_=1;
        else {dfsr_=1;far_=address;}
        throw MemoryAbort("Starlet alignment fault");
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
    if(store_observer_)for(uint32_t n=0;n<count;++n) {
        const auto* p=e.data()+table+n*stride;
        if(be32(p)==1 && be32(p+20))store_observer_(store_user_,be32(p+12),be32(p+20));
    }
    cpu.R[15]=entry;
    return entry;
}
void Starlet::load_otp(std::span<const uint8_t> bytes) {
    if(bytes.size()!=otp_.size())throw std::runtime_error("Starlet OTP must contain exactly 128 original bytes");
    std::memcpy(otp_.data(),bytes.data(),otp_.size());otp_loaded_=true;
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
    uint32_t ipc_value;
    if(bus_address>=0x0d050000u && bus_address<0x0d070000u &&
       ohci_[(bus_address-0x0d050000u)>>16].read(bus_address&0xffffu,cycles_,ipc_value))return ipc_value;
    if(bus_address>=0x0d800000u && bus_address<=0x0d80003cu &&
       wii_ipc_read(ipc_,bus_address-0x0d800000u,&ipc_value))return ipc_value;
    /* HW_TIMER: free-running AHB/128 counter (243MHz /128). */
    if(bus_address==0x0d800010u)return uint32_t(cycles_/128);
    if(bus_address==0x0d800014u)return alarm_;
    if(bus_address==0x0d800040u)return 0; /* FIQ source mask, currently all IRQ. */
    if(bus_address==0x0d80005cu)return debug_irq_enable_;
    if(bus_address==0x0d800064u)return ahb_protection_;
    if(bus_address==0x0d800194u)return resets_;
    if(bus_address==0x0d8001ecu)return otp_command_;
    if(bus_address==0x0d8001f0u) {
        if(!(otp_command_&0x80000000u))unsupported("OTP read without read command",cpu.R[15],otp_command_);
        if(!otp_loaded_)throw std::runtime_error("Starlet original 128-byte OTP input is required for OTP data read");
        return be32(otp_.data()+(otp_command_&31u)*4);
    }
    if(bus_address>=0x0d010000u && bus_address<=0x0d030000u && !(bus_address&0xffffu))
        return dma_control_[(bus_address-0x0d010000u)/0x10000];
    if(bus_address>=0x0d8001e0u && bus_address<=0x0d8001e8u && !(bus_address&3))
        return io_strength_[(bus_address-0x0d8001e0u)/4];
    if(bus_address==0x0d8001c4u)return vi_pll_;
    if(bus_address==0x0d8001c8u)return vi_pll_ext_;
    if(bus_address==0x0d8001d4u)return usb_pll_;
    if(bus_address==0x0d8001d8u)return usb_pll_ext_;
    if(bus_address==0x0d800088u)return usb_force_reset_;
    if(const int index=usb_phy_index(bus_address);index>=0)return usb_phy_[index];
    if(bus_address==0x0d8000dcu)return gpio_enable_;
    if(bus_address>=0x0d8000c0u && bus_address<=0x0d8000fcu && !(bus_address&3)) {
        const uint32_t index=((bus_address-0x0d8000c0u)/4)&7;
        if(index==2) {
            /* Only the exercised serial device is attached. Other board
             * input/strap routes stay explicit, rather than inventing them. */
            if(!seeprom_.loaded() || !(gpio_outputs()&Seeprom::cs))
                unsupported("unattached GPIO input/straps",cpu.R[15],bus_address);
            const uint32_t pins=gpio_outputs()|(seeprom_.input(cycles_)?Seeprom::miso:0);
            return pins&(bus_address<0x0d8000e0u?gpio_[7]:0xffffffu);
        }
        if(index==6)unsupported("unattached GPIO input/straps",cpu.R[15],bus_address);
        const uint32_t mask=bus_address<0x0d8000e0u && index!=7 ? gpio_[7]:0xffffffu;
        return gpio_[index]&mask;
    }
    uint32_t hollywood_value;if(wii_hollywood_read(hollywood_,bus_address,&hollywood_value))return hollywood_value;
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
    const auto address=translate(at,true);
    auto* p=physical(address,1);if(!p)unsupported("write8",cpu.R[15],at);*p=v;
    if(store_observer_)store_observer_(store_user_,address,1);
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
    if(store_observer_)store_observer_(store_user_,bus_address,2);
}
void Starlet::write32(uint32_t at,uint32_t v) {
    check_alignment(at,4);
    const auto bus_address=translate(at,true);
    if(bus_address>=0x0d050000u && bus_address<0x0d070000u &&
       ohci_[(bus_address-0x0d050000u)>>16].write(bus_address&0xffffu,v,cycles_))return;
    if(bus_address>=0x0d800000u && bus_address<=0x0d80003cu &&
       wii_ipc_write(ipc_,bus_address-0x0d800000u,v,ppc_origin_+cycles_*3))return;
    if(bus_address==0x0d800014u) {alarm_=v;return;}
    if(bus_address==0x0d8001ecu) {
        if((v&~31u)!=0x80000000u)unsupported("unsupported OTP command",cpu.R[15],v);
        otp_command_=v;return;
    }
    if(bus_address==0x0d80005cu) {debug_irq_enable_=v;return;}
    if(bus_address==0x0d800064u) {ahb_protection_=v;return;}
    if(bus_address==0x0d800040u) {
        if(v)unsupported("FIQ source routing",cpu.R[15],v);
        return;
    }
    if(bus_address==0x0d800194u) {
        if(!(v&0x08000000u))unsupported("IOP reset",cpu.R[15],v);
        resets_=v&0x7fffffffu;return;
    }
    if(bus_address>=0x0d010000u && bus_address<=0x0d030000u && !(bus_address&0xffffu)) {
        /* Original IOS clears the NAND/AES/SHA command words before setup.
         * Idle/clear is meaningful without media or keys; starting a command
         * still fails explicitly until that actual device is implemented. */
        if(v)unsupported("unimplemented NAND/AES/SHA command/control",cpu.R[15],v);
        dma_control_[(bus_address-0x0d010000u)/0x10000]=0;return;
    }
    /* I/O pad drive-strength configuration; no data or completion bits. */
    if(bus_address>=0x0d8001e0u && bus_address<=0x0d8001e8u && !(bus_address&3)) {
        io_strength_[(bus_address-0x0d8001e0u)/4]=v;return;
    }
    /* This IOS-entry profile leaves the VI PLL disabled. The original IOS
     * clock sequence programs these retained configuration registers; there
     * is no VI consumer in the standalone Starlet route. */
    if(bus_address==0x0d8001c4u) {vi_pll_=v;return;}
    if(bus_address==0x0d8001c8u) {vi_pll_ext_=v;return;}
    if(bus_address==0x0d8001d4u) {usb_pll_=v;return;}
    if(bus_address==0x0d8001d8u) {usb_pll_ext_=v;return;}
    /* Original clock startup programs intermediate AI PLL configuration too;
     * the native title-handoff helper only accepts completed clock presets. */
    if(bus_address==0x0d8001ccu) {hollywood_->audio_pll=v;return;}
    if(bus_address==0x0d8001d0u) {hollywood_->audio_control=v;return;}
    if(bus_address==0x0d800180u && !((v^hollywood_->compat)&~0x106u)) {
        /* Kernel boot also sets COMPAT.B1/B2. Its PPC-side interface is not
         * attached here; preserve the control without changing other bits. */
        hollywood_->compat=v;return;
    }
    /* HW_USBFRCRST: USB reset output configuration, not a completion flag.
     * No USB host is attached to this isolated IOS-entry machine. */
    if(bus_address==0x0d800088u) {usb_force_reset_=v;return;}
    /* EHCI analogue PHY setup only. Controller commands/status and USB DMA
     * remain unsupported; these output controls never synthesize a device. */
    if(const int index=usb_phy_index(bus_address);index>=0) {usb_phy_[index]=v;return;}
    if(bus_address==0x0d8000dcu) {
        const auto before=gpio_outputs();gpio_enable_=v&0xffffffu;
        seeprom_.outputs(before,gpio_outputs(),cycles_);return;
    }
    if(bus_address>=0x0d8000c0u && bus_address<=0x0d8000fcu && !(bus_address&3)) {
        const uint32_t index=((bus_address-0x0d8000c0u)/4)&7;
        if(index==2 || index==6)unsupported("read-only GPIO input/straps write",cpu.R[15],bus_address);
        /* GPIOB is a masked window on the same physical pin controls. The
         * Starlet window can read all pins, but only write its owned pins. */
        const uint32_t mask=index==7 ? 0xffffffu:
            (bus_address<0x0d8000e0u ? gpio_[7]:~gpio_[7])&0xffffffu;
        const auto before=gpio_outputs();
        if(index==4)gpio_[index]&=~(v&mask); /* W1C interrupt latch */
        else gpio_[index]=(gpio_[index]&~mask)|(v&mask);
        if(index==0 || index==1)seeprom_.outputs(before,gpio_outputs(),cycles_);
        return;
    }
    if(wii_hollywood_write(hollywood_,bus_address,v))return;
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
    if(store_observer_)store_observer_(store_user_,bus_address,4);
}
void Starlet::coproc_write(uint32_t cp,uint32_t op,uint32_t crn,uint32_t crm,uint32_t op2,uint32_t value) {
    if(cpu.cpsr.mode==uint8_t(armv4t::Mode::User))unsupported("unprivileged coprocessor write",cpu.R[15],cp);
    if(cp==15 && op==0) {
        if(crn==5 && crm==0 && op2==1) {ifsr_=value;return;}
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
    if(cp==15 && op==0 && crn==5 && crm==0 && op2==1)return ifsr_;
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
uint32_t Starlet::fetch32(uint32_t at) {
    fetching_=true;
    try {const auto value=read32(at);fetching_=false;return value;}
    catch(...) {fetching_=false;throw;}
}
uint16_t Starlet::fetch16(uint32_t at) {
    fetching_=true;
    try {const auto value=read16(at);fetching_=false;return value;}
    catch(...) {fetching_=false;throw;}
}
}
