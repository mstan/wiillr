// SPDX-License-Identifier: GPL-3.0-or-later
// WR9E whole-overlay contract: 80083730, 80083E08, 80083BD8 and 80083CF8.
// Resource lookup is shared with the Menu, independent of either font class.
#include "wiillr/mm9_overlay.h"
#include "wiillr/menu_text.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Reader {
    WiiMm9Memory memory;
    const uint8_t* bytes(uint32_t a,uint32_t n)const {
        need(n<=UINT32_MAX-a,"Overlay address overflow");
        const auto* p=memory.read(memory.user,a,n);need(p,"Unreadable overlay input");return p;
    }
    uint8_t u8(uint32_t a)const{return *bytes(a,1);}
    uint16_t u16(uint32_t a)const{const auto* p=bytes(a,2);return uint16_t(p[0])<<8|p[1];}
    uint32_t u32(uint32_t a)const{const auto* p=bytes(a,4);return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
};
struct Character {uint16_t code;WiiMenuFontGlyph glyph{};};
std::vector<uint16_t> decode(const Reader& r,uint32_t text,uint32_t encoding) {
    need(encoding<=3,"Unsupported overlay encoding");
    // The original Shift-JIS path uses the separately initialized SDK Unicode
    // conversion tables. Do not mistake packed Shift-JIS bytes for Unicode.
    need(encoding!=2,"Overlay Shift-JIS conversion needs qualification");
    // Matches the WR9E SDK CP1252 table at 80258550, including its identity
    // mappings for the five undefined control codes.
    constexpr uint16_t cp1252[]{0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,
        0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,0x90,0x2018,0x2019,0x201c,
        0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
    std::vector<uint16_t> out;
    for(uint32_t at=0;at<1024;) {
        const auto first=r.u8(text+at++);uint32_t code=first;
        if(encoding==1) {
            need(at<1024,"Truncated overlay UTF-16");code=uint16_t(first)<<8|r.u8(text+at++);
        } else if(encoding==0 && first>=0x80) {
            const unsigned count=(first&0xe0)==0xc0?2:(first&0xf0)==0xe0?3:0;
            need(count && at+count-1<=1024,"Invalid overlay UTF-8");code=first&((1u<<(7-count))-1);
            for(unsigned i=1;i<count;i++){const auto tail=r.u8(text+at++);need((tail&0xc0)==0x80,"Invalid overlay UTF-8 tail");code=(code<<6)|(tail&63);}
            need(code>=(count==2?0x80u:0x800u) && !(code>=0xd800 && code<=0xdfff),"Invalid overlay UTF-8 scalar");
        } else if(encoding==3 && first>=0x80 && first<=0x9f) {
            code=cp1252[first-0x80];
        }
        if(!code)return out;out.push_back(uint16_t(code));
    }
    throw std::runtime_error("Unterminated overlay text");
}
struct Batch {uint64_t key;GcWiiNativeDraw draw;std::vector<GcWiiNativeVertex> vertices;std::vector<uint16_t> indices;};
struct Plan {std::vector<Batch> batches;std::vector<std::pair<uint32_t,uint8_t>> writes;uint64_t nodes=0,glyphs=0;};
int16_t narrow(int64_t value){return int16_t(uint16_t(value));}
float uv(uint32_t value,uint32_t dimension){need(dimension,"Empty overlay atlas");return float(narrow(int((value<<12)/dimension)))/4096.f;}
Plan prepare(const Reader& r,uint32_t head,uint32_t fonts,uint32_t encodings,uint32_t level,const GcWiiNativeDraw& view) {
    for(const auto f:view.model_view)need(std::isfinite(f),"Nonfinite overlay pose");
    for(const auto f:view.projection)need(std::isfinite(f),"Nonfinite overlay projection");
    for(const auto f:{view.x,view.y,view.width,view.height,view.near_depth,view.far_depth})need(std::isfinite(f),"Nonfinite overlay view");
    need(view.width>0 && view.height>0 && view.clip_width>0 && view.clip_height>0 && view.near_depth>=0 &&
         view.far_depth<=1 && view.near_depth<=view.far_depth && !(view.material.flags&~15u),"Invalid overlay view");
    Plan plan;std::set<uint32_t> seen;
    for(auto node=r.u32(head+4);node!=head;node=r.u32(node+4)) {
        need(node && seen.size()<64 && seen.insert(node).second,"Cyclic or excessive overlay nodes");r.bytes(node,0x41c);
        const auto kind=r.u32(node+8);need(kind>0 && kind<5,"Overlay OS font family needs qualification");
        const auto input_encoding=r.u32(node+0xc);need(input_encoding<5,"Invalid overlay text encoding selector");
        const auto font=r.u32(fonts+kind*4),info=r.u32(font+4),encoding=r.u32(encodings+input_encoding*4);
        need(font && info,"Missing overlay font");r.bytes(font,0x14);
        const auto codes=decode(r,node+0x10,encoding);std::vector<Character> text;
        const WiiMenuMemory memory{r.memory.user,r.memory.read};char error[192];
        for(const auto code:codes) {
            Character ch{code};if(code!=10)need(wii_resource_font_glyph(memory,info,code,0,&ch.glyph,error,sizeof error)!=0,error);
            text.push_back(ch);
        }
        const int width=r.u16(node+0x414),height=r.u16(node+0x416),align=r.u8(node+0x419);
        const int font_width=r.u8(info+21),font_height=r.u8(info+20),line_feed=int8_t(r.u8(info+1));
        need(width>0 && width<=32767 && height>0 && height<=32767 && font_width && font_height,"Invalid overlay font scale");
        auto line_width=[&](size_t begin) {int64_t value=0;for(size_t i=begin;i<text.size() && text[i].code!=10;i++)value+=text[i].glyph.advance*width/font_width;
            need(value>=INT32_MIN && value<=INT32_MAX,"Excessive overlay line width");return int32_t(value);};
        const int anchor=int16_t(r.u16(node+0x410));int x=narrow(anchor-int64_t(align)*(uint32_t(line_width(0))>>1)),y=int16_t(r.u16(node+0x412));
        const int z=narrow(r.u8(node+0x41a)*100/4);const float alpha=float(((r.u8(node+0x418)*level)>>5)&255)/255.f;
        size_t run=0;Batch* batch=nullptr;
        for(size_t i=0;i<text.size();i++) {
            const auto& ch=text[i];if(ch.code==10) {
                y=narrow(y+line_feed*height/font_height);x=narrow(anchor-int64_t(align)*(uint32_t(line_width(i+1))>>1));continue;
            }
            const auto& g=ch.glyph;
            if(!batch || std::memcmp(&batch->draw.material.texture,&g.texture,sizeof g.texture)) {
                Batch value{};value.key=(uint64_t(node)<<32)|run++;value.draw=view;value.draw.material.texture=g.texture;
                plan.batches.push_back(std::move(value));batch=&plan.batches.back();
            }
            const int x1=narrow(x+g.left*width/font_width),x2=narrow(x1+int(g.glyph_width)*width/font_width),y2=narrow(y+int(g.glyph_height)*height/font_height);
            const float xs[]{float(x1),float(x2),float(x2),float(x1)},ys[]{float(y),float(y),float(y2),float(y2)};
            const float us[]{uv(g.cell_x,g.texture.width),uv(g.cell_x+g.glyph_width,g.texture.width)},vs[]{uv(g.cell_y,g.texture.height),uv(g.cell_y+g.glyph_height,g.texture.height)};
            need(batch->vertices.size()<=65531,"Excessive overlay batch");const auto offset=uint16_t(batch->vertices.size());
            for(unsigned v=0;v<4;v++) {GcWiiNativeVertex vertex{};vertex.position[0]=xs[v];vertex.position[1]=ys[v];vertex.position[2]=float(z);
                vertex.color[0]=vertex.color[1]=vertex.color[2]=1;vertex.color[3]=alpha;vertex.uv[0]=us[v==1 || v==2];vertex.uv[1]=vs[v>=2];batch->vertices.push_back(vertex);}
            for(const unsigned v:{0u,1u,2u,0u,2u,3u})batch->indices.push_back(uint16_t(offset+v));
            x=narrow(x+g.advance*width/font_width);++plan.glyphs;
        }
        plan.writes.emplace_back(font+0x10,uint8_t(encoding));++plan.nodes;
    }
    need(plan.batches.size()<=4096,"Excessive overlay draw batches");return plan;
}
}
struct WiiMm9OverlayNative {
    WiiMm9Memory memory{};void (*write_byte)(void*,uint32_t,uint8_t){};const GcWiiNativeRenderApi* api{};
    struct Mesh {uint64_t handle{};std::vector<GcWiiNativeVertex> vertices;std::vector<uint16_t> indices;};
    std::map<uint64_t,Mesh> meshes;WiiMm9OverlayStatus status{};
};
extern "C" WiiMm9OverlayNative* wii_mm9_overlay_create(WiiMm9Memory memory,void (*write_byte)(void*,uint32_t,uint8_t),const GcWiiNativeRenderApi* api) {
    if(!memory.read || !write_byte || !api || api->abi!=GC_WII_NATIVE_RENDER_ABI || !api->create_mesh || !api->destroy_mesh || !api->draw)return nullptr;
    auto out=std::make_unique<WiiMm9OverlayNative>();out->memory=memory;out->write_byte=write_byte;out->api=api;return out.release();
}
extern "C" void wii_mm9_overlay_destroy(WiiMm9OverlayNative* r){if(r){for(const auto& [key,mesh]:r->meshes)r->api->destroy_mesh(mesh.handle);delete r;}}
extern "C" int wii_mm9_overlay_draw(WiiMm9OverlayNative* r,uint32_t head,uint32_t fonts,uint32_t encodings,uint32_t level,const GcWiiNativeDraw* view) {
    if(!r || !view)return 0;Plan plan;
    try{plan=prepare(Reader{r->memory},head,fonts,encodings,level,*view);}
    catch(const std::exception& e){++r->status.rejected;std::snprintf(r->status.error,sizeof r->status.error,"%s",e.what());return 0;}
    std::set<uint64_t> used;
    for(const auto& [address,value]:plan.writes)r->write_byte(r->memory.user,address,value);
    for(const auto& batch:plan.batches) {
        used.insert(batch.key);auto& mesh=r->meshes[batch.key];
        if(!mesh.handle || mesh.vertices.size()!=batch.vertices.size() || mesh.indices!=batch.indices ||
           std::memcmp(mesh.vertices.data(),batch.vertices.data(),batch.vertices.size()*sizeof(GcWiiNativeVertex))) {
            const GcWiiNativeMesh input{batch.vertices.data(),batch.indices.data(),uint32_t(batch.vertices.size()),uint32_t(batch.indices.size())};
            const auto handle=r->api->create_mesh(&input);if(!handle)return -1;
            if(mesh.handle)r->api->destroy_mesh(mesh.handle);mesh={handle,batch.vertices,batch.indices};++r->status.mesh_uploads;
        }
        auto job=batch.draw;job.mesh=mesh.handle;if(!r->api->draw(&job))return -1;++r->status.draws;
    }
    for(auto i=r->meshes.begin();i!=r->meshes.end();) {if(!used.count(i->first)){r->api->destroy_mesh(i->second.handle);i=r->meshes.erase(i);}else ++i;}
    ++r->status.entries;r->status.nodes+=plan.nodes;r->status.glyphs+=plan.glyphs;r->status.error[0]=0;return 1;
}
extern "C" void wii_mm9_overlay_status(const WiiMm9OverlayNative* r,WiiMm9OverlayStatus* out){if(r && out)*out=r->status;}
