// SPDX-License-Identifier: MIT
#include "wiillr/menu_text.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Reader {
    WiiMenuMemory memory;
    const uint8_t* bytes(uint32_t a,uint32_t n)const {
        need(n<=UINT32_MAX-a,"Text address overflow");
        const auto* p=memory.read(memory.user,a,n);need(p!=nullptr,"Unreadable Menu text object");return p;
    }
    uint8_t u8(uint32_t a)const{return *bytes(a,1);}
    uint16_t u16(uint32_t a)const{const auto* p=bytes(a,2);return uint16_t(uint16_t(p[0])<<8|p[1]);}
    uint32_t u32(uint32_t a)const{const auto* p=bytes(a,4);return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
    float f32(uint32_t a)const{const auto f=std::bit_cast<float>(u32(a));need(std::isfinite(f),"Nonfinite Menu text input");return f;}
};
struct Rect {
    float left=0,top=0,right=0,bottom=0;
    void include(const Rect& r){left=std::min(left,r.left);top=std::min(top,r.top);right=std::max(right,r.right);bottom=std::max(bottom,r.bottom);}
    float width()const{return right-left;}
    float height()const{return bottom-top;}
};
struct Character {uint16_t code;WiiMenuFontGlyph glyph{};};
struct Line {size_t count;Rect rect;bool over;};
struct Batch {
    std::vector<GcWiiNativeVertex> vertices;
    std::vector<uint16_t> indices;
    GcWiiNativeGuiDraw draw{};
};
struct Plan {std::vector<Batch> batches;uint64_t glyphs=0;};
struct Layout {
    std::vector<Character> text;
    float scale_x,scale_y,char_space,line_height,font_height,font_width,ascent;
    static float tab(float x,float width) {
        if(width<=0)return x;
        need(std::abs(x/width)<1000000,"Excessive Menu tab position");
        return (float(int(x/width))+1)*width;
    }
    Line line(size_t begin,size_t end,float max_width,float& y)const {
        Rect rect{0,std::min(0.f,line_height),0,std::max(0.f,line_height)};
        float x=0;bool space=false;
        for(size_t n=begin;n<end;++n) {
            const Rect previous=rect;
            const auto& c=text[n];bool newline=false;
            if(c.code=='\n') {
                const auto old=y;y+=line_height;
                const Rect control{std::min(0.f,x),std::min(old,y+font_height),std::max(0.f,x),std::max(old,y+font_height)};
                rect.include(control);x=0;newline=true;
            } else if(c.code=='\t') {
                const float next=tab(x,4*font_width);
                rect.include(Rect{std::min(x,next),y,std::max(x,next),y+font_height});x=next;space=false;
            } else if(c.code>=32) {
                if(space)x+=char_space;
                space=true;x+=c.glyph.advance*scale_x;
                rect.left=std::min(rect.left,x);rect.right=std::max(rect.right,x);
            }
            if(rect.width()>max_width) {
                if(n>begin)return Line{n-begin,previous,true};
                return Line{1,rect,true};
            }
            if(newline)return Line{n-begin+1,rect,false};
        }
        return Line{end-begin,rect,false};
    }
    Rect measure(float max_width)const {
        Rect total;float y=0;
        for(size_t at=0;at<text.size();) {
            auto current=line(at,text.size(),max_width,y);need(current.count,"Menu text wrap made no progress");
            if(current.over) {
                const auto old=y;y+=line_height;
                current.rect={0,std::min(0.f,line_height),0,std::max(0.f,line_height)};
                current.rect.include(Rect{0,std::min(old,y+font_height),0,std::max(old,y+font_height)});
            }
            total.include(current.rect);at+=current.count;
        }
        return total;
    }
};
void camera(const WiiMenuView& view,GcWiiNativeDraw& out) {
    for(float f:view.projection)need(std::isfinite(f),"Nonfinite text projection");
    const float fields[]{view.x,view.y,view.width,view.height,view.near_depth,view.far_depth};
    for(float f:fields)need(std::isfinite(f),"Nonfinite text viewport");
    need(view.width>0 && view.height>0 && view.clip_width>0 && view.clip_height>0 && view.near_depth>=0 && view.far_depth<=1 && view.near_depth<=view.far_depth,"Invalid text viewport");
    need((view.depth_flags&~6u)==0 && (!(view.depth_flags&GC_WII_NATIVE_DEPTH_TEST) || view.depth_func==3 || view.depth_func==7),"Unsupported text depth comparison");
    std::copy_n(view.projection,16,out.projection);out.x=view.x;out.y=view.y;out.width=view.width;out.height=view.height;
    out.near_depth=view.near_depth;out.far_depth=view.far_depth;out.clip_x=view.clip_x;out.clip_y=view.clip_y;
    out.clip_width=view.clip_width;out.clip_height=view.clip_height;
    out.material.flags=GC_WII_NATIVE_BLEND|view.depth_flags;
    if(view.depth_func==7)out.material.flags|=GC_WII_NATIVE_DEPTH_ALWAYS;
}
Plan prepare(const Reader& r,uint32_t pane,uint32_t info,const WiiMenuView& view) {
    need(r.u32(pane)==0x8167192c,"Unbound Menu TextBox class");r.bytes(pane,0x100);r.bytes(info,0x54);
    const auto pointer=r.u32(pane+0xd4),font=r.u32(pane+0xe0),material=r.u32(pane+0x28);
    if(!pointer || !font || !material)return {};
    need(r.u32(material)==0x81671b48,"Unbound Menu text material");
    r.bytes(material,0x34);
    need(r.u32(pane+0xf4)==0,"Custom Menu text tags require a native adapter");
    const auto length=r.u16(pane+0xfa);need(uint32_t(length)*2<=r.u16(pane+0xf8),"Text length exceeds buffer");
    r.bytes(pointer,uint32_t(length)*2);
    if(!length)return {};
    Layout layout{};char error[192];WiiMenuFontGlyph metrics{};
    need(wii_menu_font_glyph(r.memory,font,32,&metrics,error,sizeof error)!=0,error);
    layout.scale_x=r.f32(pane+0xe4)/metrics.font_width;layout.scale_y=r.f32(pane+0xe8)/metrics.font_height;
    layout.char_space=r.f32(pane+0xf0);layout.line_height=r.f32(pane+0xec)+metrics.line_feed*layout.scale_y;
    layout.font_width=metrics.font_width*layout.scale_x;layout.font_height=metrics.font_height*layout.scale_y;
    layout.ascent=metrics.font_ascent*layout.scale_y;
    need(layout.scale_x>=0 && layout.scale_y>=0 && std::isfinite(layout.line_height),"Invalid Menu font scale");
    for(uint32_t n=0;n<length;++n) {
        Character c{};c.code=r.u16(pointer+n*2);
        if(c.code>=32)need(wii_menu_font_glyph(r.memory,font,c.code,&c.glyph,error,sizeof error)!=0,error);
        layout.text.push_back(c);
    }
    const float width=r.f32(pane+0x4c),height=r.f32(pane+0x50);
    need(width>=0 && height>=0,"Invalid Menu text pane dimensions");
    const auto origin=r.u8(pane+0xcc),position=r.u8(pane+0xfc);need(origin<=8 && position<=8,"Invalid Menu text origin");
    const auto bounds=layout.measure(width);
    const float hm=float(position%3)*.5f,vm=float(position/3)*.5f;
    const float left=-width*float(origin%3)*.5f+(width-bounds.width())*hm;
    const float top=-height*float(origin/3)*.5f+(height-bounds.height())*vm;
    GcWiiNativeGuiDraw base{};camera(view,base.draw);
    std::array<float,12> pose{};for(unsigned i=0;i<12;++i)pose[i]=r.f32(pane+0x84+i*4);
    if(r.u8(info+0x50)&0x80) {
        auto previous=pose;
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col) {
            pose[row*4+col]=0;
            for(unsigned k=0;k<3;++k)pose[row*4+col]+=r.f32(info+4+(row*4+k)*4)*previous[k*4+col];
            if(col==3)pose[row*4+col]+=r.f32(info+4+(row*4+3)*4);
        }
    }
    if(r.f32(info+0x40)-r.f32(info+0x38)<0)for(unsigned row=0;row<3;++row)pose[row*4+1]=-pose[row*4+1];
    std::copy(pose.begin(),pose.end(),base.draw.model_view);
    bool default_color=true;
    for(unsigned ch=0;ch<4;++ch) {
        base.color.low[ch]=std::clamp(int(int16_t(r.u16(material+0x24+ch*2))),0,255);
        base.color.high[ch]=std::clamp(int(int16_t(r.u16(material+0x2c+ch*2))),0,255);
        default_color=default_color && base.color.low[ch]==0 && base.color.high[ch]==255;
    }
    float color[2][4];const auto alpha=r.u8(pane+0xce);
    for(unsigned side=0;side<2;++side)for(unsigned ch=0;ch<4;++ch) {
        unsigned value=r.u8(pane+0xd8+side*4+ch);
        if(ch==3)value=value*alpha/255;
        color[side][ch]=float(value)/255;
    }
    Plan result;float y=top;
    for(size_t start=0;start<layout.text.size();) {
        auto temporary_y=y;const auto line=layout.line(start,layout.text.size(),width,temporary_y);
        need(line.count,"Menu text draw made no progress");
        const float x_origin=left+hm*(bounds.width()-line.rect.width());float x=x_origin;
        float baseline_y=y+layout.ascent;bool space=false;
        for(size_t n=start;n<start+line.count;++n) {
            const auto& c=layout.text[n];
            if(c.code=='\n'){baseline_y+=layout.line_height;x=x_origin;space=false;continue;}
            if(c.code=='\t'){x=x_origin+Layout::tab(x-x_origin,4*layout.font_width);space=false;continue;}
            if(c.code<32)continue;
            if(space)x+=layout.char_space;
            space=true;
            const auto& g=c.glyph;
            if(result.batches.empty() || std::memcmp(&result.batches.back().draw.draw.material.texture,&g.texture,sizeof g.texture)!=0 || result.batches.back().vertices.size()>65528) {
                result.batches.emplace_back();auto& draw=result.batches.back().draw;draw=base;draw.draw.material.texture=g.texture;
                // Default intensity fonts retain raster RGB and use texture alpha.
                if(default_color && (g.texture.format==0 || g.texture.format==1))for(unsigned ch=0;ch<3;++ch)draw.color.low[ch]=draw.color.high[ch]=255;
            }
            auto& batch=result.batches.back();const auto index=uint16_t(batch.vertices.size());
            const float x0=x+g.left*layout.scale_x,y0=baseline_y-g.baseline*layout.scale_y;
            const float xs[]{x0,x0+g.glyph_width*layout.scale_x},ys[]{y0,y0+g.glyph_height*layout.scale_y};
            const float us[]{float((g.cell_x<<15)/g.texture.width)/32768,float(((g.cell_x+g.glyph_width)<<15)/g.texture.width)/32768};
            const float vs[]{float((g.cell_y<<15)/g.texture.height)/32768,float(((g.cell_y+g.glyph_height)<<15)/g.texture.height)/32768};
            constexpr unsigned vx[]{0,1,1,0},vy[]{0,0,1,1};
            for(unsigned v=0;v<4;++v) {
                GcWiiNativeVertex vertex{};vertex.position[0]=xs[vx[v]];vertex.position[1]=ys[vy[v]];
                std::copy_n(color[vy[v]],4,vertex.color);vertex.uv[0]=us[vx[v]];vertex.uv[1]=vs[vy[v]];batch.vertices.push_back(vertex);
            }
            for(unsigned i:{0u,1u,2u,0u,2u,3u})batch.indices.push_back(uint16_t(index+i));
            ++result.glyphs;x+=g.advance*layout.scale_x;
        }
        y=baseline_y-layout.ascent;if(line.over)y+=layout.line_height;start+=line.count;
    }
    return result;
}
struct Stored {
    uint64_t mesh=0;
    std::vector<GcWiiNativeVertex> vertices;
    std::vector<uint16_t> indices;
};
bool same(const Stored& old,const Batch& next) {
    return old.vertices.size()==next.vertices.size() && old.indices==next.indices &&
        std::memcmp(old.vertices.data(),next.vertices.data(),old.vertices.size()*sizeof(GcWiiNativeVertex))==0;
}
}
struct WiiMenuTextNative {
    WiiMenuMemory memory;const GcWiiNativeRenderApi* meshes;const GcWiiNativeGuiApi* gui;
    std::map<uint32_t,std::vector<Stored>> panes;WiiMenuTextStatus status{};
    ~WiiMenuTextNative(){for(auto& [pane,items]:panes)for(auto& item:items)meshes->destroy_mesh(item.mesh);}
};
extern "C" WiiMenuTextNative* wii_menu_text_create(WiiMenuMemory memory,const GcWiiNativeRenderApi* meshes,const GcWiiNativeGuiApi* gui) {
    if(!memory.read || !meshes || meshes->abi!=GC_WII_NATIVE_RENDER_ABI || !meshes->create_mesh || !meshes->destroy_mesh || !gui || gui->abi!=GC_WII_NATIVE_GUI_ABI || !gui->draw)return nullptr;
    try{return new WiiMenuTextNative{memory,meshes,gui,{},{}};}catch(...){return nullptr;}
}
extern "C" void wii_menu_text_destroy(WiiMenuTextNative* renderer){delete renderer;}
extern "C" int wii_menu_text_draw(WiiMenuTextNative* renderer,uint32_t pane,uint32_t info,const WiiMenuView* view) {
    if(!renderer || !view)return 0;
    ++renderer->status.entries;
    Plan plan;
    try{plan=prepare(Reader{renderer->memory},pane,info,*view);}
    catch(const std::exception& e){++renderer->status.rejected;std::snprintf(renderer->status.error,sizeof renderer->status.error,"%s",e.what());return 0;}
    try {
        auto& retained=renderer->panes[pane];
        for(size_t i=0;i<plan.batches.size();++i) {
            auto& batch=plan.batches[i];
            if(i>=retained.size())retained.emplace_back();
            auto& old=retained[i];
            if(!old.mesh || !same(old,batch)) {
                const GcWiiNativeMesh mesh{batch.vertices.data(),batch.indices.data(),uint32_t(batch.vertices.size()),uint32_t(batch.indices.size())};
                const auto handle=renderer->meshes->create_mesh(&mesh);need(handle!=0,"Native text mesh upload failed");
                if(old.mesh)renderer->meshes->destroy_mesh(old.mesh);
                old.mesh=handle;old.vertices=std::move(batch.vertices);old.indices=std::move(batch.indices);++renderer->status.mesh_uploads;
            }
        }
        while(retained.size()>plan.batches.size()){renderer->meshes->destroy_mesh(retained.back().mesh);retained.pop_back();}
        for(size_t i=0;i<plan.batches.size();++i) {
            plan.batches[i].draw.draw.mesh=retained[i].mesh;
            need(renderer->gui->draw(&plan.batches[i].draw)!=0,"Native text submission failed");++renderer->status.draws;
        }
        renderer->status.glyphs+=plan.glyphs;renderer->status.error[0]=0;return 1;
    }catch(const std::exception& e){++renderer->status.rejected;std::snprintf(renderer->status.error,sizeof renderer->status.error,"%s",e.what());return -1;}
}
extern "C" void wii_menu_text_forget(WiiMenuTextNative* renderer,uint32_t pane) {
    if(!renderer)return;
    const auto found=renderer->panes.find(pane);if(found==renderer->panes.end())return;
    for(const auto& item:found->second)renderer->meshes->destroy_mesh(item.mesh);
    renderer->panes.erase(found);
}
extern "C" void wii_menu_text_status(const WiiMenuTextNative* renderer,WiiMenuTextStatus* out){if(renderer && out)*out=renderer->status;}
