// SPDX-License-Identifier: MIT
#include "wiillr/menu_text.h"
#include <array>
#include <cstdio>
#include <cstring>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
namespace {
std::array<uint8_t,16384> ram{};
const uint8_t* read(void*,uint32_t address,uint32_t size) {
    return address<=ram.size() && size<=ram.size()-address ? ram.data()+address:nullptr;
}
void byte(uint32_t a,uint8_t v){ram[a]=v;}
void half(uint32_t a,uint16_t v){byte(a,uint8_t(v>>8));byte(a+1,uint8_t(v));}
void word(uint32_t a,uint32_t v){half(a,uint16_t(v>>16));half(a+2,uint16_t(v));}
}
int main() {
    const WiiMenuMemory memory{nullptr,read};WiiMenuFontGlyph g{};char error[192];
    word(0x100,0x8166fe88);word(0x114,0x200);word(0x118,0x600);
    byte(0x201,9);half(0x202,20);byte(0x204,255);byte(0x205,6);byte(0x206,7);byte(0x207,1);
    word(0x208,0x300);word(0x210,0x500);byte(0x214,8);byte(0x215,8);byte(0x216,7);
    byte(0x300,7);byte(0x301,7);word(0x304,512);half(0x308,2);
    half(0x30c,4);half(0x30e,4);half(0x310,32);half(0x312,32);word(0x314,0x1000);
    half(0x500,0x41);half(0x502,0x5a);
    CHECK(wii_menu_font_glyph(memory,0x100,0x41,&g,error,sizeof error));
    CHECK(g.left==-1 && g.advance==7 && g.glyph_width==6 && g.glyph_height==7);
    CHECK(g.cell_x==1 && g.cell_y==1 && g.texture.address==0x1000 && g.texture.size==512);
    CHECK(wii_menu_font_glyph(memory,0x100,0x3042,&g,error,sizeof error));
    CHECK(g.cell_x==1 && g.cell_y==9 && g.texture.address==0x1200);
    // Table and sparse maps use Unicode code units without host wchar_t.
    half(0x500,0x3042);half(0x502,0x3042);half(0x504,1);half(0x50c,5);
    CHECK(wii_menu_font_glyph(memory,0x100,0x3042,&g,error,sizeof error));
    CHECK(g.cell_x==9 && g.cell_y==9);
    half(0x504,2);half(0x50c,1);half(0x50e,0x3042);half(0x510,6);
    CHECK(wii_menu_font_glyph(memory,0x100,0x3042,&g,error,sizeof error));
    CHECK(g.cell_x==17 && g.cell_y==9);
    // Archive remapping changes sheet storage, not original glyph metrics.
    word(0x100,0x8166ff70);half(0x510,20);half(0x600,0xffff);half(0x602,16);
    CHECK(wii_menu_font_glyph(memory,0x100,0x3042,&g,error,sizeof error));
    CHECK(g.cell_x==1 && g.cell_y==9 && g.texture.address==0x1000);
    // Rejection must preserve the caller's output in full.
    const auto unchanged=g;
    word(0x508,0x500);
    CHECK(!wii_menu_font_glyph(memory,0x100,0x1234,&g,error,sizeof error));
    CHECK(std::memcmp(&unchanged,&g,sizeof g)==0);word(0x508,0);
    half(0x30a,8);
    CHECK(!wii_menu_font_glyph(memory,0x100,0x3042,&g,error,sizeof error));
    CHECK(std::memcmp(&unchanged,&g,sizeof g)==0);half(0x30a,0);
    word(0x314,0xfffffff0);
    CHECK(!wii_menu_font_glyph(memory,0x100,0x3042,&g,error,sizeof error));
    CHECK(std::memcmp(&unchanged,&g,sizeof g)==0);
    return 0;
}
