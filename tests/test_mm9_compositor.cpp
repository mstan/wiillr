// SPDX-License-Identifier: MIT
#include <wiillr/mm9_compositor.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
std::vector<uint8_t> ram(24*1024*1024);
constexpr uint32_t config=0x80001000,graphics=0x80600000,sprite_graphics=0x80700000,pool=0x8037c428,palettes=0x803bb320;
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
const uint8_t* read(void*,uint32_t address,uint32_t size){address&=0x1fffffff;return address<=ram.size() && size<=ram.size()-address?ram.data()+address:nullptr;}
void p16(uint32_t a,uint16_t v){a&=0x1fffffff;ram.at(a)=v>>8;ram.at(a+1)=v&255;}
void p32(uint32_t a,uint32_t v){p16(a,v>>16);p16(a+2,v&65535);}
void fill(uint32_t a,unsigned n,uint8_t value){a&=0x1fffffff;std::fill_n(ram.begin()+a,n,value);}
uint16_t pixel(const WiiMm9Frame& f,unsigned x,unsigned y){const auto at=((y/4)*64+x/4)*32+(y%4)*8+(x%4)*2;return uint16_t(f.texture[at])<<8|f.texture[at+1];}
void sprite(unsigned id,unsigned priority,unsigned x,unsigned y,unsigned tile,bool limited=false,unsigned kind=0,unsigned flip=0) {
    p32(pool+id*8,(tile<<16)|priority);p32(pool+id*8+4,(y<<22)|(x<<12)|(flip<<10)|(unsigned(limited)<<9)|(kind<<6));
    p32(pool+0x808,pool+(id+1)*8);
}
void fixture() {
    std::fill(ram.begin(),ram.end(),0);p32(0x80301280+0x40,config);p32(0x80301280,1u<<24);p32(config,0x100);
    p32(0x8043ca80+0x258,graphics);p32(0x8043ca80+0x25c,sprite_graphics);
    constexpr unsigned ids[]{1,0x12,0x24,0x38};for(unsigned n=0;n<4;++n)p32(0x80281e48+n*4,ids[n]);
    p16(config+8,1);fill(graphics+0x10000,32,0x11);
    for(unsigned group=0;group<2;++group){p32(palettes+0x10+group*4,0x80500000+group*0x1000);p32(palettes+0x20+group*4,16);}
    p16(0x80500002,0xf800);p16(0x80500006,0x003e); // red and blue in RGB5551
    p16(0x80501004,0x07c0);p16(0x80501006,0x003e); // green and blue sprite palette
    p32(pool+0x80c,pool);p32(pool+0x808,pool);
}
}
int main() {
    try {
        fixture();const WiiMm9Memory memory{nullptr,read};WiiMm9Frame frame{};char error[192];
        require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,0,8)==0xf800 && pixel(frame,255,231)==0xf800 && pixel(frame,0,0)==0,"Logical background and RGB565 tiled output");
        require(frame.background_pixels==256*224 && frame.sprite_pixels==0,"Logical background coverage");
        fill(sprite_graphics,32,0x22);sprite(0,0,4,3,0);
        require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,4,11)==0x07e0 && pixel(frame,3,11)==0xf800 && pixel(frame,12,11)==0xf800,"Sprite placement, bounds and RGB555 green expansion");
        fill(sprite_graphics+32,32,0x33);sprite(1,0,4,3,1);
        require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,4,11)==0x001f,"Later sprite order within one priority");
        ram[(sprite_graphics+32)&0x1fffffff]=0x03;
        require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,4,11)==0x07e0 && pixel(frame,5,11)==0x001f,"Transparent palette zero preserves prior sprite");
        sprite(1,2,4,3,1);require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,5,11)==0x07e0,"Priority crosses background and sprite families");
        p32(pool+0x808,pool+8);fill(sprite_graphics,32,0x33);ram[sprite_graphics&0x1fffffff]=0x23;
        sprite(0,0,4,3,0,false,0,1);require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,4,11)==0x001f && pixel(frame,11,11)==0x07e0,"Horizontal sprite mirror uses logical texture axes");
        sprite(0,0,4,3,0,false,0,2);require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,4,11)==0x001f && pixel(frame,4,18)==0x07e0,"Vertical sprite mirror uses logical texture axes");
        fill(sprite_graphics,32,0x22);
        p32(0x803b930c,1);sprite(0,0,4,223,0,true);sprite(1,0,4,223,1,true);
        require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,5,231)==0x07e0 && frame.limited_sprite_rows==1 && frame.final_sprite_count==1,"Per-line sprite limit and published final counter");
        // No-op entries still consume the original optional line budget.
        sprite(0,0,4,223,0,true,1);require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,5,231)==0xf800 && frame.final_sprite_count==1,"No-op sprite still participates in line limit");
        p32(pool+0x808,pool);p32(0x803b930c,0);
        p16(graphics+2,0x0040);fill(graphics+0x10000+32,32,0x33);
        ram[0x39b220]=1;p32(0x8039b190+0x94,0x80502000);for(unsigned y=0;y<224;++y)p16(0x80502000+y*2,8);
        require(wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)),error);
        require(pixel(frame,0,8)==0x001f && pixel(frame,8,8)==0xf800,"Live per-line horizontal scroll and logical tile selection");
        const auto original=frame;p32(pool+0x808,pool+8);sprite(0,0,4,3,0,false,2);
        require(!wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)) && std::memcmp(&frame,&original,sizeof(frame))==0,"Unsupported sprite family must publish no partial texture");
        std::cout<<"MM9 logical tile/sprite compositor contracts passed; live copy/queue qualification pending\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
