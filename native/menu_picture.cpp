// SPDX-License-Identifier: GPL-3.0-or-later
#include <wiillr/menu_picture.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace {
constexpr uint32_t sdk_pointer=0x81695618;
void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
struct MaterialFamily:std::runtime_error{using std::runtime_error::runtime_error;};
void material_need(bool b,const char* why){if(!b)throw MaterialFamily(why);}
[[noreturn]] void unknown_material(uint32_t pane,uint32_t material,const uint8_t* equation,unsigned bytes=32) {
    char text[256],hex[65];
    for(unsigned n=0;n<bytes;++n)std::snprintf(hex+n*2,3,"%02x",equation[n]);
    std::snprintf(text,sizeof text,"Wii Menu equation needs another native material family (pane=%08x material=%08x stages=%s)",pane,material,hex);
    throw MaterialFamily(text);
}
void message(char* out,uint32_t size,const char* why){if(out && size)std::snprintf(out,size,"%s",why);}
struct Reader {
    WiiMenuMemory memory;
    const uint8_t* bytes(uint32_t a,uint32_t n)const{auto* p=memory.read(memory.user,a,n);need(p!=nullptr,"Unreadable Wii Menu object range");return p;}
    uint8_t u8(uint32_t a)const{return *bytes(a,1);}
    uint16_t u16(uint32_t a)const{auto* p=bytes(a,2);return uint16_t(uint16_t(p[0])<<8|p[1]);}
    uint32_t u32(uint32_t a)const{auto* p=bytes(a,4);return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
    float f32(uint32_t a)const{const auto f=std::bit_cast<float>(u32(a));need(std::isfinite(f),"Nonfinite Wii Menu geometry");return f;}
};
void texture_transform(const Reader& r,uint32_t address,float (&out)[2][3]) {
    const float rotation=std::remainder(r.f32(address+8),360.f);
    float cosine=1,sine=0;
    if(rotation==90)sine=1,cosine=0;else if(rotation==-90)sine=-1,cosine=0;
    else if(rotation==180 || rotation==-180)cosine=-1;
    else if(rotation!=0) {
        const float radians=rotation*(std::numbers::pi_v<float>/180.f);
        cosine=std::cos(radians);sine=std::sin(radians);
    }
    const float x=r.f32(address+12),y=r.f32(address+16);
    out[0][0]=cosine*x;out[0][1]=-sine*y;
    out[1][0]=sine*x;out[1][1]=cosine*y;
    // NW4R rotates the scaled coordinates about the texture center, then
    // translates. Keep the accepted quarter-turn arithmetic exact.
    out[0][2]=r.f32(address)+.5f-out[0][0]*.5f-out[0][1]*.5f;
    out[1][2]=r.f32(address+4)+.5f-out[1][0]*.5f-out[1][1]*.5f;
    for(const auto& row:out)for(float value:row)need(std::isfinite(value),"Nonfinite generated GUI texture transform");
}
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
GcWiiNativeTexture texture(const Reader& r,uint32_t address) {
    GcWiiNativeTexture out{};const auto image=r.u32(address+8),sampler=r.u32(address);
    out.width=(image&1023)+1;out.height=((image>>10)&1023)+1;out.format=(image>>20)&15;
    out.address=(r.u32(address+12)&0xffffffu)<<5;out.size=texture_bytes(out.format,out.width,out.height);
    out.wrap_s=sampler&3;out.wrap_t=(sampler>>2)&3;out.linear=(sampler>>4)&1;
    need(out.wrap_s<=2 && out.wrap_t<=2,"Invalid Wii Menu sampler wrapping");
    const auto min_filter=(sampler>>5)&7;
    need((min_filter==0 || min_filter==4) && ((min_filter>>2)&1)==out.linear && !(sampler&0x19fe00),"Wii Menu sampler filtering or LOD bias needs a native shader family");
    if(out.format>=8 && out.format<=10) {
        const auto palette=r.u32(address+16);need(palette!=0,"Missing Wii Menu indexed texture palette");
        out.palette_entries=r.u16(palette);out.palette_format=r.u32(palette+4);out.palette_address=r.u32(palette+8)&0x1fffffffu;
        const auto minimum=out.format==8?16u:(out.format==9?256u:16384u);
        need(out.palette_entries>=minimum && out.palette_entries<=16384 && out.palette_format<=2,"Wii Menu palette layout needs a native shader family");
    }
    return out;
}
using QuadUv=std::array<std::array<float,2>,4>;
void relative_uv(const QuadUv& primary,const QuadUv& other,float (&transform)[2][3]) {
    for(const auto* uv:{&primary,&other})need((*uv)[0][0]==(*uv)[3][0] && (*uv)[1][0]==(*uv)[2][0] && (*uv)[0][1]==(*uv)[1][1] && (*uv)[2][1]==(*uv)[3][1],"Wii Menu nonrectangular UVs need another native material family");
    const auto du=primary[1][0]-primary[0][0],dv=primary[3][1]-primary[0][1];
    need(du!=0 && dv!=0,"Wii Menu collapsed UVs need another native material family");
    for(auto& row:transform)for(float& f:row)f=0;
    transform[0][0]=(other[1][0]-other[0][0])/du;transform[0][2]=other[0][0]-transform[0][0]*primary[0][0];
    transform[1][1]=(other[3][1]-other[0][1])/dv;transform[1][2]=other[0][1]-transform[1][1]*primary[0][1];
    for(const auto& row:transform)for(float f:row)need(std::isfinite(f),"Nonfinite Wii Menu relative coordinates");
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
struct QuadInput {
    uint32_t material{},coordinate_count{};
    float x{},y{},width{},height{};
    uint8_t colors[4][4]{};
    std::array<QuadUv,8> coordinates{}; // native LT,RT,RB,LB order
};
#include "menu_program.inc"
WiiMenuPicture prepare_impl(const Reader& r,uint32_t pane,uint32_t info,const WiiMenuView& camera,const QuadInput* quad,bool program) {
    need(quad || r.u32(pane)==wii_menu_classes(r.memory)->picture,"Unbound Wii Menu pane class");
    WiiMenuPicture out{};out.pane=pane;out.material=quad?quad->material:r.u32(pane+0x28);
    if(!camera.clip_width || !camera.clip_height){out.skipped=1;return out;}
    if(!out.material){out.skipped=1;return out;}
    const auto material=out.material;need(r.u32(material)==wii_menu_classes(r.memory)->material,"Unbound Wii Menu material class");
    const Counts cap(r.u32(material+0x4c)),num(r.u32(material+0x50));
    const bool layered=!program && num.textures==3 && num.generators==3 && num.stages==3 && !num.ind_stages;
    const bool single_image=!program && num.textures==1 && num.generators==1 && num.stages==1 && !num.ind_stages;
    enum class Single{None,Shadow,SolidCoverage,OpaqueRange,AlphaRange,Raster};Single single=Single::None;
    const bool range_tint=!program && num.textures==1 && num.generators==1 && num.stages==2 && !num.ind_stages;
    const bool masked=!program && num.textures==2 && num.generators==2 && num.stages==2 && !num.ind_stages;
    material_need(program || layered || single_image || masked || range_tint || (num.textures<=1 && num.generators==num.textures && !num.stages && !num.ind_stages),"Wii Menu material requires a broader native shader family");
    need(num.textures<=cap.textures && num.srts<=cap.srts && num.generators<=cap.generators && num.stages<=cap.stages,"Invalid Wii Menu material allocation");
    need(cap.textures<=8 && cap.srts<=10 && cap.generators<=8 && cap.ind_srts<=3 && cap.ind_stages<=4 && cap.stages<=16,"Unbounded Wii Menu material allocation");
    material_need(program || !cap.swaps || layered || single_image || masked || range_tint,"Wii Menu custom component swaps need a native shader family");
    const auto mem=r.u32(material+0x58);if(cap.textures || cap.srts || cap.generators || cap.channels || cap.color || cap.alpha || cap.blend || cap.swaps || cap.stages || cap.ind_srts || cap.ind_stages)need(mem!=0,"Missing Wii Menu material storage");
    const auto srt=mem+cap.textures*32,gen=srt+cap.srts*20,channel=gen+cap.generators*4;
    const auto mat_color=channel+cap.channels*4,alpha=mat_color+cap.color*4+cap.swaps*4,blend=alpha+cap.alpha*4;
    unsigned color_texture=0;bool mask_register_opacity=false;
    if(masked) {
        constexpr uint8_t rgba_mask[]{0,4,0,0,0x42,0xf8,0,0x61,0x77,0x77,0,0xe1,0,0,0,0,
            1,4,1,0,0x0f,0xf8,0,0x61,0x17,0x74,0,0xe1,0,0,0,0};
        constexpr uint8_t coverage_first[]{0,4,0,0,0xff,0xff,0,0x61,0x77,0x47,0,0xe1,0,0,0,0,
            1,4,1,0,0x42,0xf8,0,0x61,0x77,0x07,0,0xe1,0,0,0,0};
        constexpr uint8_t coverage_last[]{0,4,0,0,0x42,0xf8,0,0x61,0x77,0x77,0,0xe1,0,0,0,0,
            1,4,1,0,0xff,0x0f,0,0x61,0x77,0x47,0,0xe1,0,0,0,0};
        constexpr uint8_t coverage_last_a[]{0,4,0,0,0x42,0xf8,0,0x61,0x77,0x77,0,0xe1,0,0,0,0,
            1,4,1,0,0xff,0x0f,0,0x61,0x74,0x77,0,0xe1,0,0,0,0};
        constexpr uint8_t pane_coverage[]{0,4,0,0,0x42,0xf8,0,0x61,0x77,0x77,0,0xe1,0,0,0,0,
            1,4,1,0,0xaf,0xf0,0,0x61,0x77,0x47,0,0xe1,0,0,0,0};
        constexpr uint8_t additive[]{0,4,0,0,0x24,0xf8,0,0x61,0x75,0x77,0,0xe1,0,0,0,0,
            1,4,1,0,0x6f,0x08,0,0x61,0x75,0x77,0,0xe1,0,0,0,0};
        constexpr uint8_t product[]{0,4,0,0,0xff,0xff,0,0x61,0x77,0x47,0,0xe1,0,0,0,0,
            1,4,1,0,0xff,0x8f,0,0x61,0x47,0x70,0,0xe1,0,0,0,0};
        const auto stages=blend+cap.blend*4+cap.ind_stages*4+cap.ind_srts*20;
        const auto* equation=r.bytes(stages,32);
        if(!std::memcmp(equation,rgba_mask,32)){out.mask.flags=GC_WII_NATIVE_GUI_MASK_RGB;mask_register_opacity=true;}
        else if(!std::memcmp(equation,coverage_first,32))color_texture=1;
        else if(!std::memcmp(equation,pane_coverage,32))out.mask.flags=GC_WII_NATIVE_GUI_MASK_PANE_RGB;
        else if(!std::memcmp(equation,additive,32))out.effect=2;
        else if(!std::memcmp(equation,product,32)){out.effect=3;color_texture=1;}
        else if(std::memcmp(equation,coverage_last,32) && std::memcmp(equation,coverage_last_a,32))unknown_material(pane,material,equation);
        out.needs_mask=out.effect==0;
    }
    if(single_image) {
        constexpr uint8_t mask[]{0,4,0,0,0xff,0x2f,0,0x61,0x17,0x74,0,0xe1,0,0,0,0};
        constexpr uint8_t solid_coverage[]{0,4,0,0,0xff,0x2f,0,0x61,0x77,0x47,0,0xe1,0,0,0,0};
        constexpr uint8_t opaque[]{0,4,0,0,0x42,0xf8,0,0x61,0x77,0x27,0,0xe1,0,0,0,0};
        constexpr uint8_t alpha_range[]{0,0xff,0,0,0xff,0x4f,0,0xf8,0x21,0x74,0,0xf8,0,0,0,0};
        constexpr uint8_t raster[]{0xff,4,0xff,0,0xfa,0xff,0,0x61,0x75,0x77,0,0xe1,0,0,0,0};
        const auto stages=blend+cap.blend*4+cap.ind_stages*4+cap.ind_srts*20;
        const auto* equation=r.bytes(stages,16);
        if(!std::memcmp(equation,mask,16))single=Single::Shadow;
        else if(!std::memcmp(equation,solid_coverage,16))single=Single::SolidCoverage;
        else if(!std::memcmp(equation,opaque,16))single=Single::OpaqueRange;
        else if(!std::memcmp(equation,alpha_range,16))single=Single::AlphaRange;
        else if(!std::memcmp(equation,raster,16))single=Single::Raster;
        else unknown_material(pane,material,equation,16);
    }
    if(range_tint) {
        constexpr uint8_t tinted[]{0,0xff,0,0,0x42,0xf8,0,0xf8,0x21,0x74,0,0xf8,0,0,0,0,
            0xff,4,0xff,0,0x0f,0xfa,0,0x61,0x07,0x75,0,0xe1,0,0,0,0};
        constexpr uint8_t cutoff[]{0,4,0,0,0x42,0xf8,0,0xf8,0x64,0x74,0x0e,0xe1,0,0,0,0,
            0,4,0,0,0xff,0x0f,0,0x61,0x57,0x70,0,0xe1,0,0,0,0};
        const auto stages=blend+cap.blend*4+cap.ind_stages*4+cap.ind_srts*20;
        const auto* equation=r.bytes(stages,32);
        if(!std::memcmp(equation,cutoff,32)){out.effect=1;out.cutout.cutoff=r.u8(material+0x3f);}
        else material_need(!std::memcmp(equation,tinted,32),"Wii Menu one-image effect needs another native material family");
        out.needs_gui=1;
    }
    if((layered || single_image || masked || range_tint) && cap.swaps)for(unsigned i=0;i<4;++i)material_need(r.u8(mat_color+cap.color*4+i)==0xe4,"Wii Menu component swap needs another native material family");
    if(layered) {
        // Match the source-defined complete channel background material, not
        // an arbitrary combiner program. The host API accepts its semantic
        // range, two weighted overlays and affine UVs only.
        constexpr uint8_t background[]{0,4,0,0,0x42,0xf8,0,0x61,0x77,0x27,0,0xe1,0,0,0,0,
            1,4,1,0,0x6f,0x08,0,0x61,0x77,0x07,0,0xe1,0,0,0,0,
            2,4,2,0,0xef,0x08,0,0x61,0x77,0x07,0,0xe1,0,0,0,0};
        const auto stages=blend+cap.blend*4+cap.ind_stages*4+cap.ind_srts*20;
        material_need(!std::memcmp(r.bytes(stages,sizeof background),background,sizeof background),"Wii Menu layered equation needs another native material family");
        for(unsigned c=0;c<4;++c) {
            const int32_t weight=static_cast<int16_t>(r.u16(material+0x34+c*2));
            need(weight>=0 && weight<=255,"Wii Menu signed overlay weight needs another native material family");
            out.layers.weights[0][c]=weight;out.layers.weights[1][c]=r.u8(material+0x3c+c);
        }
        out.needs_layers=1;
    }
    if(cap.alpha)need(r.u8(alpha)==0x77 && r.u8(alpha+1)==0,"Wii Menu alpha test needs a native shader family");
    out.draw.material.flags=GC_WII_NATIVE_BLEND;
    if(cap.blend){const auto mode=r.u8(blend);need(mode==0 || (mode==1 && r.u8(blend+1)==4 && r.u8(blend+2)==5),"Wii Menu blend operation needs a native shader family");out.draw.material.flags=mode==1?GC_WII_NATIVE_BLEND:0;}
    for(unsigned ch=0;ch<4;++ch) {
        const int32_t low=static_cast<int16_t>(r.u16(material+0x24+ch*2));
        const int32_t high=static_cast<int16_t>(r.u16(material+0x2c+ch*2));
        if(!program)material_need(low>=0 && low<=255 && high>=0 && high<=255,"Wii Menu wider color endpoints need a native material family");
        out.color.low[ch]=low;out.color.high[ch]=high;
        if(high!=255 || (num.textures && low!=0))out.needs_gui=1;
    }
    if(single==Single::Shadow) {
        // The authored shadow is a solid RGB color with texture-masked
        // opacity. Its equation has no pane/raster-color input.
        for(unsigned c=0;c<3;++c)out.color.high[c]=out.color.low[c];
        out.color.high[3]=out.color.low[3];out.color.low[3]=0;out.needs_gui=1;
    }
    if(single==Single::SolidCoverage) {
        // Channel-transition silhouettes use solid C0 RGB and the texture's
        // alpha directly, independent of both color-register and pane alpha.
        for(unsigned c=0;c<3;++c)out.color.high[c]=out.color.low[c];
        out.color.low[3]=0;out.color.high[3]=255;out.needs_gui=1;
    }
    if(single==Single::OpaqueRange){out.color.low[3]=out.color.high[3];out.needs_gui=1;}
    if(single==Single::AlphaRange){for(unsigned c=0;c<3;++c)out.color.low[c]=out.color.high[c];out.needs_gui=1;}
    if(single==Single::Raster)out.needs_gui=0;
    if(masked)out.mask.opacity=mask_register_opacity?out.color.low[3]:255;
    if(out.effect==2)for(unsigned c=0;c<4;++c) {
        std::swap(out.color.low[c],out.color.high[c]);const int32_t weight=static_cast<int16_t>(r.u16(material+0x34+c*2));
        need(weight>=0 && weight<=255,"Wii Menu additive color needs another native material family");out.addition.weight[c]=weight;
    }
    if(out.effect==3)for(unsigned c=0;c<4;++c){out.color.low[c]=0;out.color.high[c]=255;}
    const auto global_alpha=r.u8(pane+0xce);unsigned color_source=1,alpha_source=1;
    if(cap.channels){color_source=r.u8(channel);alpha_source=r.u8(channel+1);need(color_source<=1 && alpha_source<=1,"Invalid Wii Menu color source");}
    std::array<uint8_t,4> material_color{255,255,255,255};if(cap.color)for(unsigned ch=0;ch<4;++ch)material_color[ch]=r.u8(mat_color+ch);
    material_color[3]=uint8_t(unsigned(material_color[3])*global_alpha/255);
    const auto width=quad?quad->width:r.f32(pane+0x4c),height=quad?quad->height:r.f32(pane+0x50);
    /* Window content can overlap its authored borders: the original emits
     * signed inner extents (HBM's 31px player panel has two 16px borders). */
    need(std::isfinite(width) && std::isfinite(height) && (quad || (width>=0 && height>=0)),"Invalid Wii Menu quad size");
    const auto base=r.u8(pane+0xcc);need(base<=8,"Invalid Wii Menu pane origin");
    const float x=quad?quad->x:-width*float(base%3)*.5f,y=quad?quad->y:-height*float(base/3)*.5f;
    need(std::isfinite(x) && std::isfinite(y),"Nonfinite Wii Menu quad origin");
    constexpr unsigned color_index[]{0,1,3,2};
    const float xs[]{x,x+width,x+width,x},ys[]{y,y,y+height,y+height};
    for(unsigned v=0;v<4;++v) {
        auto& vertex=out.vertices[v];vertex.position[0]=xs[v];vertex.position[1]=ys[v];
        for(unsigned ch=0;ch<4;++ch) {
            if((single_image && single!=Single::Raster) ||
               (masked && ((ch<3 && !(out.mask.flags&GC_WII_NATIVE_GUI_MASK_PANE_RGB)) || (ch==3 && out.effect!=2))) ||
               (out.effect==1 && ch<3)){vertex.color[ch]=1.f;continue;}
            unsigned value=(ch==3?alpha_source:color_source)?(quad?quad->colors[v][ch]:r.u8(pane+0xd4+color_index[v]*4+ch)):material_color[ch];
            if(ch==3 && alpha_source)value=value*global_alpha/255;
            vertex.color[ch]=float(value)/255.f;
        }
    }
    auto pose=matrix(r,pane+0x84);if(r.u8(info+0x50)&0x80)pose=multiply(matrix(r,info+4),pose);
    if(r.f32(info+0x40)-r.f32(info+0x38)<0)for(unsigned row=0;row<3;++row)pose[row*4+1]=-pose[row*4+1];
    std::copy(pose.begin(),pose.end(),out.draw.model_view);
    view(camera,out.draw);
    if(program) {
        program_material(r,pane,quad,cap,num,mem,srt,gen,blend+cap.blend*4+cap.ind_stages*4+cap.ind_srts*20,mat_color+cap.color*4,out);
        return out;
    }
    if(num.textures && single!=Single::Raster) {
        const auto coordinate_count=quad?quad->coordinate_count:r.u8(pane+0xe5);
        need(coordinate_count>=1 && coordinate_count<=8,"Missing Wii Menu texture coordinates");
        std::array<QuadUv,3> uv{},raw_uv{};std::array<GcWiiNativeTexture,3> images{};float transforms[3][2][3]{};
        for(unsigned t=0;t<num.textures;++t) {
            const auto source=r.u8(gen+t*4+1);
            need(r.u8(gen+t*4)==1 && source>=4 && source-4<coordinate_count,"Wii Menu generated texture coordinates need a native shader family");
            const auto tex_matrix=r.u8(gen+t*4+2);float sx=1,sy=1,tx=0,ty=0,rx=0,ry=0;
            if(tex_matrix!=60) {
                need(tex_matrix>=30 && tex_matrix<=57 && (tex_matrix-30)%3==0,"Invalid Wii Menu texture matrix");const auto slot=(tex_matrix-30)/3;
                need(slot<num.srts,"Missing Wii Menu texture transform");float transform[2][3];
                texture_transform(r,srt+slot*20,transform);
                sx=transform[0][0];rx=transform[0][1];tx=transform[0][2];
                ry=transform[1][0];sy=transform[1][1];ty=transform[1][2];
            }
            const auto coords=quad?0:r.u32(pane+0xe8)+(source-4)*32;
            transforms[t][0][0]=sx;transforms[t][0][1]=rx;transforms[t][0][2]=tx;
            transforms[t][1][0]=ry;transforms[t][1][1]=sy;transforms[t][1][2]=ty;
            for(unsigned v=0;v<4;++v){const auto u=quad?quad->coordinates[source-4][v][0]:r.f32(coords+color_index[v]*8),vcoord=quad?quad->coordinates[source-4][v][1]:r.f32(coords+color_index[v]*8+4);raw_uv[t][v]={u,vcoord};uv[t][v][0]=u*sx+vcoord*rx+tx;uv[t][v][1]=u*ry+vcoord*sy+ty;}
            images[t]=texture(r,mem+t*32);
        }
        out.draw.material.texture=images[color_texture];
        for(unsigned v=0;v<4;++v)std::copy_n(uv[color_texture][v].begin(),2,out.vertices[v].uv);
        if(layered) {
            for(unsigned t=1;t<3;++t) {
                out.layers.overlays[t-1]=images[t];relative_uv(uv[0],uv[t],out.layers.uv[t-1]);
            }
        }
        if(masked) {
            out.mask.mask=images[1-color_texture];
            if(out.effect==3) {
                out.paired_uv=1;
                for(unsigned v=0;v<4;++v){std::copy_n(raw_uv[color_texture][v].begin(),2,out.vertices[v].uv);std::copy_n(raw_uv[1-color_texture][v].begin(),2,out.extra_uv[v]);}
                std::memcpy(out.primary_uv,transforms[color_texture],sizeof out.primary_uv);std::memcpy(out.mask.uv,transforms[1-color_texture],sizeof out.mask.uv);
            }else relative_uv(uv[color_texture],uv[1-color_texture],out.mask.uv);
        }
        if(out.effect==2){out.addition.addition=images[1];std::memcpy(out.addition.uv,out.mask.uv,sizeof out.mask.uv);}
    }
    out.layers.draw=out.draw;out.layers.color=out.color;
    out.mask.draw=out.draw;out.mask.color=out.color;
    out.cutout.image={out.draw,out.color};out.addition.image={out.draw,out.color};
    return out;
}
WiiMenuPicture prepare(const Reader& r,uint32_t pane,uint32_t info,const WiiMenuView& camera,const QuadInput* quad=nullptr) {
    try{return prepare_impl(r,pane,info,camera,quad,false);}
    catch(const MaterialFamily&){return prepare_impl(r,pane,info,camera,quad,true);}
}
WiiMenuWindow prepare_window(const Reader& r,uint32_t pane,uint32_t info,const WiiMenuView& camera) {
    need(r.u32(pane)==wii_menu_classes(r.memory)->window,"Unbound Wii Menu Window class");
    const auto count=r.u8(pane+0x100);const auto frames=r.u32(pane+0xfc);
    need(count==0 || count==1 || count==4 || count==8,"Unsupported Wii Menu Window frame count");
    need(!count || frames,"Missing Wii Menu Window frames");
    const auto width=r.f32(pane+0x4c),height=r.f32(pane+0x50);const auto base=r.u8(pane+0xcc);
    need(width>=0 && height>=0 && base<=8,"Invalid Wii Menu Window geometry");
    const float x=-width*float(base%3)*.5f,y=-height*float(base/3)*.5f;
    auto frame_material=[&](unsigned n){const auto m=r.u32(frames+n*8+4);need(m && r.u32(m)==wii_menu_classes(r.memory)->material,"Unbound Wii Menu Window border material");return m;};
    auto texture_size=[&](uint32_t material){
        if(!Counts(r.u32(material+0x50)).textures)return std::array<float,2>{0,0};
        const auto image=r.u32(r.u32(material+0x58)+8);return std::array<float,2>{float((image&1023)+1),float(((image>>10)&1023)+1)};
    };
    float l=0,t=0,rr=0,b=0;
    if(count){const auto lt=texture_size(frame_material(0)),rb=texture_size(frame_material(count==1?0:3));l=lt[0];t=lt[1];rr=rb[0];b=rb[1];}
    WiiMenuWindow out{};QuadInput q{};q.material=r.u32(pane+0x28);
    q.x=x+l-r.f32(pane+0xd4);q.y=y+t-r.f32(pane+0xdc);
    q.width=width-l+r.f32(pane+0xd4)-rr+r.f32(pane+0xd8);
    q.height=height-t+r.f32(pane+0xdc)-b+r.f32(pane+0xe0);
    constexpr unsigned order[]{0,1,3,2};
    for(unsigned v=0;v<4;++v)for(unsigned c=0;c<4;++c)q.colors[v][c]=r.u8(pane+0xe4+order[v]*4+c);
    q.coordinate_count=r.u8(pane+0xf5);need(q.coordinate_count<=8,"Invalid Wii Menu Window content coordinates");
    for(unsigned c=0;c<q.coordinate_count;++c)for(unsigned v=0;v<4;++v)for(unsigned a=0;a<2;++a)
        q.coordinates[c][v][a]=r.f32(r.u32(pane+0xf8)+c*32+order[v]*8+a*4);
    out.quads[out.count++]=prepare(r,pane,info,camera,&q);
    // The native border kit uses authored frame dimensions and six source-
    // defined flips. No GX calls, display lists or temporary guest objects.
    constexpr unsigned flips[6][4][2]={{{0,0},{1,0},{0,1},{1,1}},{{1,0},{0,0},{1,1},{0,1}},
        {{0,1},{1,1},{0,0},{1,0}},{{0,1},{0,0},{1,1},{1,0}},{{1,1},{0,1},{1,0},{0,0}},{{1,0},{1,1},{0,0},{0,1}}};
    auto border=[&](unsigned frame,unsigned corner,float px,float py,float w,float h,unsigned flip_override=6){
        const auto material=frame_material(frame);if(!Counts(r.u32(material+0x50)).textures)return;
        const auto size=texture_size(material);const auto flip=flip_override<6?flip_override:r.u8(frames+frame*8);
        need(flip<6 && size[0]>0 && size[1]>0,"Invalid Wii Menu Window border flip/texture");
        QuadInput z{};z.material=material;z.x=px;z.y=py;z.width=w;z.height=h;z.coordinate_count=1;
        for(auto& color:z.colors)std::fill_n(color,4,255);
        const unsigned ix=flip==3 || flip==5?1:0,iy=1-ix,anchor=corner==0?0:corner==1?1:corner==2?3:2;
        const auto& f=flips[flip];const float dx=float(int(f[1][ix])-int(f[0][ix]))*size[ix],dy=float(int(f[2][iy])-int(f[0][iy]))*size[iy];
        for(unsigned v=0;v<4;++v){const unsigned source=order[v];
            z.coordinates[0][v][ix]=float(f[anchor][ix])+(float(int(source%2)-int(anchor%2))*w)/dx;
            z.coordinates[0][v][iy]=float(f[anchor][iy])+(float(int(source/2)-int(anchor/2))*h)/dy;
        }
        need(out.count<9,"Unbounded Wii Menu Window border count");out.quads[out.count++]=prepare(r,pane,info,camera,&z);
    };
    if(count==1 || count==4){
        border(0,0,x,y,width-rr,t,count==1?0:6);
        border(count==1?0:1,1,x+width-rr,y,rr,height-b,count==1?1:6);
        border(count==1?0:3,2,x+l,y+height-b,width-l,b,count==1?4:6);
        border(count==1?0:2,3,x,y+t,l,height-t,count==1?2:6);
    }else if(count==8){
        border(0,0,x,y,l,t);border(6,0,x+l,y,width-l-rr,t);
        border(1,1,x+width-rr,y,rr,t);border(5,1,x+width-rr,y+t,rr,height-t-b);
        border(3,2,x+width-rr,y+height-b,rr,b);border(7,2,x+l,y+height-b,width-l-rr,b);
        border(2,3,x,y+height-b,l,b);border(4,3,x,y+t,l,height-t-b);
    }
    return out;
}
}
extern "C" int wii_menu_window_prepare(WiiMenuMemory memory,uint32_t pane,uint32_t info,const WiiMenuView* camera,WiiMenuWindow* output,char* error,uint32_t error_size) {
    if(!memory.read || !camera || !output){message(error,error_size,"Missing native Wii Menu Window input");return 0;}
    try{const auto result=prepare_window({memory},pane,info,*camera);*output=result;message(error,error_size,"");return 1;}catch(const std::exception& e){message(error,error_size,e.what());return 0;}
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
    struct Mesh{uint64_t handle{};std::array<GcWiiNativeVertex,4> vertices{};float extra_uv[4][2]{},program_uv[4][7][2]{};uint8_t paired{},program{},program_uv_count{};};
    WiiMenuMemory memory{};const GcWiiNativeRenderApi* backend{};const GcWiiNativeGuiApi* gui{};const GcWiiNativeGuiLayersApi* layers{};const GcWiiNativeGuiMaskApi* mask{};const GcWiiNativeGuiEffectsApi* effects{};const GcWiiNativeGuiProgramApi* program{};std::map<uint64_t,Mesh> meshes;WiiMenuNativeStatus status{};
};
extern "C" WiiMenuNative* wii_menu_native_create(WiiMenuMemory memory,const GcWiiNativeRenderApi* api) {
    if(!memory.read || !api || api->abi!=GC_WII_NATIVE_RENDER_ABI || !api->create_mesh || !api->destroy_mesh || !api->draw)return nullptr;
    auto out=std::make_unique<WiiMenuNative>();out->memory=memory;out->backend=api;return out.release();
}
extern "C" int wii_menu_native_gui(WiiMenuNative* renderer,const GcWiiNativeGuiApi* api) {
    if(!renderer || !api || api->abi!=GC_WII_NATIVE_GUI_ABI || !api->draw)return 0;
    renderer->gui=api;return 1;
}
extern "C" int wii_menu_native_gui_layers(WiiMenuNative* renderer,const GcWiiNativeGuiLayersApi* api) {
    if(!renderer || !api || api->abi!=GC_WII_NATIVE_GUI_LAYERS_ABI || !api->draw)return 0;
    renderer->layers=api;return 1;
}
extern "C" int wii_menu_native_gui_mask(WiiMenuNative* renderer,const GcWiiNativeGuiMaskApi* api) {
    if(!renderer || !api || api->abi!=GC_WII_NATIVE_GUI_MASK_ABI || !api->draw)return 0;
    renderer->mask=api;return 1;
}
extern "C" int wii_menu_native_gui_effects(WiiMenuNative* renderer,const GcWiiNativeGuiEffectsApi* api) {
    if(!renderer || !api || api->abi!=GC_WII_NATIVE_GUI_EFFECTS_ABI || !api->create_mesh || !api->cutout || !api->add || !api->product)return 0;
    renderer->effects=api;return 1;
}
extern "C" int wii_menu_native_gui_program(WiiMenuNative* renderer,const GcWiiNativeGuiProgramApi* api) {
    if(!renderer || !api || api->abi!=GC_WII_NATIVE_GUI_PROGRAM_ABI || !api->create_mesh || !api->draw)return 0;
    renderer->program=api;return 1;
}
extern "C" void wii_menu_native_forget_picture(WiiMenuNative* renderer,uint32_t pane) {
    if(renderer)for(auto it=renderer->meshes.begin();it!=renderer->meshes.end();) {
        if((it->first>>63) && ((it->first&UINT64_C(0x7fffffffffffffff))>>8)==pane) {
            renderer->backend->destroy_mesh(it->second.handle);it=renderer->meshes.erase(it);
        } else ++it;
    }
    if(!renderer)return;for(unsigned slot=0;slot<9;++slot){auto found=renderer->meshes.find((uint64_t(pane)<<4)|slot);if(found==renderer->meshes.end())continue;
    renderer->backend->destroy_mesh(found->second.handle);renderer->meshes.erase(found);}renderer->status.live_meshes=renderer->meshes.size();
}
extern "C" void wii_menu_native_destroy(WiiMenuNative* renderer) {
    if(!renderer)return;for(const auto& [pane,mesh]:renderer->meshes)renderer->backend->destroy_mesh(mesh.handle);delete renderer;
}
static bool backend_ready(WiiMenuNative* renderer,const WiiMenuPicture& picture) {
    if(picture.skipped)return true;
    if(picture.program_extra_count>7){message(renderer->status.error,sizeof(renderer->status.error),"Invalid GUI mesh coordinate count");++renderer->status.rejected;return false;}
    if(picture.needs_program ? !renderer->program : ((picture.effect && !renderer->effects) || (!picture.effect && ((picture.needs_mask && !renderer->mask) || (picture.needs_layers && !renderer->layers) ||
       (!picture.needs_mask && !picture.needs_layers && picture.needs_gui && !renderer->gui))))) {
        message(renderer->status.error,sizeof(renderer->status.error),"Native GUI material backend unavailable");++renderer->status.rejected;return false;
    }
    return true;
}
static int submit_prepared(WiiMenuNative* renderer,uint64_t key,WiiMenuPicture picture) {
    if(picture.skipped){++renderer->status.skipped;return 1;}
    if(!backend_ready(renderer,picture))return 0;
    auto found=renderer->meshes.find(key);
    if(found==renderer->meshes.end() || found->second.paired!=picture.paired_uv || found->second.program!=picture.needs_program ||
       (picture.needs_program && (found->second.program_uv_count!=picture.program_extra_count || std::memcmp(found->second.program_uv,picture.program_extra_uv,sizeof picture.program_extra_uv))) || std::memcmp(found->second.vertices.data(),picture.vertices,sizeof(picture.vertices)) ||
       (picture.paired_uv && std::memcmp(found->second.extra_uv,picture.extra_uv,sizeof picture.extra_uv))) {
        constexpr uint16_t indices[]{0,1,2,0,2,3};const GcWiiNativeMesh input{picture.vertices,indices,4,6};
        float packed_uv[56]{};
        for(unsigned v=0;v<4;++v)for(unsigned c=0;c<picture.program_extra_count && c<7;++c)for(unsigned a=0;a<2;++a)packed_uv[(v*picture.program_extra_count+c)*2+a]=picture.program_extra_uv[v][c][a];
        const GcWiiNativeGuiMesh multi{input,picture.needs_program?packed_uv:&picture.extra_uv[0][0],picture.needs_program?picture.program_extra_count:(picture.paired_uv?1u:0u)};
        const auto handle=picture.needs_program?renderer->program->create_mesh(&multi):(picture.paired_uv?renderer->effects->create_mesh(&multi):renderer->backend->create_mesh(&input));
        if(!handle){message(renderer->status.error,sizeof(renderer->status.error),"Native Wii Menu mesh creation failed");++renderer->status.rejected;return -1;}
        if(found!=renderer->meshes.end())renderer->backend->destroy_mesh(found->second.handle);
        WiiMenuNative::Mesh mesh;mesh.handle=handle;mesh.paired=picture.paired_uv;mesh.program=picture.needs_program;mesh.program_uv_count=picture.program_extra_count;std::memcpy(mesh.program_uv,picture.program_extra_uv,sizeof mesh.program_uv);std::memcpy(mesh.extra_uv,picture.extra_uv,sizeof mesh.extra_uv);std::copy_n(picture.vertices,4,mesh.vertices.begin());renderer->meshes[key]=mesh;found=renderer->meshes.find(key);
        ++renderer->status.mesh_uploads;renderer->status.live_meshes=renderer->meshes.size();
    }
    picture.draw.mesh=found->second.handle;
    const GcWiiNativeGuiDraw gui_draw{picture.draw,picture.color};
    picture.layers.draw=picture.draw;
    picture.mask.draw=picture.draw;
    picture.cutout.image.draw=picture.draw;picture.addition.image.draw=picture.draw;
    GcWiiNativeGuiProductDraw product{picture.mask,picture.paired_uv,{}};std::memcpy(product.primary_uv,picture.primary_uv,sizeof product.primary_uv);
    picture.program.draw=picture.draw;
    const auto submitted=picture.needs_program?renderer->program->draw(&picture.program):(picture.effect==1?renderer->effects->cutout(&picture.cutout):
        (picture.effect==2?renderer->effects->add(&picture.addition):
        (picture.effect==3?renderer->effects->product(&product):
        (picture.needs_mask?renderer->mask->draw(&picture.mask):
        (picture.needs_layers ? renderer->layers->draw(&picture.layers):
        (picture.needs_gui ? renderer->gui->draw(&gui_draw):renderer->backend->draw(&picture.draw)))))));
    if(!submitted){message(renderer->status.error,sizeof(renderer->status.error),"Native Wii Menu draw submission failed");++renderer->status.rejected;return -1;}
    ++renderer->status.draws;return 1;
}
extern "C" int wii_menu_native_draw_picture(WiiMenuNative* renderer,uint32_t pane,uint32_t info,const WiiMenuView* camera) {
    if(!renderer || !camera)return 0;WiiMenuPicture picture{};
    if(!wii_menu_picture_prepare(renderer->memory,pane,info,camera,&picture,renderer->status.error,sizeof(renderer->status.error))){++renderer->status.rejected;return 0;}
    return submit_prepared(renderer,uint64_t(pane)<<4,picture);
}
extern "C" int wii_menu_native_submit_prepared(WiiMenuNative* renderer,uint64_t key,const WiiMenuPicture* picture) {
    return renderer && picture?submit_prepared(renderer,key,*picture):-1;
}
extern "C" int wii_menu_native_draw_window(WiiMenuNative* renderer,uint32_t pane,uint32_t info,const WiiMenuView* camera) {
    if(!renderer || !camera)return 0;WiiMenuWindow window{};
    if(!wii_menu_window_prepare(renderer->memory,pane,info,camera,&window,renderer->status.error,sizeof(renderer->status.error))){++renderer->status.rejected;return 0;}
    for(unsigned slot=0;slot<window.count;++slot)if(!backend_ready(renderer,window.quads[slot]))return 0;
    for(unsigned slot=0;slot<window.count;++slot){const auto result=submit_prepared(renderer,(uint64_t(pane)<<4)|slot,window.quads[slot]);if(result!=1)return result;}
    return 1;
}
extern "C" void wii_menu_native_status(const WiiMenuNative* renderer,WiiMenuNativeStatus* output){if(renderer && output)*output=renderer->status;}
