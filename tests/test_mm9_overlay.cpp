// SPDX-License-Identifier: MIT
#include "wiillr/mm9_overlay.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
namespace {
std::array<uint8_t,16384> ram{};
const uint8_t* read(void*,uint32_t a,uint32_t n){return a<=ram.size() && n<=ram.size()-a?ram.data()+a:nullptr;}
uint64_t writes=0,uploads=0,draws=0,destroys=0;std::vector<GcWiiNativeVertex> vertices;
void byte(uint32_t a,uint8_t value){ram[a]=value;}
void half(uint32_t a,uint16_t value){byte(a,uint8_t(value>>8));byte(a+1,uint8_t(value));}
void word(uint32_t a,uint32_t value){half(a,uint16_t(value>>16));half(a+2,uint16_t(value));}
void write_byte(void*,uint32_t a,uint8_t value){++writes;byte(a,value);}
uint64_t create(const GcWiiNativeMesh* input){vertices.assign(input->vertices,input->vertices+input->vertex_count);return ++uploads;}
int destroy(uint64_t){++destroys;return 1;}
int draw(const GcWiiNativeDraw* job){if(job->material.texture.address!=0x3000)return 0;++draws;return 1;}
}
int main() {
    word(0x84,0x1000);word(0x104,0x200);word(0x144,1);word(0x204,0x300);
    byte(0x301,9);half(0x302,0);byte(0x304,255);byte(0x305,6);byte(0x306,7);byte(0x307,1);
    word(0x308,0x400);word(0x310,0x500);byte(0x314,8);byte(0x315,8);byte(0x316,7);
    byte(0x400,7);byte(0x401,7);word(0x404,512);half(0x408,1);half(0x40c,4);half(0x40e,4);
    half(0x410,32);half(0x412,32);word(0x414,0x3000);half(0x500,0x41);half(0x502,0x5a);
    word(0x1004,0x80);word(0x1008,1);word(0x100c,1);half(0x1010,0x41);half(0x1012,0x41);half(0x1014,10);half(0x1016,0x41);
    half(0x1410,100);half(0x1412,20);half(0x1414,16);half(0x1416,16);byte(0x1418,255);byte(0x1419,1);byte(0x141a,2);
    const GcWiiNativeRenderApi api{GC_WII_NATIVE_RENDER_ABI,create,destroy,draw,nullptr};
    auto* renderer=wii_mm9_overlay_create({nullptr,read},write_byte,&api);CHECK(renderer);
    GcWiiNativeDraw view{};view.width=640;view.height=480;view.clip_width=640;view.clip_height=480;view.far_depth=1;
    CHECK(wii_mm9_overlay_draw(renderer,0x80,0x100,0x140,32,&view)==1);
    CHECK(uploads==1 && draws==1 && writes==1 && ram[0x210]==1 && vertices.size()==12);
    CHECK(vertices[0].position[0]==84 && vertices[0].position[1]==20 && vertices[0].position[2]==50);
    CHECK(vertices[4].position[0]==98 && vertices[8].position[0]==91 && vertices[8].position[1]==38);
    CHECK(vertices[0].uv[0]==1.f/32 && vertices[0].uv[1]==1.f/32 && vertices[0].color[3]==1);
    CHECK(wii_mm9_overlay_draw(renderer,0x80,0x100,0x140,32,&view)==1);
    CHECK(uploads==1 && draws==2 && writes==2);
    // UTF-8 uses Unicode glyph lookup; text encoding is independent of font ID.
    word(0x100c,0);word(0x140,0);std::memcpy(ram.data()+0x1010,"AA\nA",5);
    CHECK(wii_mm9_overlay_draw(renderer,0x80,0x100,0x140,32,&view)==1);
    CHECK(uploads==1 && draws==3 && writes==3 && ram[0x210]==0);
    // CP1252 Euro must select U+20AC, not the glyph for byte 0x80.
    word(0x508,0x580);half(0x580,0x20ac);half(0x582,0x20ac);half(0x58c,1);
    word(0x100c,3);word(0x14c,3);byte(0x1010,0x80);byte(0x1011,0);
    CHECK(wii_mm9_overlay_draw(renderer,0x80,0x100,0x140,32,&view)==1);
    CHECK(vertices.size()==4 && vertices[0].uv[0]==9.f/32 && ram[0x210]==3);
    const auto valid_uploads=uploads,valid_draws=draws,valid_writes=writes;
    // The full list validates before a valid first node may draw or publish.
    word(0x1004,0x1800);word(0x1804,0x80);word(0x1808,0);byte(0x210,3);
    CHECK(wii_mm9_overlay_draw(renderer,0x80,0x100,0x140,32,&view)==0);
    CHECK(uploads==valid_uploads && draws==valid_draws && writes==valid_writes && ram[0x210]==3);
    // Cycles and missing string terminators reject without side effects.
    word(0x1804,0x1000);word(0x1808,1);
    CHECK(!wii_mm9_overlay_draw(renderer,0x80,0x100,0x140,32,&view));
    word(0x1004,0x80);word(0x100c,1);std::memset(ram.data()+0x1010,0xff,1024);
    CHECK(!wii_mm9_overlay_draw(renderer,0x80,0x100,0x140,32,&view));
    CHECK(uploads==valid_uploads && draws==valid_draws && writes==valid_writes);
    // Empty-list publication retires only this renderer's retained geometry.
    word(0x84,0x80);CHECK(wii_mm9_overlay_draw(renderer,0x80,0x100,0x140,32,&view)==1);
    CHECK(destroys==uploads);wii_mm9_overlay_destroy(renderer);return 0;
}
