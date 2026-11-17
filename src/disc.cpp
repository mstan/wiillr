/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/disc.h"
#include "wiillr/aes128.hpp"
#include "wiillr/sha1.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {
uint32_t be32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
bool seek(FILE* file,uint64_t offset){
#ifdef _WIN32
    return offset<=INT64_MAX && !_fseeki64(file,static_cast<int64_t>(offset),SEEK_SET);
#else
    return offset<=INT64_MAX && !fseeko(file,static_cast<off_t>(offset),SEEK_SET);
#endif
}
bool raw(FILE* file,uint64_t offset,void* output,size_t size){return seek(file,offset) && fread(output,1,size,file)==size;}
void cbc(const wiillr::Aes128Key& key,const uint8_t* iv,uint8_t* data,size_t size){
    std::array<uint8_t,16> previous;std::memcpy(previous.data(),iv,16);
    for(size_t i=0;i<size;i+=16){
        std::array<uint8_t,16> block,cipher;std::memcpy(block.data(),data+i,16);cipher=block;
        wiillr::aes128_decrypt(key,block);
        for(unsigned j=0;j<16;++j)data[i+j]=block[j]^previous[j];
        previous=cipher;
    }
}
std::array<uint8_t,20> sha1_1k(const uint8_t* data){
    std::array<uint32_t,5> h={0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
    for(unsigned i=0;i<1024;i+=64)wiillr::sha1_compress(h,data+i);
    std::array<uint8_t,64> tail={0x80};tail[62]=0x20;
    wiillr::sha1_compress(h,tail.data());
    std::array<uint8_t,20> result;
    for(unsigned i=0;i<20;++i)result[i]=uint8_t(h[i/4]>>(24-8*(i%4)));
    return result;
}
}
struct WiiDisc {
    FILE* file=nullptr;
    uint64_t base=0,size=0,disc_size=0,partition=0,sector=UINT64_MAX;
    uint32_t ios=0;
    wiillr::Aes128Key key{};
    std::array<uint8_t,0x7c00> cache{};
    ~WiiDisc(){if(file)fclose(file);}
};
extern "C" WiiDisc* wii_disc_open(const char* iso,const char* common_key){
    if(!iso || !common_key)return nullptr;
    auto d=std::make_unique<WiiDisc>();d->file=fopen(iso,"rb");
    if(!d->file)return nullptr;
#ifdef _WIN32
    if(_fseeki64(d->file,0,SEEK_END))return nullptr;
    const auto length=_ftelli64(d->file);
#else
    if(fseeko(d->file,0,SEEK_END))return nullptr;
    const auto length=ftello(d->file);
#endif
    if(length<0)return nullptr;d->disc_size=uint64_t(length);
    std::array<uint8_t,16> key;FILE* k=fopen(common_key,"rb");
    if(!k)return nullptr;
    const bool ok=fread(key.data(),1,16,k)==16 && fgetc(k)==EOF;fclose(k);if(!ok)return nullptr;
    std::array<uint8_t,0x440> header{};
    if(!raw(d->file,0,header.data(),0x100) || be32(header.data()+0x18)!=0x5d1c9ea3)return nullptr;
    std::array<uint8_t,32> groups;
    if(!raw(d->file,0x40000,groups.data(),groups.size()))return nullptr;
    unsigned found=0;uint64_t partition=0;
    for(unsigned group=0;group<4;++group){
        const auto count=be32(groups.data()+group*8);const uint64_t table=uint64_t(be32(groups.data()+group*8+4))*4;
        if(count>256)return nullptr;
        for(unsigned i=0;i<count;++i){std::array<uint8_t,8> entry;
            if(!raw(d->file,table+i*8,entry.data(),entry.size()))return nullptr;
            if(be32(entry.data()+4)==0){partition=uint64_t(be32(entry.data()))*4;++found;}
        }
    }
    if(found!=1 || !raw(d->file,partition,header.data(),0x2c0) || header[0x1f1])return nullptr;
    d->partition=partition;
    std::array<uint8_t,16> iv{};std::memcpy(iv.data(),header.data()+0x1dc,8);
    std::array<uint8_t,16> titlekey;std::memcpy(titlekey.data(),header.data()+0x1bf,16);
    cbc(wiillr::aes128_expand(key),iv.data(),titlekey.data(),16);d->key=wiillr::aes128_expand(titlekey);
    d->base=partition+uint64_t(be32(header.data()+0x2b8))*4;
    const auto encrypted=uint64_t(be32(header.data()+0x2bc))*4;
    if(!encrypted || encrypted%0x8000 || d->base>d->disc_size || encrypted>d->disc_size-d->base)return nullptr;
    d->size=encrypted/0x8000*0x7c00;
    const auto tmd=partition+uint64_t(be32(header.data()+0x2a8))*4;
    std::array<uint8_t,0x1e4> metadata;
    if(be32(header.data()+0x2a4)<metadata.size() || !raw(d->file,tmd,metadata.data(),metadata.size()) ||
       be32(metadata.data())!=0x10001 || be32(metadata.data()+0x184)!=1 ||
       std::memcmp(metadata.data()+0x18c,header.data()+0x1dc,8))return nullptr;
    d->ios=be32(metadata.data()+0x188);
    if(!wii_disc_read(d.get(),0,header.data(),0x440))return nullptr;
    return d.release();
}
extern "C" bool wii_disc_read(WiiDisc* d,uint64_t offset,void* destination,size_t size){
    if(!d || (!destination && size) || offset>d->size || size>d->size-offset)return false;
    auto* output=static_cast<uint8_t*>(destination);
    while(size){
        const uint64_t index=offset/0x7c00;const size_t within=offset%0x7c00;
        if(d->sector!=index){
            std::array<uint8_t,0x8000> block;
            if(!raw(d->file,d->base+index*0x8000,block.data(),block.size()))return false;
            std::array<uint8_t,16> iv;std::memcpy(iv.data(),block.data()+0x3d0,16);
            const std::array<uint8_t,16> zero{};
            cbc(d->key,zero.data(),block.data(),0x400);cbc(d->key,iv.data(),block.data()+0x400,0x7c00);
            for(unsigned n=0;n<31;++n){const auto hash=sha1_1k(block.data()+0x400+n*0x400);
                if(std::memcmp(hash.data(),block.data()+n*20,20))return false;}
            std::memcpy(d->cache.data(),block.data()+0x400,d->cache.size());d->sector=index;
        }
        const size_t amount=std::min(size,0x7c00-within);
        std::memcpy(output,d->cache.data()+within,amount);output+=amount;offset+=amount;size-=amount;
    }
    return true;
}
extern "C" void wii_disc_close(WiiDisc* d){delete d;}
extern "C" uint32_t wii_disc_ios(const WiiDisc* d){return d?d->ios:0;}
extern "C" uint64_t wii_disc_partition(const WiiDisc* d){return d?d->partition:0;}
extern "C" uint64_t wii_disc_size(const WiiDisc* d){return d?d->disc_size:0;}
extern "C" bool wii_disc_boot_header(WiiDisc* d,void* output,size_t size){return size==0x440 && wii_disc_read(d,0,output,size);}
