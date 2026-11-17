// SPDX-License-Identifier: GPL-3.0-or-later
#include "wiillr/menu_text.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>
#include <limits>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
namespace {
std::array<uint8_t,16384> ram{};
const uint8_t* read(void*,uint32_t address,uint32_t size) {
    return address<=ram.size() && size<=ram.size()-address ? ram.data()+address:nullptr;
}
void byte(uint32_t a,uint8_t v){ram[a]=v;}
void half(uint32_t a,uint16_t v){byte(a,uint8_t(v>>8));byte(a+1,uint8_t(v));}
void word(uint32_t a,uint32_t v){half(a,uint16_t(v>>16));half(a+2,uint16_t(v));}
std::vector<GcWiiNativeVertex> uploaded;
uint64_t uploads=0,destroys=0,draws=0;
GcWiiNativeGuiDraw last_draw{};
uint64_t create_mesh(const GcWiiNativeMesh* mesh) {
    uploaded.assign(mesh->vertices,mesh->vertices+mesh->vertex_count);return ++uploads;
}
int destroy_mesh(uint64_t){++destroys;return 1;}
int draw_gui(const GcWiiNativeGuiDraw* job) {
    if(job->color.low[0]!=255 || job->color.high[0]!=255 || job->color.low[3]!=0 || job->color.high[3]!=255)return 0;
    last_draw=*job;++draws;return 1;
}
}
int main() {
    const WiiMenuMemory memory{nullptr,read};WiiMenuFontGlyph g{};char error[192];
    word(0x100,0x8166fe88);word(0x114,0x200);word(0x118,0x600);
    byte(0x201,9);half(0x202,20);byte(0x204,255);byte(0x205,6);byte(0x206,7);byte(0x207,1);
    word(0x208,0x300);word(0x210,0x500);byte(0x214,8);byte(0x215,8);byte(0x216,7);
    byte(0x300,7);byte(0x301,7);byte(0x302,6);word(0x304,512);half(0x308,2);
    half(0x30c,4);half(0x30e,4);half(0x310,32);half(0x312,32);word(0x314,0x1000);
    half(0x500,0x41);half(0x502,0x5a);
    CHECK(wii_menu_font_glyph(memory,0x100,0x41,&g,error,sizeof error));
    CHECK(g.left==-1 && g.advance==7 && g.glyph_width==6 && g.glyph_height==7);
    CHECK(g.cell_x==1 && g.cell_y==1 && g.texture.address==0x1000 && g.texture.size==512);
    // A resource caller does not need the Menu's font object or vtable.
    const auto bound=g;word(0x100,0xdeadbeef);
    CHECK(wii_resource_font_glyph(memory,0x200,0x41,0,&g,error,sizeof error));
    CHECK(std::memcmp(&bound,&g,sizeof g)==0);
    CHECK(!wii_menu_font_glyph(memory,0x100,0x41,&g,error,sizeof error));
    CHECK(std::memcmp(&bound,&g,sizeof g)==0);word(0x100,0x8166fe88);
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
    // Whole TextBox layout batches glyphs and retains geometry across draws.
    word(0x100,0x8166fe88);word(0x314,0x1000);
    half(0x500,0x41);half(0x502,0x5a);half(0x504,0);half(0x50c,0);
    word(0x2000,0x8167192c);word(0x2028,0x2400);word(0x204c,0x42c80000);word(0x2050,0x41a00000);
    for(unsigned at:{0u,5u,10u})word(0x2084+at*4,0x3f800000);
    byte(0x20ce,255);word(0x20d4,0x2700);word(0x20d8,0xffffffff);word(0x20dc,0xffffffff);
    word(0x20e0,0x100);word(0x20e4,0x41000000);word(0x20e8,0x41000000);word(0x20f0,0x3f800000);
    half(0x20f8,8);half(0x20fa,2);word(0x2400,0x81671b48);
    for(unsigned ch=0;ch<4;++ch)half(0x242c+ch*2,255);
    word(0x2540,0x3f800000);half(0x2700,0x41);half(0x2702,0x41);
    const GcWiiNativeRenderApi meshes{GC_WII_NATIVE_RENDER_ABI,create_mesh,destroy_mesh,nullptr,nullptr};
    const GcWiiNativeGuiApi gui{GC_WII_NATIVE_GUI_ABI,draw_gui};
    auto* renderer=wii_menu_text_create(memory,&meshes,&gui);CHECK(renderer);
    WiiMenuView view{};for(unsigned i:{0u,5u,10u,15u})view.projection[i]=1;
    view.width=view.height=64;view.clip_width=view.clip_height=64;view.far_depth=1;
    CHECK(wii_menu_text_draw(renderer,0x2000,0x2500,&view));
    CHECK(uploads==1 && draws==1 && uploaded.size()==8);
    CHECK(uploaded[0].position[0]==-1 && uploaded[0].position[1]==1);
    CHECK(uploaded[4].position[0]==7 && uploaded[4].position[1]==1);
    CHECK(wii_menu_text_draw(renderer,0x2000,0x2500,&view));CHECK(uploads==1 && draws==2);
    // An additional label borrows the real style without changing the guest
    // buffer or evicting the original TextBox's cached geometry each frame.
    WiiMenuTextLabel label{{'A','A',0},12,34,.6f};const auto guest_before=ram;
    CHECK(wii_menu_text_draw_label(renderer,0x2000,0x2500,&view,&label)==1);
    CHECK(ram==guest_before && uploads==2 && uploaded.size()==8);
    CHECK(last_draw.draw.model_view[3]==12 && last_draw.draw.model_view[7]==34 && last_draw.draw.model_view[0]==.6f);
    CHECK(uploaded[0].position[0]<0 && uploaded[6].position[0]>0);
    const auto label_uploads=uploads;
    CHECK(wii_menu_text_draw(renderer,0x2000,0x2500,&view)==1);
    CHECK(wii_menu_text_draw_label(renderer,0x2000,0x2500,&view,&label)==1 && uploads==label_uploads);
    label.x=std::numeric_limits<float>::infinity();const auto label_draws=draws;
    CHECK(!wii_menu_text_draw_label(renderer,0x2000,0x2500,&view,&label));
    CHECK(uploads==label_uploads && draws==label_draws && ram==guest_before);label.x=12;
    for(auto& ch:label.text)ch='A';
    CHECK(!wii_menu_text_draw_label(renderer,0x2000,0x2500,&view,&label));
    CHECK(uploads==label_uploads && draws==label_draws && ram==guest_before);
    // A constrained width wraps at a character boundary with the real line feed.
    word(0x204c,0x41000000);
    CHECK(wii_menu_text_draw(renderer,0x2000,0x2500,&view));
    CHECK(uploaded[4].position[0]==-1 && uploaded[4].position[1]==10);
    // Explicit line breaks and UTF-16 text share the same native batch route.
    word(0x508,0x700);half(0x700,0x3042);half(0x702,0x3042);half(0x70c,5);half(0x2700,0x3042);
    word(0x204c,0x42c80000);half(0x2702,10);half(0x2704,0x41);half(0x20fa,3);
    CHECK(wii_menu_text_draw(renderer,0x2000,0x2500,&view));
    CHECK(uploaded[4].position[0]==-1 && uploaded[4].position[1]==10);
    const auto before_uploads=uploads,before_draws=draws;word(0x20f4,0x3000);
    CHECK(!wii_menu_text_draw(renderer,0x2000,0x2500,&view));
    CHECK(uploads==before_uploads && draws==before_draws);
    // Standalone Glyph carries original atlas coordinates, while original
    // Print already applied the bearing. Cached vertex alpha is final: do
    // not apply the writer opacity again, and never advance its cursor here.
    const uint32_t writer=0x3000,value=0x3100;
    word(writer,0);word(writer+4,0xffffffff);
    word(writer+8,0xff000080);word(writer+12,0x00ff0040);
    word(writer+16,0x0000ff20);word(writer+20,0xffffff10);
    word(writer+0x24,0x3fc00000);word(writer+0x28,0x40000000);
    word(writer+0x2c,0x42f68000);byte(writer+0x42,17);
    word(writer+0x38,1);word(writer+0x3c,1);word(writer+0x48,0x100);
    word(value,0x1000);byte(value+4,254);byte(value+5,6);byte(value+6,7);byte(value+7,7);
    word(value+8,1);half(value+12,32);half(value+14,32);half(value+16,9);half(value+18,17);
    std::array<uint8_t,0x4c> writer_before;std::memcpy(writer_before.data(),ram.data()+writer,writer_before.size());
    float xyz[]{10,20,.25f},model[]{1,0,0,2,0,1,0,3,0,0,1,4};WiiMenuPicture quad{};WiiMenuCharWriterStyle style{};
    CHECK(wii_menu_charwriter_style(memory,writer,&style,error,sizeof error));
    CHECK(wii_menu_charwriter_validate(memory,writer,error,sizeof error));
    CHECK(wii_menu_charwriter_prepare(memory,writer,value,xyz,&view,model,&style,&quad,error,sizeof error));
    CHECK(quad.vertices[0].position[0]==10 && quad.vertices[0].position[1]==20);
    CHECK(quad.vertices[2].position[0]==19 && quad.vertices[2].position[1]==34 && quad.vertices[2].position[2]==.25f);
    CHECK(quad.vertices[0].uv[0]==9.f/32 && quad.vertices[2].uv[1]==24.f/32);
    CHECK(quad.vertices[0].color[3]==128.f/255 && quad.vertices[1].color[3]==64.f/255);
    CHECK(quad.vertices[2].color[3]==16.f/255 && quad.vertices[3].color[2]==1);
    CHECK(quad.draw.model_view[3]==2 && quad.draw.model_view[7]==3 && quad.draw.model_view[11]==4);
    CHECK(quad.color.low[0]==255 && quad.color.high[0]==255 && quad.color.low[3]==0);
    CHECK(std::memcmp(writer_before.data(),ram.data()+writer,writer_before.size())==0);
    word(writer,0x11223344);
    CHECK(wii_menu_charwriter_prepare(memory,writer,value,xyz,&view,model,&style,&quad,error,sizeof error));
    CHECK(quad.color.low[0]==255); // Setup snapshot survives a later class color-map edit.
    word(writer,0);
    const auto glyph_uploads=uploads,glyph_draws=draws;
    CHECK(wii_menu_charwriter_draw(renderer,writer,value,xyz,&view,model,&style)==1);
    CHECK(wii_menu_charwriter_draw(renderer,writer,value,xyz,&view,model,&style)==1);
    CHECK(uploads==glyph_uploads+1 && draws==glyph_draws+2);
    CHECK(std::memcmp(writer_before.data(),ram.data()+writer,writer_before.size())==0);
    // Resource/material failures occur before upload and preserve output.
    const auto untouched=quad;word(value+8,8);
    CHECK(!wii_menu_charwriter_prepare(memory,writer,value,xyz,&view,model,&style,&quad,error,sizeof error));
    CHECK(std::memcmp(&quad,&untouched,sizeof quad)==0);word(value+8,6);half(0x30a,6);
    CHECK(wii_menu_charwriter_style(memory,writer,&style,error,sizeof error));
    CHECK(wii_menu_charwriter_prepare(memory,writer,value,xyz,&view,model,&style,&quad,error,sizeof error));
    CHECK(quad.color.low[0]==0 && quad.color.high[0]==255); // RGBA keeps source RGB.
    word(writer+0x38,0);word(writer+0x3c,0);
    CHECK(wii_menu_charwriter_prepare(memory,writer,value,xyz,&view,model,&style,&quad,error,sizeof error));
    CHECK(quad.draw.material.texture.linear==0);
    const auto finite_quad=quad;word(writer+0x24,0x7f7fffff);
    CHECK(!wii_menu_charwriter_prepare(memory,writer,value,xyz,&view,model,&style,&quad,error,sizeof error));
    CHECK(std::memcmp(&quad,&finite_quad,sizeof quad)==0);word(writer+0x24,0x3fc00000);
    word(writer+0x38,1);word(writer+0x3c,0);const auto no_upload=uploads;
    CHECK(!wii_menu_charwriter_draw(renderer,writer,value,xyz,&view,model,&style));CHECK(uploads==no_upload);
    wii_menu_text_forget(renderer,0x2000);CHECK(destroys+1==uploads);
    wii_menu_text_destroy(renderer);CHECK(destroys==uploads);
    return 0;
}
