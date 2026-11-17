// SPDX-License-Identifier: GPL-3.0-or-later
#include <wiillr/mm9_hbm.h>
#include <wiillr/mm9_screen.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>
namespace {
constexpr WiiMenuClasses classes{0x802a9aa0,0x802a9c44,0x802a99f0,0x802a9b5c,0x802aae38,0};
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Reader {
    WiiMenuMemory memory;
    const uint8_t* bytes(uint32_t a,uint32_t n)const {need(n<=UINT32_MAX-a,"HBM address overflow");const auto* p=memory.read(memory.user,a,n);need(p,"Unreadable HBM object");return p;}
    uint8_t u8(uint32_t a)const{return *bytes(a,1);}
    uint32_t u32(uint32_t a)const{auto* p=bytes(a,4);return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
    float f32(uint32_t a)const{const auto v=std::bit_cast<float>(u32(a));need(std::isfinite(v),"Nonfinite HBM value");return v;}
};
struct Job {uint64_t key{};uint32_t text{};WiiMenuPicture quad{};};
}
struct WiiMm9Hbm {
    WiiMenuMemory memory{};WiiMenuNative* pictures{};WiiMenuTextNative* text{};WiiMm9ScreenNative* fade{};
    std::vector<Job> jobs;std::set<uint32_t> seen;WiiMm9HbmStatus status{};
};
extern "C" WiiMm9Hbm* wii_mm9_hbm_create(WiiMenuMemory memory,const GcWiiNativeRenderApi* render,
    const GcWiiNativeGuiApi* gui,const GcWiiNativeGuiLayersApi* layers,
    const GcWiiNativeGuiMaskApi* mask,const GcWiiNativeGuiEffectsApi* effects) {
    auto out=std::make_unique<WiiMm9Hbm>();memory.classes=&classes;out->memory=memory;
    out->pictures=wii_menu_native_create(memory,render);
    out->text=wii_menu_text_create(memory,render,gui);
    out->fade=wii_mm9_screen_create({memory.user,memory.read},render);
    if(!out->pictures || !out->text || !out->fade || !wii_menu_native_gui(out->pictures,gui) ||
       !wii_menu_native_gui_layers(out->pictures,layers) || !wii_menu_native_gui_mask(out->pictures,mask) ||
       !wii_menu_native_gui_effects(out->pictures,effects)) {
        wii_mm9_hbm_destroy(out.release());return nullptr;
    }
    return out.release();
}
extern "C" void wii_mm9_hbm_destroy(WiiMm9Hbm* out) {
    if(!out)return;wii_menu_native_destroy(out->pictures);wii_menu_text_destroy(out->text);wii_mm9_screen_destroy(out->fade);delete out;
}
namespace {
void collect(WiiMm9Hbm& out,const Reader& r,uint32_t pane,uint32_t info,const WiiMenuView& view) {
    need(out.seen.size()<4096 && out.seen.insert(pane).second,"Cyclic or excessive HBM pane hierarchy");
    if(!(r.u8(pane+0xcf)&1))return;
    const auto type=r.u32(pane);char error[192]{};
    if(type==classes.picture) {
        Job job{};job.key=uint64_t(pane)<<4;
        need(wii_menu_picture_prepare(out.memory,pane,info,&view,&job.quad,error,sizeof error),error);out.jobs.push_back(job);
    } else if(type==classes.window) {
        WiiMenuWindow window{};need(wii_menu_window_prepare(out.memory,pane,info,&view,&window,error,sizeof error),error);
        for(unsigned slot=0;slot<window.count;++slot)out.jobs.push_back({(uint64_t(pane)<<4)|slot,0,window.quads[slot]});
    } else if(type==classes.textbox) {
        need(wii_menu_text_validate(out.memory,pane,info,&view,error,sizeof error),error);Job job{};job.text=pane;out.jobs.push_back(job);
    } else need(type==0x802a9a38 || type==0x802a9820,"Unbound HBM pane class");
    auto node=r.u32(pane+0x14);uint32_t previous=pane+0x14,count=0;
    while(node!=pane+0x14) {
        need(count++<1024 && node>=4 && r.u32(node+4)==previous,"Invalid HBM child links");
        need(r.u32(node-4+0xc)==pane,"Unbound HBM child parent");
        collect(out,r,node-4,info,view);previous=node;node=r.u32(node);
    }
    need(count==r.u32(pane+0x10) && previous==r.u32(pane+0x18),"Invalid HBM child count/tail");
}
}
extern "C" int wii_mm9_hbm_draw(WiiMm9Hbm* out,uint32_t object,const WiiMenuView* view) {
    if(!out || !view)return 0;
    try {
        const Reader r{out->memory};r.bytes(object,0x4f0);const auto info=object+0x274;
        need(r.u32(info)==0x802a9908 && !(r.u8(info+0x50)&8),"Unbound HBM DrawInfo/debug outlines");
        out->jobs.clear();out->seen.clear();
        for(const auto offset:{0x244u,0x240u,0x254u,0x250u,0x24cu,0x248u}) {
            if(offset!=0x244 && offset!=0x240 && r.u32(r.u32(object+4)+0x20))continue;
            const auto layout=r.u32(object+offset);need(r.u32(layout)==0x802a9960,"Unbound HBM Layout class");
            if(const auto pane=r.u32(layout+0x10))collect(*out,r,pane,info,*view);
        }
        const auto tick=r.u32(object+0x4e0),total=r.u32(object+0x4e4);
        need(total && total<=INT32_MAX/255 && tick<=total,"Invalid HBM fade interval");
        const auto alpha=uint8_t(tick*255/total);WiiMm9Screen fade{};
        if(alpha) {
            std::copy_n(view->projection,16,fade.draw.projection);fade.draw.model_view[0]=fade.draw.model_view[5]=fade.draw.model_view[10]=1;
            fade.draw.x=view->x;fade.draw.y=view->y;fade.draw.width=view->width;fade.draw.height=view->height;
            fade.draw.clip_x=view->clip_x;fade.draw.clip_y=view->clip_y;fade.draw.clip_width=view->clip_width;fade.draw.clip_height=view->clip_height;
            fade.draw.far_depth=1;fade.draw.material.flags=GC_WII_NATIVE_BLEND;
            const auto a=r.f32(0x8025cd88),b=r.f32(0x8025cd8c);const float xs[]{a,a,b,b},ys[]{a,b,b,a};
            for(unsigned i=0;i<4;++i){auto& v=fade.vertices[i];v.position[0]=xs[i];v.position[1]=ys[i];for(unsigned c=0;c<3;++c)v.color[c]=r.u8(object+0x4ec)?r.u8(object+0x4ed+c)/255.f:0;v.color[3]=alpha/255.f;}
        }
        // All scene resources and geometry are checked before the first draw.
        for(const auto& job:out->jobs) {
            const auto result=job.text?wii_menu_text_draw(out->text,job.text,info,view):wii_menu_native_submit_prepared(out->pictures,job.key,&job.quad);
            if(result!=1){std::snprintf(out->status.error,sizeof out->status.error,"HBM native scene submission failed");++out->status.rejected;return -1;}
        }
        if(alpha && wii_mm9_screen_draw_prepared(out->fade,0xffffffff,&fade)!=1){++out->status.rejected;return -1;}
        ++out->status.scenes;out->status.error[0]=0;return 1;
    } catch(const std::exception& e){++out->status.rejected;std::snprintf(out->status.error,sizeof out->status.error,"%s",e.what());return 0;}
}
extern "C" void wii_mm9_hbm_status(const WiiMm9Hbm* out,WiiMm9HbmStatus* status){if(out && status)*status=out->status;}
