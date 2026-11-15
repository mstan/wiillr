// SPDX-License-Identifier: MIT
#include <wiillr/mm9_screen.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <unordered_map>
namespace {
void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void message(char* p,uint32_t n,const char* why){if(p && n)std::snprintf(p,n,"%s",why);}
struct Reader {
    WiiMm9Memory memory;
    const uint8_t* bytes(uint32_t a,uint32_t n)const{auto* p=memory.read(memory.user,a,n);need(p,"Unreadable MM9 screen object");return p;}
    uint8_t u8(uint32_t a)const{return *bytes(a,1);}
    uint16_t u16(uint32_t a)const{auto* p=bytes(a,2);return uint16_t(p[0])<<8|p[1];}
    uint32_t u32(uint32_t a)const{auto* p=bytes(a,4);return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
    float f32(uint32_t a)const{const auto f=std::bit_cast<float>(u32(a));need(std::isfinite(f),"Nonfinite MM9 screen coordinates");return f;}
};
WiiMm9Screen prepare(const Reader& r,uint32_t object,uint32_t level,const GcWiiNativeDraw& camera) {
    WiiMm9Screen out{};out.draw=camera;out.draw.mesh=0;
    for(float f:camera.model_view)need(std::isfinite(f),"Nonfinite MM9 screen pose");
    for(float f:camera.projection)need(std::isfinite(f),"Nonfinite MM9 screen projection");
    const float values[]{camera.x,camera.y,camera.width,camera.height,camera.near_depth,camera.far_depth};for(float f:values)need(std::isfinite(f),"Nonfinite MM9 screen view");
    need(camera.width>0 && camera.height>0 && camera.clip_width>0 && camera.clip_height>0 && camera.near_depth>=0 && camera.far_depth<=1 && camera.near_depth<=camera.far_depth && !(camera.material.flags&~15u),"Invalid MM9 screen view");
    auto& texture=out.draw.material.texture;texture={};const auto image=r.u32(object+8),sampler=r.u32(object);
    texture.width=(image&1023)+1;texture.height=((image>>10)&1023)+1;texture.format=(image>>20)&15;
    texture.address=(r.u32(object+12)&0xffffffu)<<5;
    unsigned block_width=4,block_height=4,block_bytes=32;
    switch(texture.format) {
    case 0:case 14:block_width=block_height=8;break;
    case 1:case 2:block_width=8;break;
    case 3:case 4:case 5:break;
    case 6:block_bytes=64;break;
    default:need(false,"MM9 queued palette/texture family needs qualification");
    }
    texture.size=((texture.width+block_width-1)/block_width)*((texture.height+block_height-1)/block_height)*block_bytes;
    texture.wrap_s=sampler&3;texture.wrap_t=(sampler>>2)&3;texture.linear=(sampler>>4)&1;
    need(texture.wrap_s<=2 && texture.wrap_t<=2 && (((sampler>>5)&7)==0 || ((sampler>>5)&7)==4) && (((sampler>>7)&1)==texture.linear) && !(sampler&0x19fe00),"MM9 screen sampler needs qualification");
    r.bytes(texture.address,texture.size);
    const int x=int16_t(r.u16(object+0x20)),y=int16_t(r.u16(object+0x22)),w=int16_t(r.u16(object+0x24)),h=int16_t(r.u16(object+0x26));
    need(w>0 && h>0,"Invalid MM9 screen rectangle");const float z=float(r.u8(object+0x38)*100/4);
    const float xs[]{float(x-w/2),float(x-w/2),float(x+(w+1)/2),float(x+(w+1)/2)};
    const float ys[]{float(y+(h+1)/2),float(y-h/2),float(y-h/2),float(y+(h+1)/2)};
    const auto u0=r.f32(object+0x28),u1=r.f32(object+0x2c),v0=r.f32(object+0x30),v1=r.f32(object+0x34);
    const float us[]{u0,u0,u1,u1},vs[]{v1,v0,v0,v1};
    const auto value=level>=32?65535u:((level&31)<<11)|((level&31)<<6)|(level&31);
    const auto red=(value>>11)&31,green=(value>>5)&63,blue=value&31;
    const float color[]{float((red<<3)|(red>>2))/255.f,float((green<<2)|(green>>4))/255.f,float((blue<<3)|(blue>>2))/255.f,1.f};
    for(unsigned n=0;n<4;++n){auto& vertex=out.vertices[n];vertex.position[0]=xs[n];vertex.position[1]=ys[n];vertex.position[2]=z;vertex.uv[0]=us[n];vertex.uv[1]=vs[n];std::copy_n(color,4,vertex.color);}
    return out;
}
}
extern "C" int wii_mm9_screen_view_projection(GcWiiNativeDraw* camera,
    float original_half_width,float guest_left,float guest_right,int enhanced_view) {
    if(!camera || !std::isfinite(original_half_width) || !(original_half_width>0) ||
       !std::isfinite(guest_left) || !std::isfinite(guest_right) || !(guest_right>guest_left))return 0;
    const float left=enhanced_view?-original_half_width:guest_left;
    const float right=enhanced_view?original_half_width:guest_right;
    const float scale=2/(right-left),offset=-(right+left)/(right-left);
    if(!std::isfinite(scale) || !std::isfinite(offset) || !(scale>0))return 0;
    camera->projection[0]=scale;camera->projection[3]=offset;return 1;
}
extern "C" int wii_mm9_screen_prepare(WiiMm9Memory memory,uint32_t object,uint32_t level,const GcWiiNativeDraw* camera,WiiMm9Screen* output,char* error,uint32_t size) {
    if(!memory.read || !camera || !output){message(error,size,"Missing MM9 screen input");return 0;}
    try{const auto screen=prepare({memory},object,level,*camera);*output=screen;message(error,size,"");return 1;}catch(const std::exception& e){message(error,size,e.what());return 0;}
}
struct WiiMm9ScreenNative {
    WiiMm9Memory memory{};const GcWiiNativeRenderApi* backend{};
    struct Mesh {uint64_t handle{};std::array<GcWiiNativeVertex,4> vertices{};};
    std::unordered_map<uint32_t,Mesh> meshes;WiiMm9ScreenStatus status{};
};
extern "C" WiiMm9ScreenNative* wii_mm9_screen_create(WiiMm9Memory memory,const GcWiiNativeRenderApi* api) {
    if(!memory.read || !api || api->abi!=GC_WII_NATIVE_RENDER_ABI || !api->create_mesh || !api->destroy_mesh || !api->draw)return nullptr;
    auto out=std::make_unique<WiiMm9ScreenNative>();out->memory=memory;out->backend=api;return out.release();
}
extern "C" void wii_mm9_screen_destroy(WiiMm9ScreenNative* renderer){if(!renderer)return;for(const auto& [object,mesh]:renderer->meshes)if(mesh.handle)renderer->backend->destroy_mesh(mesh.handle);delete renderer;}
extern "C" int wii_mm9_screen_draw(WiiMm9ScreenNative* renderer,uint32_t object,uint32_t level,const GcWiiNativeDraw* camera) {
    if(!renderer)return 0;WiiMm9Screen screen{};
    if(!wii_mm9_screen_prepare(renderer->memory,object,level,camera,&screen,renderer->status.error,sizeof(renderer->status.error))){++renderer->status.rejected;return 0;}
    return wii_mm9_screen_draw_prepared(renderer,object,&screen);
}
extern "C" int wii_mm9_screen_draw_prepared(WiiMm9ScreenNative* renderer,uint32_t key,const WiiMm9Screen* input) {
    if(!renderer || !input)return -1;const auto& screen=*input;
    auto& retained=renderer->meshes[key];
    if(!retained.handle || std::memcmp(retained.vertices.data(),screen.vertices,sizeof(screen.vertices))) {
        constexpr uint16_t indices[]{0,1,2,0,2,3};const GcWiiNativeMesh input{screen.vertices,indices,4,6};const auto mesh=renderer->backend->create_mesh(&input);
        if(!mesh){message(renderer->status.error,sizeof(renderer->status.error),"Native MM9 screen mesh creation failed");++renderer->status.rejected;return -1;}
        if(retained.handle)renderer->backend->destroy_mesh(retained.handle);retained.handle=mesh;std::copy_n(screen.vertices,4,retained.vertices.begin());++renderer->status.mesh_uploads;
    }
    auto draw=screen.draw;draw.mesh=retained.handle;if(!renderer->backend->draw(&draw)){message(renderer->status.error,sizeof(renderer->status.error),"Native MM9 screen draw failed");++renderer->status.rejected;return -1;}
    ++renderer->status.draws;return 1;
}
extern "C" void wii_mm9_screen_status(const WiiMm9ScreenNative* renderer,WiiMm9ScreenStatus* output){if(renderer && output)*output=renderer->status;}
