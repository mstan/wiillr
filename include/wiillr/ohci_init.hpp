/* SPDX-License-Identifier: MIT */
#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
namespace wiillr {
/* OHCI 1.0 control/IRQ/reset boundary (registers 0x00..0x44). Descriptor
 * execution, root-hub device state and transfers are separate, unsupported
 * interfaces: entering operational state must not silently drop DMA work.
 * HCR selects USB suspend after a 10us reset profile at the 243MHz IOP clock.
 * IRQ status is W1C; the enable/disable aliases modify only the enable mask. */
class OhciInit {
public:
    OhciInit() {registers_[0x34/4]=0x2edf;registers_[0x44/4]=0x628;}
    bool read(uint32_t at,uint64_t cycles,uint32_t& value) {
        finish(cycles);
        if(at&3 || at>0x44)return false;
        if(at==0) {value=0x10;return true;}
        if(at==8) {value=reset_until_?1:0;return true;}
        if(reset_until_)throw std::runtime_error("Starlet OHCI access during controller reset");
        if(at==0x10 || at==0x14) {value=enabled_;return true;}
        if(at==0x0c) {value=status_;return true;}
        value=registers_[at/4];return true;
    }
    bool write(uint32_t at,uint32_t value,uint64_t cycles) {
        finish(cycles);
        if(at&3 || at>0x44)return false;
        if(reset_until_)throw std::runtime_error("Starlet OHCI access during controller reset");
        switch(at) {
        case 4:
            if((value&0xc0)==0x80)throw std::runtime_error("Starlet OHCI operational DMA provider is not attached");
            registers_[at/4]=value&0x7ff;return true;
        case 8:
            if(value&6)throw std::runtime_error("Starlet OHCI transfer descriptor provider is not attached");
            if(value&1) {reset_until_=cycles+2430;return true;}
            if(value&8 && (registers_[1]&0x100)) {registers_[1]&=~0x100u;status_|=0x40000000u;}
            return true;
        case 0x0c:status_&=~(value&0x4000007fu);return true;
        case 0x10:enabled_|=value&0xc000007fu;return true;
        case 0x14:enabled_&=~value;return true;
        case 0x18:registers_[at/4]=value&0xffffff00u;return true;
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
    bool interrupt(uint64_t cycles) {finish(cycles);return (enabled_&0x80000000u) && (status_&enabled_&0x4000007fu);}
private:
    void finish(uint64_t cycles) {
        if(reset_until_ && cycles>=reset_until_) {
            registers_.fill(0);registers_[1]=0xc0;registers_[0x34/4]=0x2edf;
            registers_[0x44/4]=0x628;enabled_=status_=0;reset_until_=0;
        }
    }
    std::array<uint32_t,0x48/4> registers_{};
    uint32_t enabled_=0,status_=0;
    uint64_t reset_until_=0;
};
}
