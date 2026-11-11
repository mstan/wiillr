// SPDX-License-Identifier: MIT
#include <wiillr/menu_picture.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>

namespace {
constexpr uint32_t picture_vtable=0x816717f4,material_vtable=0x81671b48;
constexpr uint32_t sdk_pointer=0x81695618;
void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void message(char* out,uint32_t size,const char* why){if(out && size)std::snprintf(out,size,"%s",why);}
struct Reader {
    WiiMenuMemory memory;
    const uint8_t* bytes(uint32_t a,uint32_t n)const{auto* p=memory.read(memory.user,a,n);need(p!=nullptr,"Unreadable Wii Menu object range");return p;}
    uint8_t u8(uint32_t a)const{return *bytes(a,1);}
    uint16_t u16(uint32_t a)const{auto* p=bytes(a,2);return uint16_t(uint16_t(p[0])<<8|p[1]);}
    uint32_t u32(uint32_t a)const{auto* p=bytes(a,4);return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
    float f32(uint32_t a)const{const auto f=std::bit_cast<float>(u32(a));need(std::isfinite(f),"Nonfinite Wii Menu geometry");return f;}
};
struct Counts {
    unsigned textures,srts,generators,ind_srts,ind_stages,swaps,stages,channels,color,alpha,blend;
    explicit Counts(uint32_t v):textures(v>>28),srts((v>>24)&15),generators((v>>20)&15),ind_srts((v>>18)&3),ind_stages((v>>15)&7),swaps((v>>14)&1),stages((v>>9)&31),channels((v>>8)&1),color((v>>7)&1),alpha((v>>6)&1),blend((v>>5)&1){}
};
std::array<float,12> matrix(const Reader& r,uint32_t address){std::array<float,12> m;for(unsigned n=0;n<12;++n)m[n]=r.f32(address+n*4);return m;}
std::array<float,12> multiply(const std::array<float,12>& a,const std::array<float,12>& b) {
    std::array<float,12> out{};
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col){out[row*4+col]=a[row*4]*b[col]+a[row*4+1]*b[4+col]+a[row*4+2]*b[8+col];if(col==3)out[row*4+col]+=a[row*4+3];}
    return out;
}
uint32_t texture_bytes(uint32_t format,uint32_t width,uint32_t height) {
    uint32_t x=4,y=4,bytes=32;
    switch(format){case 0:case 8:case 14:x=y=8;break;case 1:case 2:case 9:x=8;break;case 3:case 4:case 5:case 10:break;case 6:bytes=64;break;default:throw std::runtime_error("Unsupported Wii Menu texture format");}
    return ((width+x-1)/x)*((height+y-1)/y)*bytes;
}
void view(const WiiMenuView& source,GcWiiNativeDraw& out) {
    for(float f:source.projection)need(std::isfinite(f),"Nonfinite Wii Menu projection");
    const float fields[]{source.x,source.y,source.width,source.height,source.near_depth,source.far_depth};for(float f:fields)need(std::isfinite(f),"Nonfinite Wii Menu view");
    need(source.width>0 && source.height>0 && source.near_depth>=0 && source.far_depth<=1 && source.near_depth<=source.far_depth && source.clip_width>0 && source.clip_height>0,"Invalid Wii Menu view");
    need((source.depth_flags&~6u)==0 && (!(source.depth_flags&GC_WII_NATIVE_DEPTH_TEST) || source.depth_func==3 || source.depth_func==7),"Wii Menu depth comparison needs a native shader family");
    std::copy_n(source.projection,16,out.projection);out.x=source.x;out.y=source.y;out.width=source.width;out.height=source.height;out.near_depth=source.near_depth;out.far_depth=source.far_depth;
    out.clip_x=source.clip_x;out.clip_y=source.clip_y;out.clip_width=source.clip_width;out.clip_height=source.clip_height;
    out.material.flags|=source.depth_flags;
    if(source.depth_func==7)out.material.flags|=GC_WII_NATIVE_DEPTH_ALWAYS;
}
WiiMenuPicture prepare(const Reader& r,uint32_t pane,uint32_t info,const WiiMenuView& camera) {
    need(r.u32(pane)==picture_vtable,"Unbound Wii Menu pane class");
    WiiMenuPicture out{};out.pane=pane;out.material=r.u32(pane+0x28);
    if(!out.material){out.skipped=1;return out;}
    const auto material=out.material;need(r.u32(material)==material_vtable,"Unbound Wii Menu material class");
    const Counts cap(r.u32(material+0x4c)),num(r.u32(material+0x50));
    need(num.textures<=1 && num.generators==num.textures && !num.stages && !num.ind_stages,"Wii Menu material requires a broader native shader family");
    need(num.textures<=cap.textures && num.srts<=cap.srts && num.generators<=cap.generators,"Invalid Wii Menu material allocation");
    need(cap.textures<=8 && cap.srts<=10 && cap.generators<=8 && cap.ind_srts<=3 && cap.ind_stages<=4 && cap.stages<=16,"Unbounded Wii Menu material allocation");
    need(!cap.swaps,"Wii Menu custom component swaps need a native shader family");
    const auto mem=r.u32(material+0x58);if(cap.textures || cap.srts || cap.generators || cap.channels || cap.color || cap.alpha || cap.blend)need(mem!=0,"Missing Wii Menu material storage");
    const auto srt=mem+cap.textures*32,gen=srt+cap.srts*20,channel=gen+cap.generators*4;
    const auto mat_color=channel+cap.channels*4,alpha=mat_color+cap.color*4+cap.swaps*4,blend=alpha+cap.alpha*4;
    if(cap.alpha)need(r.u8(alpha)==0x77 && r.u8(alpha+1)==0,"Wii Menu alpha test needs a native shader family");
    out.draw.material.flags=GC_WII_NATIVE_BLEND;
    if(cap.blend){const auto mode=r.u8(blend);need(mode==0 || (mode==1 && r.u8(blend+1)==4 && r.u8(blend+2)==5),"Wii Menu blend operation needs a native shader family");out.draw.material.flags=mode==1?GC_WII_NATIVE_BLEND:0;}
    std::array<uint16_t,4> tint{};
    for(unsigned ch=0;ch<4;++ch){const auto value=static_cast<int16_t>(r.u16(material+0x2c+ch*2));need(value==255,"Wii Menu material color remapping needs a native shader family");tint[ch]=uint16_t(value);}
    if(num.textures)for(unsigned ch=0;ch<4;++ch)need(r.u16(material+0x24+ch*2)==0,"Wii Menu texture color offset needs a native shader family");
    const auto global_alpha=r.u8(pane+0xce);unsigned color_source=1,alpha_source=1;
    if(cap.channels){color_source=r.u8(channel);alpha_source=r.u8(channel+1);need(color_source<=1 && alpha_source<=1,"Invalid Wii Menu color source");}
    std::array<uint8_t,4> material_color{255,255,255,255};if(cap.color)for(unsigned ch=0;ch<4;++ch)material_color[ch]=r.u8(mat_color+ch);
    material_color[3]=uint8_t(unsigned(material_color[3])*global_alpha/255);
    const auto width=r.f32(pane+0x4c),height=r.f32(pane+0x50);need(width>=0 && height>=0,"Negative Wii Menu pane size");
    const auto base=r.u8(pane+0xcc);need(base<=8,"Invalid Wii Menu pane origin");
    const float x=-width*float(base%3)*.5f,y=-height*float(base/3)*.5f;
    constexpr unsigned color_index[]{0,1,3,2};
    const float xs[]{x,x+width,x+width,x},ys[]{y,y,y+height,y+height};
    for(unsigned v=0;v<4;++v) {
        auto& vertex=out.vertices[v];vertex.position[0]=xs[v];vertex.position[1]=ys[v];
        for(unsigned ch=0;ch<4;++ch) {
            unsigned value=(ch==3?alpha_source:color_source)?r.u8(pane+0xd4+color_index[v]*4+ch):material_color[ch];
            if(ch==3 && alpha_source)value=value*global_alpha/255;
            vertex.color[ch]=float(value)*float(tint[ch])/(255.f*255.f);
        }
    }
    auto pose=matrix(r,pane+0x84);if(r.u8(info+0x50)&0x80)pose=multiply(matrix(r,info+4),pose);
    if(r.f32(info+0x40)-r.f32(info+0x38)<0)for(unsigned row=0;row<3;++row)pose[row*4+1]=-pose[row*4+1];
    std::copy(pose.begin(),pose.end(),out.draw.model_view);
    view(camera,out.draw);
    if(num.textures) {
        need(r.u8(pane+0xe5)>=1 && r.u8(pane+0xe5)<=8,"Missing Wii Menu texture coordinates");
        need(r.u8(gen)==1 && r.u8(gen+1)==4,"Wii Menu generated texture coordinates need a native shader family");
        const auto tex_matrix=r.u8(gen+2);float sx=1,sy=1,tx=0,ty=0;
        if(tex_matrix!=60) {
            need(tex_matrix>=30 && tex_matrix<=57 && (tex_matrix-30)%3==0,"Invalid Wii Menu texture matrix");const auto slot=(tex_matrix-30)/3;
            need(slot<num.srts,"Missing Wii Menu texture transform");const auto transform=srt+slot*20;
            need(r.f32(transform+8)==0,"Rotated Wii Menu texture transform needs qualification");
            sx=r.f32(transform+12);sy=r.f32(transform+16);tx=r.f32(transform)+.5f-sx*.5f;ty=r.f32(transform+4)+.5f-sy*.5f;
        }
        const auto coords=r.u32(pane+0xe8);for(unsigned v=0;v<4;++v){out.vertices[v].uv[0]=r.f32(coords+color_index[v]*8)*sx+tx;out.vertices[v].uv[1]=r.f32(coords+color_index[v]*8+4)*sy+ty;}
        auto& texture=out.draw.material.texture;const auto image=r.u32(mem+8),sampler=r.u32(mem);
        texture.width=(image&1023)+1;texture.height=((image>>10)&1023)+1;texture.format=(image>>20)&15;
        texture.address=(r.u32(mem+12)&0xffffffu)<<5;texture.size=texture_bytes(texture.format,texture.width,texture.height);
        texture.wrap_s=sampler&3;texture.wrap_t=(sampler>>2)&3;texture.linear=(sampler>>4)&1;
        need(texture.wrap_s<=2 && texture.wrap_t<=2,"Invalid Wii Menu sampler wrapping");
        const auto min_filter=(sampler>>5)&7;
        need((min_filter==0 || min_filter==4) && ((min_filter>>2)&1)==texture.linear && !(sampler&0x19fe00),"Wii Menu sampler filtering or LOD bias needs a native shader family");
        if(texture.format>=8 && texture.format<=10) {
            const auto palette=r.u32(mem+16);need(palette!=0,"Missing Wii Menu indexed texture palette");
            texture.palette_entries=r.u16(palette);texture.palette_format=r.u32(palette+4);texture.palette_address=r.u32(palette+8)&0x1fffffffu;
            const auto minimum=texture.format==8?16u:(texture.format==9?256u:16384u);
            need(texture.palette_entries>=minimum && texture.palette_entries<=16384 && texture.palette_format<=2,"Wii Menu palette layout needs a native shader family");
        }
    }
    return out;
}
}
extern "C" int wii_menu_picture_prepare(WiiMenuMemory memory,uint32_t pane,uint32_t info,const WiiMenuView* camera,WiiMenuPicture* output,char* error,uint32_t error_size) {
    if(!memory.read || !camera || !output){message(error,error_size,"Missing native Wii Menu input");return 0;}
    try{const auto result=prepare({memory},pane,info,*camera);*output=result;message(error,error_size,"");return 1;}catch(const std::exception& e){message(error,error_size,e.what());return 0;}
}
extern "C" int wii_menu_picture_sdk_view(WiiMenuMemory memory,WiiMenuView* output,char* error,uint32_t error_size) {
    if(!memory.read || !output){message(error,error_size,"Missing native Wii Menu view input");return 0;}
    try {
        const Reader r{memory};const auto data=r.u32(sdk_pointer);WiiMenuView camera{};
        const auto type=r.u32(data+0x528);need(type<=1,"Unsupported Wii Menu projection");
        camera.projection[0]=r.f32(data+0x52c);camera.projection[5]=r.f32(data+0x534);camera.projection[10]=r.f32(data+0x53c);camera.projection[11]=r.f32(data+0x540);
        camera.projection[type?3:2]=r.f32(data+0x530);camera.projection[type?7:6]=r.f32(data+0x538);if(type)camera.projection[15]=1;else camera.projection[14]=-1;
        camera.x=r.f32(data+0x544);camera.y=r.f32(data+0x548);camera.width=r.f32(data+0x54c);camera.height=r.f32(data+0x550);camera.near_depth=r.f32(data+0x554);camera.far_depth=r.f32(data+0x558);
        const auto top=r.u32(data+0x148),bottom=r.u32(data+0x14c);camera.clip_x=int32_t((top>>12)&2047)-342;camera.clip_y=int32_t(top&2047)-342;
        camera.clip_width=int32_t((bottom>>12)&2047)-342-camera.clip_x+1;camera.clip_height=int32_t(bottom&2047)-342-camera.clip_y+1;
        const auto depth=r.u32(data+0x228);camera.depth_func=(depth>>1)&7;camera.depth_flags=((depth&1)?GC_WII_NATIVE_DEPTH_TEST:0)|((depth&16)?GC_WII_NATIVE_DEPTH_WRITE:0);
        need((r.u32(data+0x220)&24u)==24u,"Wii Menu inherited write masks need qualification");
        need(!(r.u32(data+0x224)&0x100u),"Wii Menu constant destination alpha needs qualification");
        auto values=camera;values.depth_flags=0;GcWiiNativeDraw check{};view(values,check);*output=camera;message(error,error_size,"");return 1;
    }catch(const std::exception& e){message(error,error_size,e.what());return 0;}
}
struct WiiMenuNative {
    struct Mesh{uint64_t handle{};std::array<GcWiiNativeVertex,4> vertices{};};
    WiiMenuMemory memory{};const GcWiiNativeRenderApi* backend{};std::map<uint32_t,Mesh> meshes;WiiMenuNativeStatus status{};
};
extern "C" WiiMenuNative* wii_menu_native_create(WiiMenuMemory memory,const GcWiiNativeRenderApi* api) {
    if(!memory.read || !api || api->abi!=GC_WII_NATIVE_RENDER_ABI || !api->create_mesh || !api->destroy_mesh || !api->draw)return nullptr;
    auto out=std::make_unique<WiiMenuNative>();out->memory=memory;out->backend=api;return out.release();
}
extern "C" void wii_menu_native_forget_picture(WiiMenuNative* renderer,uint32_t pane) {
    if(!renderer)return;auto found=renderer->meshes.find(pane);if(found==renderer->meshes.end())return;
    renderer->backend->destroy_mesh(found->second.handle);renderer->meshes.erase(found);renderer->status.live_meshes=renderer->meshes.size();
}
extern "C" void wii_menu_native_destroy(WiiMenuNative* renderer) {
    if(!renderer)return;for(const auto& [pane,mesh]:renderer->meshes)renderer->backend->destroy_mesh(mesh.handle);delete renderer;
}
extern "C" int wii_menu_native_draw_picture(WiiMenuNative* renderer,uint32_t pane,uint32_t info,const WiiMenuView* camera) {
    if(!renderer || !camera)return 0;
    WiiMenuPicture picture{};
    if(!wii_menu_picture_prepare(renderer->memory,pane,info,camera,&picture,renderer->status.error,sizeof(renderer->status.error))){++renderer->status.rejected;return 0;}
    if(picture.skipped){++renderer->status.skipped;return 1;}
    auto found=renderer->meshes.find(pane);
    if(found==renderer->meshes.end() || std::memcmp(found->second.vertices.data(),picture.vertices,sizeof(picture.vertices))) {
        constexpr uint16_t indices[]{0,1,2,0,2,3};const GcWiiNativeMesh input{picture.vertices,indices,4,6};
        const auto handle=renderer->backend->create_mesh(&input);
        if(!handle){message(renderer->status.error,sizeof(renderer->status.error),"Native Wii Menu mesh creation failed");++renderer->status.rejected;return -1;}
        if(found!=renderer->meshes.end())renderer->backend->destroy_mesh(found->second.handle);
        WiiMenuNative::Mesh mesh;mesh.handle=handle;std::copy_n(picture.vertices,4,mesh.vertices.begin());renderer->meshes[pane]=mesh;found=renderer->meshes.find(pane);
        ++renderer->status.mesh_uploads;renderer->status.live_meshes=renderer->meshes.size();
    }
    picture.draw.mesh=found->second.handle;
    if(!renderer->backend->draw(&picture.draw)){message(renderer->status.error,sizeof(renderer->status.error),"Native Wii Menu draw submission failed");++renderer->status.rejected;return -1;}
    ++renderer->status.draws;return 1;
}
extern "C" void wii_menu_native_status(const WiiMenuNative* renderer,WiiMenuNativeStatus* output){if(renderer && output)*output=renderer->status;}
