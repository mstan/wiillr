/* SPDX-License-Identifier: MIT */
#pragma once
#include <array>
#include <span>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace wiillr {
/* 93C56 x16 Microwire, driven by the actual CS/CLK/DI edges. Commands and
 * the don't-care address bit follow Microchip DS21794B table 1-3. Storage is
 * a private copy: programming can never modify the supplied backup. The
 * selected programming-time profile is 5ms at the 243MHz IOP clock. */
class Seeprom {
public:
    static constexpr uint32_t cs=0x400,clock=0x800,mosi=0x1000,miso=0x2000;
    static constexpr uint64_t program_cycles=1215000;
    void load(std::span<const uint8_t> bytes) {
        if(bytes.size()!=bytes_.size())throw std::runtime_error("Starlet SEEPROM must contain exactly 256 original bytes");
        *this=Seeprom{};std::memcpy(bytes_.data(),bytes.data(),bytes_.size());loaded_=true;
    }
    bool loaded() const {return loaded_;}
    std::span<const uint8_t> image(uint64_t cycles) {finish(cycles);return bytes_;}
    void outputs(uint32_t before,uint32_t after,uint64_t cycles) {
        if(!loaded_)return;
        finish(cycles);
        if(!(after&cs)) {
            if((before&cs) && requested_!=None && write_enabled_) {
                pending_=requested_;busy_until_=cycles+program_cycles;
            }
            bits_=0;shift_=0;phase_=Command;requested_=None;output_=true;return;
        }
        if(!(before&cs)) {bits_=0;shift_=0;phase_=Command;output_=true;}
        if(busy_until_ || (before&clock) || !(after&clock))return;
        if(phase_==Read) {
            const auto word=uint16_t(bytes_[address_*2])<<8|bytes_[address_*2+1];
            output_=(word&(0x8000u>>bits_))!=0;
            if(++bits_==16) {bits_=0;address_=(address_+1)&127;}
        } else if(phase_==Write || phase_==WriteAll) {
            shift_=(shift_<<1)|((after&mosi)!=0);
            if(++bits_==16) {data_=uint16_t(shift_);requested_=phase_==Write?Word:All;phase_=Complete;}
        } else if(phase_==Command) {
            const uint32_t bit=(after&mosi)!=0;
            if(!bits_ && !bit)return; /* Leading clocks do not start a command. */
            shift_=(shift_<<1)|bit;
            if(++bits_!=11)return;
            address_=shift_&127;bits_=0;
            switch((shift_>>8)&3) {
            case 2:phase_=Read;output_=false;break; /* READ dummy zero. */
            case 1:phase_=Write;shift_=0;break;
            case 3:requested_=Word;data_=0xffff;phase_=Complete;break;
            case 0:
                switch((shift_>>6)&3) {
                case 0:write_enabled_=false;phase_=Complete;break;
                case 3:write_enabled_=true;phase_=Complete;break;
                case 2:requested_=All;data_=0xffff;phase_=Complete;break;
                case 1:phase_=WriteAll;shift_=0;break;
                }
            }
        }
    }
    bool input(uint64_t cycles) {finish(cycles);return !busy_until_ && output_;}
private:
    enum Phase {Command,Read,Write,WriteAll,Complete};
    enum Program {None,Word,All};
    void finish(uint64_t cycles) {
        if(!busy_until_ || cycles<busy_until_)return;
        const unsigned first=pending_==All?0:address_,end=pending_==All?128:address_+1;
        for(unsigned i=first;i<end;++i) {bytes_[i*2]=uint8_t(data_>>8);bytes_[i*2+1]=uint8_t(data_);}
        busy_until_=0;pending_=None;output_=true;
    }
    std::array<uint8_t,256> bytes_{};
    bool loaded_=false,write_enabled_=false,output_=true;
    uint32_t shift_=0;unsigned bits_=0,address_=0;
    uint16_t data_=0;
    uint64_t busy_until_=0;
    Phase phase_=Command;
    Program requested_=None,pending_=None;
};
}
