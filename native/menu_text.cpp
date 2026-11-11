// SPDX-License-Identifier: MIT
// NW4R resource contracts checked against the CC0 wii-ipl 43U annotations:
// ut_ResFontBase.cpp, ut_ArchiveFont.cpp and ut/fontResources.h.
#include "wiillr/menu_text.h"
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace {
void need(bool value,const char* error){if(!value)throw std::runtime_error(error);}
struct Reader {
    WiiMenuMemory memory;
    uint32_t at(uint32_t base,uint32_t offset)const {
        need(offset<=UINT32_MAX-base,"Font address overflow");return base+offset;
    }
    const uint8_t* bytes(uint32_t address,uint32_t size)const {
        need(size<=UINT32_MAX-address,"Font span overflow");
        const auto* p=memory.read(memory.user,address,size);
        need(p!=nullptr,"Unreadable font resource span");return p;
    }
    uint8_t u8(uint32_t a)const{return *bytes(a,1);}
    int8_t s8(uint32_t a)const{return int8_t(u8(a));}
    uint16_t u16(uint32_t a)const{const auto* p=bytes(a,2);return uint16_t(uint16_t(p[0])<<8|p[1]);}
    uint32_t u32(uint32_t a)const{const auto* p=bytes(a,4);return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
};
uint16_t lookup(const Reader& r,uint32_t map,uint16_t code) {
    std::unordered_set<uint32_t> seen;
    while(map) {
        need(seen.size()<512 && seen.insert(map).second,"Cyclic or excessive font maps");
        r.bytes(map,12);
        const auto first=r.u16(map),last=r.u16(r.at(map,2));
        need(first<=last,"Invalid font character range");
        if(code>=first && code<=last) {
            const auto info=r.at(map,12);
            switch(r.u16(r.at(map,4))) {
            case 0:return uint16_t(code-first+r.u16(info));
            case 1:return r.u16(r.at(info,uint32_t(code-first)*2));
            case 2:{
                uint32_t low=0,high=r.u16(info);
                r.bytes(r.at(info,2),high*4);
                while(low<high) {
                    const auto mid=low+(high-low)/2,entry=r.at(info,2+mid*4);
                    const auto key=r.u16(entry);
                    if(key<code)low=mid+1;
                    else if(key>code)high=mid;
                    else return r.u16(r.at(entry,2));
                }
                return 0xffff;
            }
            default:throw std::runtime_error("Unsupported font map method");
            }
        }
        map=r.u32(r.at(map,8));
    }
    return 0xffff;
}
uint32_t widths(const Reader& r,uint32_t info,uint16_t index) {
    auto block=r.u32(r.at(info,12));std::unordered_set<uint32_t> seen;
    while(block) {
        need(seen.size()<512 && seen.insert(block).second,"Cyclic or excessive font widths");
        r.bytes(block,8);const auto first=r.u16(block),last=r.u16(r.at(block,2));
        need(first<=last,"Invalid font width range");
        if(index>=first && index<=last)return r.at(block,8+uint32_t(index-first)*3);
        block=r.u32(r.at(block,4));
    }
    return r.at(info,4);
}
uint32_t image_size(uint32_t format,uint32_t w,uint32_t h) {
    uint32_t bw=4,bh=4,bytes=32;
    switch(format) {
    case 0:case 14:bw=bh=8;break;
    case 1:case 2:bw=8;break;
    case 3:case 4:case 5:break;
    case 6:bytes=64;break;
    default:throw std::runtime_error("Unsupported font texture format");
    }
    return ((w+bw-1)/bw)*((h+bh-1)/bh)*bytes;
}
WiiMenuFontGlyph glyph(const Reader& r,uint32_t font,uint16_t code) {
    const auto vtable=r.u32(font);
    need(vtable==0x8166fe88 || vtable==0x8166ff70,"Unbound Menu font class");
    const bool archive=vtable==0x8166ff70;
    const auto info=r.u32(r.at(font,0x14));r.bytes(info,24);
    need(r.u8(r.at(info,7))==1,"Menu text requires UTF-16 font encoding");
    const auto texture=r.u32(r.at(info,8));r.bytes(texture,24);
    const auto row=r.u16(r.at(texture,12)),line=r.u16(r.at(texture,14));
    need(row && line,"Empty font sheet grid");
    const uint32_t per_sheet=uint32_t(row)*line;
    auto index=lookup(r,r.u32(r.at(info,16)),code);
    const auto alternate=r.u16(r.at(info,2));
    if(index==0xffff)index=alternate;
    auto real=index;
    const auto adjust=[&](uint16_t value) {
        const auto offsets=r.u32(r.at(font,0x18));
        const auto offset=r.u16(r.at(offsets,(uint32_t(value)/per_sheet)*2));
        return offset==0xffff ? uint16_t(0xffff):uint16_t(value-offset);
    };
    if(archive) {
        real=adjust(index);
        if(real==0xffff){index=alternate;real=adjust(index);}
    }
    need(real!=0xffff,"Missing font fallback sheet");
    const auto sheet=uint32_t(real)/per_sheet,cell=uint32_t(real)%per_sheet;
    need(sheet<r.u16(r.at(texture,8)),"Font sheet index outside resource");
    WiiMenuFontGlyph out{};const auto width=widths(r,info,index);r.bytes(width,3);
    out.left=r.s8(width);out.glyph_width=r.u8(r.at(width,1));out.advance=r.s8(r.at(width,2));
    out.glyph_height=r.u8(r.at(texture,1));
    out.cell_x=(cell%row)*(uint32_t(r.u8(texture))+1)+1;
    out.cell_y=(cell/row)*(out.glyph_height+1)+1;
    auto& image=out.texture;image.format=r.u16(r.at(texture,10));
    image.width=r.u16(r.at(texture,16));image.height=r.u16(r.at(texture,18));
    need(image.width && image.width<=1024 && image.height && image.height<=1024,"Invalid font texture dimensions");
    image.size=image_size(image.format,image.width,image.height);
    const auto sheet_size=r.u32(r.at(texture,4));need(image.size<=sheet_size,"Short font sheet storage");
    const uint64_t offset=uint64_t(sheet)*sheet_size;
    need(offset<=UINT32_MAX,"Font sheet offset overflow");
    const auto address=r.at(r.u32(r.at(texture,20)),uint32_t(offset));r.bytes(address,image.size);
    image.address=address&0x1fffffffu;image.linear=1;
    need(out.cell_x+out.glyph_width<=image.width && out.cell_y+out.glyph_height<=image.height,"Font glyph outside sheet");
    out.font_width=r.u8(r.at(info,21));out.font_height=r.u8(r.at(info,20));out.font_ascent=r.u8(r.at(info,22));
    out.line_feed=r.s8(r.at(info,1));
    need(out.font_width && out.font_height,"Empty font metrics");
    return out;
}
}
extern "C" int wii_menu_font_glyph(WiiMenuMemory memory,uint32_t font,uint16_t code,
                                    WiiMenuFontGlyph* out,char* error,uint32_t size) {
    const auto message=[&](const char* text){if(error && size)std::snprintf(error,size,"%s",text);};
    if(!memory.read || !out){message("Missing font resource input");return 0;}
    try{const auto prepared=glyph(Reader{memory},font,code);*out=prepared;message("");return 1;}
    catch(const std::exception& e){message(e.what());return 0;}
}
