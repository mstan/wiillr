// SPDX-License-Identifier: MIT
#include <wiillr/mm9_indexed.h>
#include <cmath>
#include <cstdio>
#include <stdexcept>
namespace {
void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
}
extern "C" int wii_mm9_indexed_prepare(const WiiMm9IndexedTile* input,
        const WiiMm9Screen* canvas,WiiMm9Screen* output,char* error,uint32_t capacity) {
    if(!input || !canvas || !output){if(error && capacity)std::snprintf(error,capacity,"Missing indexed tile input");return 0;}
    try {
        const auto& t=*input;const auto& texture=canvas->draw.material.texture;
        need(texture.width==256 && texture.height==240 && texture.size==WII_MM9_TEXTURE_BYTES &&
             texture.format==4 && !texture.linear && (texture.address&0x1fffffffu)==0x0039b2c0 &&
             !(canvas->draw.material.flags&~15u),
            "Indexed canvas requires the original nearest RGB565 screen");
        need(t.source_width && t.source_width<=1024 && !(t.source_width&7) &&
             t.source_height && t.source_height<=1024 && !(t.source_height&7) &&
             t.source_columns && t.source_x<t.source_width && t.source_columns<=t.source_width-t.source_x &&
             t.source_rows && t.source_y<t.source_height && t.source_rows<=t.source_height-t.source_y && !(t.flips&~3u),
            "Invalid indexed asset rectangle");
        need(std::isfinite(t.x) && std::isfinite(t.y) && std::isfinite(t.width) && std::isfinite(t.height) &&
             std::abs(t.x)<=32768 && std::abs(t.y)<=32768 && t.width>0 && t.width<=1024 && t.height>0 && t.height<=1024,
            "Invalid indexed logical rectangle");
        constexpr float u[4]{0,0,1,1},v[4]{1,0,0,1};
        for(unsigned n=0;n<4;++n) {
            need(canvas->vertices[n].uv[0]==u[n] && canvas->vertices[n].uv[1]==v[n],"Cropped indexed canvas needs qualification");
            for(float value:canvas->vertices[n].position)need(std::isfinite(value) && std::abs(value)<=65536,"Unbounded indexed canvas");
            for(float value:canvas->vertices[n].color)need(std::isfinite(value) && value>=0 && value<=1,"Invalid indexed canvas tint");
        }
        const auto& origin=canvas->vertices[1];const auto& right=canvas->vertices[2];const auto& bottom=canvas->vertices[0];
        need(origin.position[1]==right.position[1] && origin.position[0]==bottom.position[0] &&
             right.position[0]==canvas->vertices[3].position[0] && bottom.position[1]==canvas->vertices[3].position[1] &&
             origin.position[0]<right.position[0] && origin.position[1]<bottom.position[1],"Nonrectangular indexed canvas");
        for(unsigned n=0;n<4;++n)need(canvas->vertices[n].position[2]==origin.position[2],"Nonplanar indexed canvas");
        WiiMm9Screen out=*canvas;
        out.draw.material.flags|=GC_WII_NATIVE_BLEND;
        out.draw.material.texture={t.source,(t.source_width/8)*(t.source_height/8)*32,8,t.source_width,t.source_height,
            t.palette,16,GC_WII_NATIVE_PALETTE_RGB5551_ZERO,0,0,0};
        const float xs[4]{t.x,t.x,t.x+t.width,t.x+t.width};
        const float ys[4]{t.y+t.height,t.y,t.y,t.y+t.height};
        const float us[2]{float(t.source_x)/t.source_width,float(t.source_x+t.source_columns)/t.source_width};
        const float vs[2]{float(t.source_y)/t.source_height,float(t.source_y+t.source_rows)/t.source_height};
        for(unsigned n=0;n<4;++n) {
            out.vertices[n].position[0]=origin.position[0]+xs[n]/256.f*(right.position[0]-origin.position[0]);
            out.vertices[n].position[1]=origin.position[1]+ys[n]/240.f*(bottom.position[1]-origin.position[1]);
            out.vertices[n].uv[0]=us[(unsigned(u[n])^unsigned(bool(t.flips&1)))];
            out.vertices[n].uv[1]=vs[(unsigned(v[n])^unsigned(bool(t.flips&2)))];
        }
        *output=out;if(error && capacity)*error=0;return 1;
    }catch(const std::exception& e){if(error && capacity)std::snprintf(error,capacity,"%s",e.what());return 0;}
}
