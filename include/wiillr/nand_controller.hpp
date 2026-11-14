/* SPDX-License-Identifier: MIT */
#pragma once
#include <array>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <utility>
#include <atomic>
namespace wiillr {
/* Raw 2KiB+64-byte NAND controller. Media is caller-supplied; writes and
 * erases use private page overlays and never touch the supplied image. The
 * fixed ID/timing profile describes a compatible 512MiB Samsung device; it
 * does not claim to identify the physical chip that produced a backup. */
class NandController {
public:
    static constexpr uint32_t page_size=0x840;
    struct Bus {
        void* user;
        uint8_t* (*map)(void*,uint32_t,uint32_t);
        void (*stored)(void*,uint32_t,uint32_t);
    };
    void load(std::vector<uint8_t> bytes) {
        if(bytes.empty() || bytes.size()%page_size)
            throw std::runtime_error("Starlet raw NAND must contain whole 2112-byte pages");
        *this=NandController{};media_=std::move(bytes);
    }
    static uint32_t ecc(const uint8_t* data) {
        uint32_t odd=0,even=0;
        for(uint32_t byte=0;byte<512;++byte)for(uint32_t bit=0;bit<8;++bit)
            if(data[byte]&(1u<<bit)) {const uint32_t index=(byte<<3)|bit;odd^=index;even^=index^4095u;}
        return ((even&255u)<<24)|((even&3840u)<<8)|((odd&255u)<<8)|((odd&3840u)>>8);
    }
    bool read(uint32_t at,uint32_t& value) const {
        if(at&3 || at>0x1c)return false;
        value=at==0?command_:registers_[at/4];return true;
    }
    bool write(uint32_t at,uint32_t value,uint64_t cycles,const Bus& bus) {
        if(at&3 || at>0x1c)return false;
        if(at) {registers_[at/4]=value;return true;}
        if(!(value&0x80000000u)) {
            if(until_)throw std::runtime_error("Starlet NAND command cancelled while busy");
            command_&=~(value&0x7fffffffu);return true;
        }
        const uint32_t op=(value>>16)&255u,count=value&4095u;
        if(until_ && op!=0xff)throw std::runtime_error("Starlet NAND command submitted while busy");
        if(media_.empty())throw std::runtime_error("Starlet original raw NAND input is required");
        command_=value;operation_=Idle;
        active_data_=registers_[4];active_ecc_=registers_[5];
        uint64_t latency=243; /* command/address latch: 1us selected profile */
        switch(op) {
        case 0xff:
            /* A flash reset aborts staging/in-flight work, not its media.
             * A later confirm must never resurrect a pre-reset operation. */
            staged_=erase_staged_=false;latency=24300;break; /*100us */
        case 0x00:
            selected_page_=registers_[3];selected_column_=registers_[2]&0xffffu;break;
        case 0x30:
            if(!(value&0x2000u) || count>page_size)throw std::runtime_error("Starlet unsupported NAND read mode");
            require_page(selected_page_);operation_=Read;latency=6075;break; /*25us */
        case 0x90:
            if(!(value&0x2000u) || registers_[2] || !count || count>64)throw std::runtime_error("Starlet unsupported NAND ID mode");
            operation_=Id;break;
        case 0x70:
            if(!(value&0x2000u) || !count || count>64)throw std::runtime_error("Starlet unsupported NAND status mode");
            operation_=Status;break;
        case 0x80:
        {
            if(!(value&0x4000u) || (value&0x2000u) || (count!=0x800 && count!=page_size) || (registers_[2]&0xffffu))
                throw std::runtime_error("Starlet unsupported NAND program mode");
            require_page(registers_[3]);
            if((active_data_&15u) || ((count==page_size || (value&0x1000u)) && (active_ecc_&15u)))
                throw std::runtime_error("Starlet NAND program DMA must be 16-byte aligned");
            const auto* data=mapped(bus,active_data_,0x800);
            const auto* spare=count==page_size?mapped(bus,active_ecc_,0x40):nullptr;
            if(value&0x1000u)mapped(bus,active_ecc_^0x40u,0x10); // Validate before any DMA write.
            staging_.fill(0xff);std::memcpy(staging_.data(),data,0x800);
            if(spare)std::memcpy(staging_.data()+0x800,spare,0x40);
            selected_page_=registers_[3];selected_column_=0;staged_=true;
            /* ECC calculation is separate RAM output, not an implicit edit
             * to raw spare bytes sent to the NAND page register. */
            if(value&0x1000u)operation_=StageEcc;
            break;
        }
        case 0x85:
        {
            /* Samsung K9F4G08U0A p34-35: random data input updates the staged
             * page register, and only a later 0x10 commits it to media. Reject
             * the different five-address-byte copy-back command here. */
            const uint32_t column=registers_[2]&0xffffu;
            if(!staged_ || !(value&0x4000u) || (value&0x1c003000u) || !count ||
               count>0x800 || column>page_size || count>page_size-column || (active_data_&15u))
                throw std::runtime_error("Starlet unsupported NAND random data input");
            std::memcpy(staging_.data()+column,mapped(bus,active_data_,count),count);break;
        }
        case 0x10:
            if(!staged_)throw std::runtime_error("Starlet NAND program without staged page");
            operation_=Program;latency=48600;break; /*200us */
        case 0x60:selected_page_=registers_[3];require_page(selected_page_);erase_staged_=true;break;
        case 0xd0:
            if(!erase_staged_)throw std::runtime_error("Starlet NAND erase without address");
            operation_=Erase;latency=486000;break; /*2ms */
        default:throw std::runtime_error("Starlet unsupported NAND command");
        }
        until_=cycles+latency;return true;
    }
    bool advance(uint64_t cycles,const Bus& bus) {
        if(!until_ || cycles<until_)return false;
        const uint32_t count=command_&4095u;
        if(operation_==Read) {
            const auto* page=page_bytes(selected_page_);
            if(selected_column_ || count!=page_size)throw std::runtime_error("Starlet unsupported partial NAND DMA read");
            auto* data=mapped(bus,active_data_,0x800);
            auto* spare=mapped(bus,active_ecc_,0x40);
            auto* syndrome=(command_&0x1000)?mapped(bus,active_ecc_^0x40u,0x10):nullptr;
            std::memcpy(data,page,0x800);std::memcpy(spare,page+0x800,0x40);
            if(syndrome)for(unsigned i=0;i<4;++i)put32(syndrome+i*4,ecc(page+i*512));
            stored(bus,active_data_,0x800);
            const bool contiguous=syndrome && (active_ecc_^0x40u)==active_ecc_+0x40;
            stored(bus,active_ecc_,contiguous?0x50:0x40);
            if(syndrome && !contiguous)stored(bus,active_ecc_^0x40u,0x10);
        } else if(operation_==StageEcc) {
            auto* output=mapped(bus,active_ecc_^0x40u,0x10);
            for(unsigned i=0;i<4;++i)put32(output+i*4,ecc(staging_.data()+i*512));
            stored(bus,active_ecc_^0x40u,0x10);
        } else if(operation_==Id || operation_==Status) {
            auto* data=mapped(bus,active_data_,count);
            if(operation_==Id) {
                const uint8_t id[]={0xec,0xdc,0x10,0x95,0x54};
                for(unsigned i=0;i<count;++i)data[i]=id[i%5];
            } else std::fill_n(data,count,uint8_t(0xc0));
            stored(bus,active_data_,count);
        } else if(operation_==Program) {
            auto& dest=mutable_page(selected_page_);
            for(unsigned i=0;i<page_size;++i)dest[i]&=staging_[i];
            staged_=false;
        } else if(operation_==Erase) {
            const auto first=selected_page_&~63u;require_page(first+63);
            for(unsigned i=0;i<64;++i)mutable_page(first+i).fill(0xff);
            erase_staged_=false;
        }
        std::atomic_thread_fence(std::memory_order_seq_cst);
        until_=0;command_&=~0x80000000u;
        return (command_&0x40000000u)!=0;
    }
    const uint8_t* page_bytes(uint32_t page) const {
        require_page(page);auto found=overlay_.find(page);
        return found==overlay_.end()?media_.data()+size_t(page)*page_size:found->second.data();
    }
private:
    enum Operation {Idle,Read,Id,Status,StageEcc,Program,Erase};
    static uint8_t* mapped(const Bus& bus,uint32_t address,uint32_t size) {
        auto* result=bus.map?bus.map(bus.user,address,size):nullptr;
        if(!result && size)throw std::runtime_error("Starlet NAND DMA outside physical RAM");
        return result;
    }
    static void stored(const Bus& bus,uint32_t address,uint32_t size) {if(size && bus.stored)bus.stored(bus.user,address,size);}
    static void put32(uint8_t* p,uint32_t v) {for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(24-8*i));}
    void require_page(uint32_t page) const {
        if(media_.empty())throw std::runtime_error("Starlet original raw NAND input is required");
        if(page>=media_.size()/page_size)throw std::runtime_error("Starlet NAND page outside supplied image");
    }
    std::array<uint8_t,page_size>& mutable_page(uint32_t page) {
        auto found=overlay_.find(page);
        if(found!=overlay_.end())return found->second;
        std::array<uint8_t,page_size> copy;std::memcpy(copy.data(),page_bytes(page),page_size);
        return overlay_.emplace(page,copy).first->second;
    }
    std::vector<uint8_t> media_;
    std::unordered_map<uint32_t,std::array<uint8_t,page_size>> overlay_;
    std::array<uint32_t,8> registers_{};
    std::array<uint8_t,page_size> staging_{};
    uint32_t command_=0,selected_page_=0,selected_column_=0;
    uint32_t active_data_=0,active_ecc_=0;
    uint64_t until_=0;
    bool staged_=false,erase_staged_=false;
    Operation operation_=Idle;
};
}
