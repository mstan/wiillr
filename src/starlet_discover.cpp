/* SPDX-License-Identifier: MIT */
#include "wiillr/starlet.hpp"
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdio>
#include <stdexcept>

int main(int argc,char** argv) {
    if(argc<3 || argc>6) {std::fprintf(stderr,"usage: wiillr-starlet-discover ORIGINAL-IOS-CONTENT MAX-STEPS [ORIGINAL-128-BYTE-OTP [ORIGINAL-256-BYTE-SEEPROM [ORIGINAL-RAW-NAND]]]\n");return 2;}
    uint64_t retired=0,cycles=0,swis=0,irqs=0,undefined=0;
    uint32_t pc=0,lr=0;
    try {
        size_t used;
        const std::string count=argv[2];const uint64_t limit=std::stoull(count,&used,0);
        if(used!=count.size() || !limit || limit>1000000000ull)throw std::runtime_error("Invalid step limit");
        std::ifstream input(argv[1],std::ios::binary);
        if(!input)throw std::runtime_error("Cannot open IOS content");
        std::vector<uint8_t> binary((std::istreambuf_iterator<char>(input)),{});
        std::vector<uint8_t> mem1(0x1800000),mem2(0x4000000);
        wiillr::Starlet machine(mem1,mem2);
        if(argc>=4) {
            std::ifstream otp_input(argv[3],std::ios::binary);
            if(!otp_input)throw std::runtime_error("Cannot open original OTP input");
            const std::vector<uint8_t> otp((std::istreambuf_iterator<char>(otp_input)),{});
            machine.load_otp(otp);
        }
        if(argc>=5) {
            std::ifstream seeprom_input(argv[4],std::ios::binary);
            if(!seeprom_input)throw std::runtime_error("Cannot open original SEEPROM input");
            const std::vector<uint8_t> seeprom((std::istreambuf_iterator<char>(seeprom_input)),{});
            machine.load_seeprom(seeprom);
        }
        if(argc==6) {
            std::ifstream nand_input(argv[5],std::ios::binary|std::ios::ate);
            if(!nand_input)throw std::runtime_error("Cannot open original raw NAND input");
            const auto size=nand_input.tellg();
            if(size!=0x21000000 && size!=0x21000400)throw std::runtime_error("Expected 512MiB raw NAND with optional BootMii footer");
            std::vector<uint8_t> nand(0x21000000);nand_input.seekg(0);
            if(!nand_input.read(reinterpret_cast<char*>(nand.data()),nand.size()))throw std::runtime_error("Truncated original raw NAND input");
            machine.load_nand(std::move(nand));
        }
        machine.load_ios(binary);
        while(retired<limit) {
            pc=machine.cpu.R[15];
            lr=machine.cpu.R[14];
            const uint32_t cost=machine.step();
            ++retired;cycles+=cost;
            swis=machine.swi_entries();irqs=machine.irq_entries();undefined=machine.undefined_entries();
        }
        std::printf("{\"execution\":\"ARM-discovery-interpreter\",\"instructions\":%llu,\"cycles\":%llu,\"pc\":%u,\"swi_entries\":%llu,\"limited\":true}\n",
                    static_cast<unsigned long long>(retired),static_cast<unsigned long long>(cycles),machine.cpu.R[15],
                    static_cast<unsigned long long>(machine.swi_entries()));
        return 0;
    }catch(const std::exception& error) {
        std::fprintf(stderr,"%s\n",error.what());
        std::printf("{\"execution\":\"ARM-discovery-interpreter\",\"instructions\":%llu,\"cycles\":%llu,\"pc\":%u,\"lr\":%u,\"swi_entries\":%llu,\"irq_entries\":%llu,\"undefined_entries\":%llu,\"stopped\":true}\n",
                    static_cast<unsigned long long>(retired),static_cast<unsigned long long>(cycles),pc,lr,
                    static_cast<unsigned long long>(swis),static_cast<unsigned long long>(irqs),static_cast<unsigned long long>(undefined));
        return 1;
    }
}
