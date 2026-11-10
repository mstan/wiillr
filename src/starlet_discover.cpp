/* SPDX-License-Identifier: MIT */
#include "wiillr/starlet.hpp"
#include "arm_decode.h"
#include "thumb_decode.h"
#include "interpreter.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdio>
#include <stdexcept>

int main(int argc,char** argv) {
    if(argc!=3) {std::fprintf(stderr,"usage: wiillr-starlet-discover ORIGINAL-IOS-CONTENT MAX-STEPS\n");return 2;}
    uint64_t retired=0,cycles=0;
    uint32_t pc=0,raw=0;
    try {
        size_t used;
        const std::string count=argv[2];const uint64_t limit=std::stoull(count,&used,0);
        if(used!=count.size() || !limit || limit>1000000000ull)throw std::runtime_error("Invalid step limit");
        std::ifstream input(argv[1],std::ios::binary);
        if(!input)throw std::runtime_error("Cannot open IOS content");
        std::vector<uint8_t> binary((std::istreambuf_iterator<char>(input)),{});
        std::vector<uint8_t> mem1(0x1800000),mem2(0x4000000);
        wiillr::Starlet machine(mem1,mem2);
        machine.load_ios(binary);
        while(retired<limit) {
            pc=machine.cpu.R[15];
            raw=machine.cpu.cpsr.t ? machine.fetch16(pc):machine.fetch32(pc);
            const auto instruction=machine.cpu.cpsr.t ? armv4t::ThumbDecoder::decode(uint16_t(raw),pc):
                armv4t::ArmDecoder::decode(raw,pc);
            uint32_t cost;
            const auto result=armv4t::Interpreter::step(machine.cpu,machine,instruction,&cost);
            if(result==armv4t::Interpreter::Result::Undefined || result==armv4t::Interpreter::Result::NotImplemented)
                throw std::runtime_error("Unsupported ARM/Thumb instruction");
            if(result==armv4t::Interpreter::Result::Swi)
                throw std::runtime_error("SWI exception routing is not implemented yet");
            ++retired;cycles+=cost;
        }
        std::printf("{\"execution\":\"ARM-discovery-interpreter\",\"instructions\":%llu,\"cycles\":%llu,\"pc\":%u,\"limited\":true}\n",
                    static_cast<unsigned long long>(retired),static_cast<unsigned long long>(cycles),machine.cpu.R[15]);
        return 0;
    }catch(const std::exception& error) {
        std::fprintf(stderr,"%s\n",error.what());
        std::printf("{\"execution\":\"ARM-discovery-interpreter\",\"instructions\":%llu,\"cycles\":%llu,\"pc\":%u,\"raw\":%u,\"stopped\":true}\n",
                    static_cast<unsigned long long>(retired),static_cast<unsigned long long>(cycles),pc,raw);
        return 1;
    }
}
