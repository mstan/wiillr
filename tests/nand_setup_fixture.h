/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "wiillr/sha1.hpp"
#include "menu_profile_fixture.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace nand_fixture {
using Bytes=std::vector<uint8_t>;
inline void put(Bytes& b,size_t at,uint64_t value,unsigned width=4){for(unsigned i=0;i<width;++i)b.at(at+width-1-i)=uint8_t(value>>(i*8));}
inline std::array<uint8_t,20> sha(const Bytes& input){
    Bytes b=input;const uint64_t bits=b.size()*8;b.push_back(128);while(b.size()%64!=56)b.push_back(0);
    for(int i=7;i>=0;--i)b.push_back(uint8_t(bits>>(8*i)));
    std::array<uint32_t,5> h={0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
    for(size_t i=0;i<b.size();i+=64)wiillr::sha1_compress(h,b.data()+i);
    std::array<uint8_t,20> result{};for(unsigned i=0;i<20;++i)result[i]=uint8_t(h[i/4]>>(24-8*(i%4)));return result;
}
inline Bytes bytes(const std::string& s){return {s.begin(),s.end()};}
inline std::map<std::string,Bytes> files(){
    Bytes dol(260);put(dol,0,256);put(dol,0x48,0x81300000);put(dol,0x90,4);put(dol,0xe0,0x81300000);
    std::copy_n("TEST",4,dol.begin()+256);
    const Bytes ios=bytes("synthetic IOS content, never executable firmware");
    const auto title=[&](uint64_t id,uint64_t sys,uint16_t version,const Bytes& content){
        Bytes t(0x140+0xa4+36);put(t,0,0x10001);put(t,0x184,sys,8);put(t,0x18c,id,8);put(t,0x198,1,2);
        put(t,0x1dc,version,2);put(t,0x1de,1,2);put(t,0x1e0,0,2);
        put(t,0x1e4,0);put(t,0x1e8,0,2);put(t,0x1ea,1,2);put(t,0x1ec,content.size(),8);
        const auto hash=sha(content);std::copy(hash.begin(),hash.end(),t.begin()+0x1f4);return t;
    };
    const auto ticket=[](uint64_t id){Bytes t(0x2a4);put(t,0,0x10001);put(t,0x1dc,id,8);return t;};
    Bytes uid(24);put(uid,0,0x100000002ull,8);put(uid,8,4096);put(uid,12,0x100000050ull,8);put(uid,20,4097);
    auto sc=bytes(menu_profile_fixture());
    Bytes setting(256);const std::string text="AREA=USA\r\nMODEL=RVL-001(USA)\r\n";std::copy(text.begin(),text.end(),setting.begin());
    uint32_t key=0x73b5dbfa;for(auto& c:setting){c^=uint8_t(key);key=std::rotl(key,1);}
    Bytes save(0x5000);for(size_t i=0;i<save.size();++i)save[i]=uint8_t(i*17+i/251);
    return {{"title/00000001/00000002/content/title.tmd",title(0x100000002ull,0x100000050ull,513,dol)},
      {"title/00000001/00000002/content/00000000.app",dol},
      {"title/00000001/00000002/data/setting.txt",setting},
      {"title/00000001/00000002/data/native-save",save},
      {"title/00000001/00000050/content/title.tmd",title(0x100000050ull,0,6944,ios)},
      {"title/00000001/00000050/content/00000000.app",ios},
      {"ticket/00000001/00000002.tik",ticket(0x100000002ull)},
      {"ticket/00000001/00000050.tik",ticket(0x100000050ull)},
      {"sys/uid.sys",uid},{"sys/cert.sys",bytes("synthetic certificate container")},
      {"shared2/sys/SYSCONF",sc},{"empty",{}}};
}
inline void write(const std::filesystem::path& p,const Bytes& b){std::filesystem::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write((const char*)b.data(),b.size());if(!f)throw std::runtime_error("fixture write");}
inline void folder(const std::filesystem::path& root){for(const auto& [name,data]:files())write(root/name,data);}
}
