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
        const auto original=frame;
        // Actual neighboring map pages and sprites outside the old crop.
        fixture();p16(config+8,0x4001);
        p16(graphics+2048,0x0040);fill(graphics+0x10000+32,32,0x33);
        fill(sprite_graphics,32,0x22);sprite(0,0,260,10,0);
        WiiMm9Frame stock{};require(wii_mm9_composite_prepare(memory,&stock,error,sizeof error),error);
        std::vector<uint8_t> wide(344*240*4);
        require(wii_mm9_composite_view_prepare(memory,344,wide.data(),unsigned(wide.size()),error,sizeof error),error);
        WiiMm9Frame combined{};
        require(wii_mm9_composite_present_prepare(memory,344,&combined,wide.data(),unsigned(wide.size()),error,sizeof error),error);
        require(!std::memcmp(&stock,&combined,sizeof stock),"Combined traversal must preserve the complete original output and counters");
        const auto view_pixel=[&](unsigned x,unsigned y){const auto* p=wide.data()+(y*344+x)*4;return uint16_t((p[0]>>3)<<11|(p[1]>>2)<<5|(p[2]>>3));};
        for(unsigned y=0;y<240;++y)for(unsigned x=0;x<256;++x)
            require(view_pixel(x+44,y)==pixel(stock,x,y),"Wide view must preserve every original center pixel");
        require(view_pixel(44+256,8)==0x001f,"Right margin must read the next map page");
        require(view_pixel(44+260,18)==0x07e0,"Offscreen sprite must be visible in the new margin");
        // The standard world loader's visible cache can be valid while its
        // other half still contains old columns. Margins use mutable world
        // metatiles, with level bounds, without changing the original center.
        constexpr uint32_t scene=0x8031bf90,grid=0x8031c814,definitions=0x80504000,layout=0x80505000;
        p32(scene+4,0x8009f1a8);p32(scene+0x5c,1);
        p32(scene+0x20,definitions);p32(scene+0x28,layout);
        p32(scene+0x14,16); // Published scroll can differ from the target at +34.
        p32(0x8031c1e4,definitions);p32(0x8031c1e0,definitions+16);
        p32(layout,0x02000201);p16(grid,32);
        for(unsigned n=0;n<32*14;++n)p16(grid+4+n*2,1);
        p16(definitions+8,0);p16(definitions+10,0x0040);
        p16(definitions+12,0x0080);p16(definitions+14,0x00c0);
        p16(0x80500004,0x07c0);fill(graphics+0x10000+64,32,0x22);
        p16(config+0x10,16);
        require(wii_mm9_composite_prepare(memory,&stock,error,sizeof error),error);
        require(wii_mm9_composite_present_prepare(memory,344,&combined,wide.data(),unsigned(wide.size()),error,sizeof error),error);
        require(!std::memcmp(&stock,&combined,sizeof stock),"Direct world margins keep stock pixels/counters exact");
        require(view_pixel(44-8,8)==0x001f && view_pixel(44+280,8)==0x001f,"Uncached margins come from real world cells");
        require(view_pixel(44-16,8)==0xf800 && view_pixel(44-16,16)==0x07e0 &&
                view_pixel(44-8,16)==0,"World metatile quadrants preserve order and transparency");
        require(view_pixel(0,8)==0,"Negative world coordinates cannot wrap to the opposite side");
        p16(grid+4+(296/16)*2,0);
        require(wii_mm9_composite_present_prepare(memory,344,&combined,wide.data(),unsigned(wide.size()),error,sizeof error),error);
        require(view_pixel(44+280,8)==0xf800,"Mutable world cells update without refreshing the rolling cache");
        p32(scene+0x14,256);p16(config+0x10,256);
        require(wii_mm9_composite_present_prepare(memory,344,&combined,wide.data(),unsigned(wide.size()),error,sizeof error),error);
        require(view_pixel(44+280,8)==0,"World right edge cannot repeat cached foreground");
        const auto valid_world=wide;p16(grid,31);
        require(!wii_mm9_composite_present_prepare(memory,344,&combined,wide.data(),unsigned(wide.size()),error,sizeof error) && wide==valid_world,"Invalid world dimensions publish no partial view");
        p32(scene+4,0);
        const auto before=wide;
        require(!wii_mm9_composite_view_prepare(memory,345,wide.data(),unsigned(wide.size()),error,sizeof error) && wide==before,"Invalid view must not partially publish");
        frame=original;p32(pool+0x808,pool+8);sprite(0,0,4,3,0,false,2);
        require(!wii_mm9_composite_prepare(memory,&frame,error,sizeof(error)) && std::memcmp(&frame,&original,sizeof(frame))==0,"Unsupported sprite family must publish no partial texture");
        std::cout<<"MM9 logical tile/sprite compositor contracts passed; live copy/queue qualification pending\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
