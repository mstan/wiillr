/* SPDX-License-Identifier: MIT */
#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <atomic>
#include <limits>
namespace wiillr {
/* OHCI 1.0 control/IRQ/reset and idle Operational boundary (0x00..0x44).
 * Descriptor execution, root-hub state and transfers remain unsupported;
 * all list-enable and transfer-activation requests fail explicitly.
 * OpenHCI 1.0a sections4.4.1/6.2/6.5.6 define HCCA frame stores and FNO.
 * HCR selects USB suspend after a 10us reset profile at the 243MHz IOP clock.
 * IRQ status is W1C; the enable/disable aliases modify only the enable mask. */
class OhciInit {
public:
    struct Bus {void* user;uint8_t* (*map)(void*,uint32_t,uint32_t);void (*stored)(void*,uint32_t,uint32_t);};
    OhciInit() {registers_[0x34/4]=0x2edf;registers_[0x44/4]=0x628;}
    bool read(uint32_t at,uint64_t cycles,uint32_t& value,const Bus& bus={}) {
        finish(cycles,bus);
        if(at&3 || at>0x44)return false;
        if(at==0) {value=0x10;return true;}
        if(at==8) {value=reset_until_?1:0;return true;}
        if(reset_until_)throw std::runtime_error("Starlet OHCI access during controller reset");
        if(at==0x10 || at==0x14) {value=enabled_;return true;}
        if(at==0x0c) {value=status_;return true;}
        if(at==0x38 && frame_running_) {value=remaining(cycles);return true;}
        value=registers_[at/4];return true;
    }
    bool write(uint32_t at,uint32_t value,uint64_t cycles,const Bus& bus={}) {
        finish(cycles,bus);
        if(at&3 || at>0x44)return false;
        if(reset_until_)throw std::runtime_error("Starlet OHCI access during controller reset");
        switch(at) {
        case 4: {
            if(value&0x3c)throw std::runtime_error("Starlet OHCI descriptor list provider is not attached");
            const bool operational=(value&0xc0)==0x80;
            if(operational && !frame_running_) {
                mapped_hcca(bus,registers_[0x18/4]);
                frame_end_=scaled(cycles)+frame_period();
                frame_toggle_=registers_[0x34/4]&0x80000000u;frame_running_=true;
            } else if(!operational && frame_running_) {
                registers_[0x38/4]=remaining(cycles);frame_running_=false;
            }
            registers_[at/4]=value&0x7ff;return true;
        }
        case 8:
            if(value&6)throw std::runtime_error("Starlet OHCI transfer descriptor provider is not attached");
            if(value&1) {frame_running_=false;reset_until_=cycles+2430;return true;}
            if(value&8 && (registers_[1]&0x100)) {registers_[1]&=~0x100u;status_|=0x40000000u;}
            return true;
        case 0x0c:status_&=~(value&0x4000007fu);return true;
        case 0x10:enabled_|=value&0xc000007fu;return true;
        case 0x14:enabled_&=~value;return true;
        case 0x18:
            value&=0xffffff00u;if(frame_running_)mapped_hcca(bus,value);
            registers_[at/4]=value;return true;
        case 0x20:case 0x24:case 0x28:case 0x2c:registers_[at/4]=value&0xfffffff0u;return true;
        case 0x34:registers_[at/4]=value&0xffff3fffu;return true;
        case 0x40:registers_[at/4]=value&0x3fff;return true;
        case 0x44:registers_[at/4]=value&0xfff;return true;
        case 0:case 0x1c:case 0x30:case 0x38:case 0x3c:
            if(value)throw std::runtime_error("Starlet OHCI read-only register write");
            return true;
        }
        return false;
    }
    bool interrupt(uint64_t cycles,const Bus& bus={}) {finish(cycles,bus);return (enabled_&0x80000000u) && (status_&enabled_&0x4000007fu);}
private:
    static uint64_t scaled(uint64_t cycles) {
        if(cycles>(std::numeric_limits<uint64_t>::max()-0x4000u*81u)/4)
            throw std::runtime_error("Starlet OHCI frame clock overflow");
        return cycles*4;
    }
    /* IOP243MHz / USB12MHz =81/4. Retain quarter-cycle phase so arbitrary
     * FI values do not accumulate rounding drift; default FI is exactly1ms. */
    uint64_t frame_period() const {return uint64_t((registers_[0x34/4]&0x3fff)+1)*81;}
    uint32_t remaining(uint64_t cycles) const {
        return frame_toggle_|uint32_t((frame_end_-scaled(cycles)+80)/81-1);
    }
    static uint8_t* mapped_hcca(const Bus& bus,uint32_t at) {
        auto* p=bus.map?bus.map(bus.user,at,256):nullptr;
        if(!p)throw std::runtime_error("Starlet OHCI HCCA DMA outside physical RAM");
        return p;
    }
    void finish(uint64_t cycles,const Bus& bus) {
        if(reset_until_ && cycles>=reset_until_) {
            registers_.fill(0);registers_[1]=0xc0;registers_[0x34/4]=0x2edf;
            registers_[0x44/4]=0x628;enabled_=status_=0;reset_until_=0;
        }
        if(reset_until_ || !frame_running_)return;
        const auto now=scaled(cycles);
        while(now>=frame_end_) {
            auto* hcca=mapped_hcca(bus,registers_[0x18/4]);
            const uint32_t old=registers_[0x3c/4],next=(old+1)&0xffffu;
            /* OHCI DMA structures are little endian. Clear HCCA's frame
             * padding, preserving every periodic head and done-head byte. */
            hcca[128]=uint8_t(next);hcca[129]=uint8_t(next>>8);hcca[130]=hcca[131]=0;
            registers_[0x3c/4]=next;
            frame_end_+=frame_period();
            frame_toggle_=registers_[0x34/4]&0x80000000u;
            if(bus.stored)bus.stored(bus.user,registers_[0x18/4]+128,4);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            status_|=4; /* StartOfFrame is published after HccaFrameNumber. */
            if((old^next)&0x8000u)status_|=0x20; /* Both bit15 transitions. */
        }
    }
    std::array<uint32_t,0x48/4> registers_{};
    uint32_t enabled_=0,status_=0;
    uint64_t reset_until_=0;
    bool frame_running_=false;
    uint32_t frame_toggle_=0;
    uint64_t frame_end_=0;
};
}
