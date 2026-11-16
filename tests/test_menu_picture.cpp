// SPDX-License-Identifier: MIT
#include <wiillr/menu_picture.h>
#include <wiillr/menu_text.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <set>
#include <functional>
#include <map>
namespace {
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
std::vector<uint8_t> ram(0x4000);
constexpr uint32_t pane=0x80000100,material=0x80000400,info=0x80000600,mem=0x80000800,coords=0x80000900;
uint64_t uploads{},draws{},destroyed{},next_handle=1;bool fail_draw{};GcWiiNativeDraw last{};
GcWiiNativeGuiMaterial last_gui{};
GcWiiNativeGuiLayersDraw last_layers{};
GcWiiNativeGuiMaskDraw last_mask{};
GcWiiNativeGuiCutoutDraw last_cutout{};GcWiiNativeGuiAddDraw last_add{};GcWiiNativeGuiProductDraw last_product{};
const uint8_t* read(void*,uint32_t address,uint32_t size){address&=0x1fffffffu;if(address==0x1695618)address=0xa00;return address<=ram.size() && size<=ram.size()-address?ram.data()+address:nullptr;}
void p16(uint32_t a,uint16_t v){a&=0x1fffffff;ram.at(a)=v>>8;ram.at(a+1)=v&255;}
void p32(uint32_t a,uint32_t v){p16(a,v>>16);p16(a+2,v&65535);}
void pf(uint32_t a,float f){p32(a,std::bit_cast<uint32_t>(f));}
void identity(uint32_t a){for(unsigned i=0;i<12;++i)pf(a+i*4,i==0 || i==5 || i==10?1.f:0.f);}
WiiMenuView camera(){WiiMenuView out{};out.projection[0]=out.projection[5]=out.projection[15]=1;out.projection[10]=-1;out.width=out.clip_width=640;out.height=out.clip_height=480;out.far_depth=1;return out;}
void fixture() {
    std::fill(ram.begin(),ram.end(),0);p32(pane,0x816717f4);p32(pane+0x28,material);p32(material,0x81671b48);
    for(unsigned i=0;i<4;++i)p16(material+0x2c+i*2,255);
    identity(pane+0x84);pf(pane+0x4c,10);pf(pane+0x50,20);ram[(pane&0x1fffffff)+0xcc]=4;ram[(pane&0x1fffffff)+0xce]=128;
    constexpr uint32_t colors[]{0xff0000ff,0x00ff007f,0x0000ff40,0xffffffff};for(unsigned i=0;i<4;++i)p32(pane+0xd4+i*4,colors[i]);
    identity(info+4);pf(info+0x38,0);pf(info+0x40,480);
}
uint64_t create(const GcWiiNativeMesh* mesh){require(mesh && mesh->vertex_count==4 && mesh->index_count==6,"Native quad topology");require(mesh->indices[0]==0 && mesh->indices[5]==3,"Native quad winding");++uploads;return next_handle++;}
int destroy(uint64_t handle){require(handle>0,"Destroying invalid native mesh");++destroyed;return 1;}
int draw(const GcWiiNativeDraw* job){last=*job;if(fail_draw)return 0;++draws;return 1;}
int draw_gui(const GcWiiNativeGuiDraw* job){last_gui=job->color;return draw(&job->draw);}
int draw_layers(const GcWiiNativeGuiLayersDraw* job){last_layers=*job;return draw(&job->draw);}
int draw_mask(const GcWiiNativeGuiMaskDraw* job){last_mask=*job;return draw(&job->draw);}
uint64_t create_gui(const GcWiiNativeGuiMesh* mesh){require(mesh && mesh->extra_uv && mesh->extra_uv_count==1,"Paired native UV creation");return create(&mesh->mesh);}
int cutout(const GcWiiNativeGuiCutoutDraw* job){last_cutout=*job;return draw(&job->image.draw);}
int add(const GcWiiNativeGuiAddDraw* job){last_add=*job;return draw(&job->image.draw);}
int product(const GcWiiNativeGuiProductDraw* job){last_product=*job;return draw(&job->image.draw);}
void custom_stage(uint32_t address,unsigned slot,std::array<unsigned,4> color,std::array<unsigned,4> alpha) {
    p32(address,(slot<<24)|(4u<<16)|(slot<<8));
    p32(address+4,((color[0]|(color[1]<<4))<<24)|((color[2]|(color[3]<<4))<<16)|0x61);
    p32(address+8,((alpha[0]|(alpha[1]<<4))<<24)|((alpha[2]|(alpha[3]<<4))<<16)|0xe1);p32(address+12,0);
}
void status(GcWiiNativeRenderStatus*){}
void bindings() {
    fixture();auto view=camera();WiiMenuPicture out{};char error[192]{};
    p32(pane,0x802a9aa0);p32(material,0x802a99f0);
    const WiiMenuMemory original{nullptr,read};
    require(!wii_menu_picture_prepare(original,pane,info,&view,&out,error,sizeof error),"Default binding must reject another title's classes");
    const WiiMenuClasses hbm{0x802a9aa0,0x802a9c44,0x802a99f0,0x802a9b5c,0x802aae38,0};
    const WiiMenuMemory bound{nullptr,read,&hbm};
    require(wii_menu_picture_prepare(bound,pane,info,&view,&out,error,sizeof error),error);
    require(out.vertices[0].position[0]==-5 && out.vertices[0].position[1]==-10,"Bound title must retain the resource geometry contract");
    p32(material,0x81671b48);
    require(!wii_menu_picture_prepare(bound,pane,info,&view,&out,error,sizeof error),"Title binding must reject a different material class");
}
void units() {
    fixture();const WiiMenuMemory memory{nullptr,read};auto view=camera();WiiMenuPicture out{};char error[192]{};
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof(error)),error);
    require(out.vertices[0].position[0]==-5 && out.vertices[0].position[1]==-10 && out.vertices[2].position[0]==5 && out.vertices[2].position[1]==10,"Centered pane geometry");
    require(std::abs(out.vertices[0].color[3]-128.f/255)<1e-7f && std::abs(out.vertices[1].color[3]-63.f/255)<1e-7f,"Original integer global alpha");
    require(out.vertices[2].color[0]==1 && out.vertices[3].color[2]==1,"Pane corner order");
    ram[(info&0x1fffffff)+0x50]=0x80;pf(info+4+3*4,20);pf(info+0x40,-480);
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof(error)),error);
    require(out.draw.model_view[3]==20 && out.draw.model_view[5]==-1,"View multiplication and Y-axis column inversion");
    p32(material+0x4c,0x11100000);p32(material+0x50,0x11100000);p32(material+0x58,mem);
    p32(mem,(4u<<5)|(1u<<4)|1u);p32(mem+8,(1u<<20)|(3u<<10)|7);p32(mem+12,0x10000100u>>5);
    // TexSRT = translate.xy, rotation, scale.xy; one generator follows it.
    pf(mem+32,.25f);pf(mem+36,.5f);pf(mem+40,0);pf(mem+44,2);pf(mem+48,.5f);
    p32(mem+52,0x01041eff);ram[(pane&0x1fffffff)+0xe5]=1;p32(pane+0xe8,coords);
    constexpr float uv[]{0,0,1,0,0,1,1,1};for(unsigned n=0;n<8;++n)pf(coords+n*4,uv[n]);
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof(error)),error);
    require(out.draw.material.texture.address==0x10000100 && out.draw.material.texture.size==32 && out.draw.material.texture.width==8,"MEM2 texture identity/layout");
    require(out.vertices[0].uv[0]==-.25f && out.vertices[2].uv[0]==1.75f && out.vertices[0].uv[1]==.75f && out.vertices[2].uv[1]==1.25f,"Native texture scale/translation");
    pf(mem+32,0);pf(mem+36,0);pf(mem+40,90);pf(mem+44,1);pf(mem+48,1);
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error),error);
    require(out.vertices[0].uv[0]==1 && out.vertices[0].uv[1]==0 && out.vertices[1].uv[0]==1 && out.vertices[1].uv[1]==1,"Native quarter-turn texture rotation around center");
    pf(mem+40,-90);require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error),error);
    require(out.vertices[0].uv[0]==0 && out.vertices[0].uv[1]==1,"Native reverse quarter-turn texture rotation");
    pf(mem+40,180);require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error),error);
    require(out.vertices[0].uv[0]==1 && out.vertices[0].uv[1]==1,"Native half-turn texture rotation");
    for(float angle:{30.f,-37.f,45.f,135.f,222.5f,1087.5f}) {
        pf(mem+32,.25f);pf(mem+36,.5f);pf(mem+40,angle);pf(mem+44,2);pf(mem+48,.5f);
        require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error) && !out.needs_program,error);
        const double radians=double(angle)*0.017453292519943295;
        constexpr double u[]{0,1,1,0},v[]{0,0,1,1};
        for(unsigned i=0;i<4;++i) {
            const double x=2*(u[i]-.5),y=.5*(v[i]-.5);
            require(std::abs(out.vertices[i].uv[0]-(.75+std::cos(radians)*x-std::sin(radians)*y))<2e-6 &&
                    std::abs(out.vertices[i].uv[1]-(1.+std::sin(radians)*x+std::cos(radians)*y))<2e-6,
                    "Named material uses centered arbitrary-angle scale/rotation/translation");
        }
    }
    pf(mem+32,.25f);pf(mem+36,.5f);pf(mem+40,0);pf(mem+44,2);pf(mem+48,.5f);
    const GcWiiNativeRenderApi api{1,create,destroy,draw,status};auto* renderer=wii_menu_native_create(memory,&api);require(renderer,"Native provider creation");
    require(wii_menu_native_draw_picture(renderer,pane,info,&view)==1,"Initial native picture draw");
    pf(pane+0x84+3*4,12);require(wii_menu_native_draw_picture(renderer,pane,info,&view)==1,"Animated pose draw");
    require(uploads==1 && draws==2 && last.model_view[3]==32,"Pose animation must reuse geometry");
    ram[(pane&0x1fffffff)+0xce]=255;require(wii_menu_native_draw_picture(renderer,pane,info,&view)==1,"Animated alpha draw");
    require(uploads==2 && destroyed==1,"Changed vertex alpha must replace immutable mesh");
    p32(material+0x50,0x21100000);const auto previous=out;
    require(!wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof(error)) && std::memcmp(&previous,&out,sizeof(out))==0,"Unsupported material must not publish partial output");
    require(wii_menu_native_draw_picture(renderer,pane,info,&view)==0 && draws==3,"Unsupported material must not submit");
    p32(material+0x50,0x11100000);fail_draw=true;require(wii_menu_native_draw_picture(renderer,pane,info,&view)==-1,"Backend failure must be distinct from unsupported family");fail_draw=false;
    wii_menu_native_forget_picture(renderer,pane);WiiMenuNativeStatus result{};wii_menu_native_status(renderer,&result);
    require(result.live_meshes==0 && result.draws==3 && result.rejected==2,"Native provider accounting");
    p32(pane+0x28,0);require(wii_menu_native_draw_picture(renderer,pane,info,&view)==1,"Original null-material no-op");
    wii_menu_native_destroy(renderer);
    fixture();view=camera();p16(material+0x24,52);p16(material+0x2c,190);
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof(error)),error);
    require(out.needs_gui && out.color.low[0]==52 && out.color.high[0]==190 && out.vertices[0].color[0]==1,"Color endpoints must remain separate from pane tint");
    auto* colored=wii_menu_native_create(memory,&api);require(colored,"Color provider creation");
    require(wii_menu_native_draw_picture(colored,pane,info,&view)==0,"Unavailable native material family must reject");
    const GcWiiNativeGuiApi gui{GC_WII_NATIVE_GUI_ABI,draw_gui};
    require(wii_menu_native_gui(colored,&gui),"Native GUI material binding");
    const auto before_upload=uploads;
    require(wii_menu_native_draw_picture(colored,pane,info,&view)==1 && last_gui.high[0]==190,"Native flat tint submission");
    p16(material+0x2c,200);
    require(wii_menu_native_draw_picture(colored,pane,info,&view)==1 && last_gui.high[0]==200 && uploads==before_upload+1,"Animated material endpoints must reuse native geometry");
    const auto remap_previous=out;p16(material+0x2c,1024);
    require(!wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof(error)) && std::memcmp(&out,&remap_previous,sizeof(out))==0,"Invalid signed endpoint must reject atomically");
    wii_menu_native_destroy(colored);
    fixture();view=camera();
    constexpr uint32_t layered_count=(3u<<28)|(3u<<24)|(3u<<20)|(1u<<14)|(3u<<9)|(1u<<8)|(1u<<7)|(1u<<6)|(1u<<5);
    p32(material+0x4c,layered_count);p32(material+0x50,layered_count);p32(material+0x58,mem);
    ram[(pane&0x1fffffff)+0xe5]=3;p32(pane+0xe8,coords);
    for(unsigned t=0;t<3;++t) {
        p32(mem+t*32,0);p32(mem+t*32+8,(1u<<20)|(3u<<10)|7);p32(mem+t*32+12,(0x10000100+t*0x100)>>5);
        pf(mem+96+t*20+12,1);pf(mem+96+t*20+16,1);p32(mem+156+t*4,(1u<<24)|((4+t)<<16)|((30+t*3)<<8));
        for(unsigned n=0;n<8;++n)pf(coords+t*32+n*4,uv[n]);
    }
    pf(mem+116,.25f);pf(mem+120,.5f);pf(mem+128,2);pf(mem+132,5);
    p32(mem+176,0xe4e4e4e4);p32(mem+180,0x77000000);p32(mem+184,0x01040500);
    constexpr uint8_t layers_stage[]{0,4,0,0,0x42,0xf8,0,0x61,0x77,0x27,0,0xe1,0,0,0,0,
        1,4,1,0,0x6f,0x08,0,0x61,0x77,0x07,0,0xe1,0,0,0,0,
        2,4,2,0,0xef,0x08,0,0x61,0x77,0x07,0,0xe1,0,0,0,0};
    std::copy(std::begin(layers_stage),std::end(layers_stage),ram.begin()+0x8bc);
    for(unsigned c=0;c<4;++c){p16(material+0x34+c*2,12);ram[(material&0x1fffffff)+0x3c+c]=8;}
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error),error);
    require(out.needs_layers && out.layers.overlays[1].address==0x10000300,"Three-image material resource identities");
    require(out.layers.uv[0][0][0]==2 && out.layers.uv[0][0][2]==-.25f && out.layers.uv[0][1][1]==5 && out.layers.uv[0][1][2]==-1.5f,"Independent overlay coordinate transform");
    auto* layered=wii_menu_native_create(memory,&api);require(layered,"Layered provider creation");
    require(wii_menu_native_draw_picture(layered,pane,info,&view)==0,"Missing layered backend must decline before upload");
    const GcWiiNativeGuiLayersApi layers_api{1,draw_layers};require(wii_menu_native_gui_layers(layered,&layers_api),"Layered backend binding");
    const auto layer_uploads=uploads;
    require(wii_menu_native_draw_picture(layered,pane,info,&view)==1 && last_layers.weights[0][0]==12,"Layered material native submission");
    pf(mem+120,1.5f);require(wii_menu_native_draw_picture(layered,pane,info,&view)==1 && uploads==layer_uploads+1 && last_layers.uv[0][1][2]==-.5f,"Overlay animation must reuse base geometry");
    const auto layers_previous=out;ram[0x8bc+24]=0x78;
    require(!wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error) && !std::memcmp(&out,&layers_previous,sizeof out),"Invalid layer alpha selector must reject atomically");
    wii_menu_native_destroy(layered);
    fixture();view=camera();
    constexpr uint32_t shadow_count=(1u<<28)|(1u<<24)|(1u<<20)|(1u<<14)|(1u<<9)|(1u<<8)|(1u<<7)|(1u<<6)|(1u<<5);
    p32(material+0x4c,shadow_count);p32(material+0x50,shadow_count);p32(material+0x58,mem);
    p16(material+0x24,20);p16(material+0x26,30);p16(material+0x28,40);p16(material+0x2a,90);
    p32(mem,0);p32(mem+8,(1u<<20)|(3u<<10)|7);p32(mem+12,0x10000100>>5);
    pf(mem+44,1);pf(mem+48,1);p32(mem+52,0x01041e00);p32(mem+56,0x01010000);
    p32(mem+64,0xe4e4e4e4);p32(mem+68,0x77000000);p32(mem+72,0x01040500);
    constexpr uint8_t shadow_stage[]{0,4,0,0,0xff,0x2f,0,0x61,0x17,0x74,0,0xe1,0,0,0,0};
    std::copy(std::begin(shadow_stage),std::end(shadow_stage),ram.begin()+0x84c);
    ram[(pane&0x1fffffff)+0xe5]=1;p32(pane+0xe8,coords);for(unsigned n=0;n<8;++n)pf(coords+n*4,uv[n]);
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error),error);
    require(out.needs_gui && !out.needs_layers && out.color.low[0]==20 && out.color.high[0]==20 && out.color.low[3]==0 && out.color.high[3]==90,"Source-defined constant shadow and masked opacity");
    require(out.vertices[0].color[0]==1 && out.vertices[0].color[1]==1 && out.vertices[0].color[3]==1,"Shadow equation must ignore pane tint and global alpha");
    auto* shadow=wii_menu_native_create(memory,&api);require(wii_menu_native_gui(shadow,&gui),"Existing GUI backend for shadow");
    const auto shadow_uploads=uploads;require(wii_menu_native_draw_picture(shadow,pane,info,&view)==1,"Native shadow submission");
    ram[(pane&0x1fffffff)+0xce]=1;p16(material+0x2a,80);
    require(wii_menu_native_draw_picture(shadow,pane,info,&view)==1 && uploads==shadow_uploads+1 && last_gui.high[3]==80,"Shadow opacity and unused pane animation must reuse geometry");
    wii_menu_native_destroy(shadow);
    custom_stage(mem+76,0,{15,15,15,2},{7,7,7,4});
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error),error);
    require(out.needs_gui && out.color.low[0]==20 && out.color.high[0]==20 &&
        out.color.low[1]==30 && out.color.high[1]==30 && out.color.low[3]==0 && out.color.high[3]==255 &&
        out.vertices[0].color[0]==1 && out.vertices[0].color[3]==1,
        "Channel silhouette keeps solid register RGB and direct texture alpha despite register/pane opacity");
    custom_stage(mem+76,0,{2,4,8,15},{7,7,7,2});
    p16(material+0x24,20);p16(material+0x2c,200);p16(material+0x2a,10);p16(material+0x32,180);
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error),error);
    require(out.color.low[0]==20 && out.color.high[0]==200 && out.color.low[3]==180 && out.color.high[3]==180 && out.vertices[0].color[3]==1,"Opaque source ignores texture and pane alpha before color remap");
    custom_stage(mem+76,0,{15,15,15,4},{1,2,4,7});ram[0x84d]=0xff;ram[0x853]=ram[0x857]=0xf8;
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error),error);
    require(out.color.low[0]==200 && out.color.high[0]==200 && out.color.low[3]==10 && out.color.high[3]==180 && out.vertices[0].color[1]==1,"Bounded unclamped source color and alpha-range material");
    fixture();view=camera();
    constexpr uint32_t mask_count=(2u<<28)|(2u<<24)|(2u<<20)|(1u<<14)|(2u<<9)|(1u<<8)|(1u<<7)|(1u<<6)|(1u<<5);
    p32(material+0x4c,mask_count);p32(material+0x50,mask_count);p32(material+0x58,mem);
    p16(material+0x2a,90);ram[(pane&0x1fffffff)+0xe5]=1;p32(pane+0xe8,coords);
    for(unsigned t=0;t<2;++t){p32(mem+t*32,0);p32(mem+t*32+8,(1u<<20)|(3u<<10)|7);p32(mem+t*32+12,(0x10000100+t*0x100)>>5);pf(mem+64+t*20+12,1);pf(mem+64+t*20+16,1);p32(mem+104+t*4,(1u<<24)|(4u<<16)|((30+t*3)<<8));}
    pf(mem+84,.25f);pf(mem+88,.5f);pf(mem+96,2);pf(mem+100,.5f);
    p32(mem+112,0x01010000);p32(mem+120,0xe4e4e4e4);p32(mem+124,0x77000000);p32(mem+128,0x01040500);
    for(unsigned n=0;n<8;++n)pf(coords+n*4,uv[n]);
    custom_stage(mem+132,0,{2,4,8,15},{7,7,7,7});custom_stage(mem+148,1,{15,0,8,15},{7,1,4,7});
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error),error);
    require(out.needs_mask && out.mask.flags==1 && out.mask.opacity==90 && out.mask.mask.address==0x10000200,"RGBA coverage material and source opacity");
    require(out.mask.uv[0][0]==2 && out.mask.uv[0][2]==-.25f && out.mask.uv[1][1]==.5f && out.mask.uv[1][2]==.75f,"Mask generators may share original TEX0 coordinates");
    auto* masked=wii_menu_native_create(memory,&api);require(masked,"Mask provider creation");
    require(wii_menu_native_draw_picture(masked,pane,info,&view)==0,"Missing mask backend must decline");
    const GcWiiNativeGuiMaskApi mask_api{1,draw_mask};require(wii_menu_native_gui_mask(masked,&mask_api),"Mask backend binding");
    const auto mask_uploads=uploads;require(wii_menu_native_draw_picture(masked,pane,info,&view)==1,"Native mask submission");
    ram[(pane&0x1fffffff)+0xce]=1;p16(material+0x2a,70);
    require(wii_menu_native_draw_picture(masked,pane,info,&view)==1 && uploads==mask_uploads+1 && last_mask.opacity==70,"Mask opacity and unused pane alpha preserve geometry");
    wii_menu_native_destroy(masked);
    custom_stage(mem+132,0,{15,15,15,15},{7,7,7,4});custom_stage(mem+148,1,{2,4,8,15},{7,7,7,0});
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error),error);
    require(out.draw.material.texture.address==0x10000200 && out.mask.mask.address==0x10000100 && out.mask.flags==0 && out.mask.opacity==255,"Coverage-first material maps independent color/mask resources");
    require(out.vertices[0].uv[0]==-.25f && out.mask.uv[0][0]==.5f && out.mask.uv[0][2]==.125f && out.mask.uv[1][1]==2 && out.mask.uv[1][2]==-1.5f,"Coverage-first coordinates invert the color mapping");
    custom_stage(mem+132,0,{2,4,8,15},{7,7,7,7});custom_stage(mem+148,1,{15,15,15,0},{7,7,7,4});
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error) && out.mask.opacity==255 && out.mask.flags==0,"Coverage-last material");
    custom_stage(mem+148,1,{15,15,15,0},{4,7,7,7});
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error),"Equivalent direct-alpha coverage material");
    custom_stage(mem+148,1,{15,10,0,15},{7,7,7,4});
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error),error);
    require(out.mask.flags==GC_WII_NATIVE_GUI_MASK_PANE_RGB && out.mask.opacity==255 &&
        out.vertices[0].color[0]==1 && out.vertices[0].color[1]==0 &&
        out.vertices[1].color[0]==0 && out.vertices[1].color[1]==1 && out.vertices[0].color[3]==1,
        "Channel transition retains pane RGB without multiplying pane alpha");
    const auto mask_previous=out;ram[0x884+6]=2;
    require(!wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error) && !std::memcmp(&out,&mask_previous,sizeof out),"Invalid mask arithmetic must reject atomically");
    // The same one-image allocation can select the authored threshold effect.
    fixture();view=camera();const auto one_pair=mask_count-(1u<<28)-(1u<<24)-(1u<<20);
    p32(material+0x4c,one_pair);p32(material+0x50,one_pair);p32(material+0x58,mem);
    p32(mem,0);p32(mem+8,(1u<<20)|(3u<<10)|7);p32(mem+12,0x10000100>>5);
    pf(mem+44,1);pf(mem+48,1);p32(mem+52,0x01041e00);p32(mem+56,0x01010000);p32(mem+64,0xe4e4e4e4);p32(mem+68,0x77000000);p32(mem+72,0x01040500);
    ram[(pane&0x1fffffff)+0xe5]=1;p32(pane+0xe8,coords);for(unsigned n=0;n<8;++n)pf(coords+n*4,uv[n]);
    custom_stage(mem+76,0,{2,4,8,15},{4,6,4,7});ram[0x853]=0xf8;ram[0x856]=14;
    custom_stage(mem+92,0,{15,15,15,0},{7,5,0,7});p32(material+0x3c,0xffffffbe);
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error),error);
    require(out.effect==1 && out.cutout.cutoff==190 && out.vertices[0].color[1]==1 && out.vertices[0].color[3]==128.f/255,"Texture cutoff with independent pane alpha");
    const GcWiiNativeGuiEffectsApi effects{1,create_gui,cutout,add,product};auto* effect=wii_menu_native_create(memory,&api);
    require(wii_menu_native_draw_picture(effect,pane,info,&view)==0,"Missing effect backend declines before upload");
    require(wii_menu_native_gui_effects(effect,&effects) && wii_menu_native_draw_picture(effect,pane,info,&view)==1 && last_cutout.cutoff==190,"Cutoff semantic submission");
    wii_menu_native_destroy(effect);
    p32(material+0x4c,mask_count);p32(material+0x50,mask_count);
    for(unsigned t=0;t<2;++t){p32(mem+t*32,0);p32(mem+t*32+8,(1u<<20)|(3u<<10)|7);p32(mem+t*32+12,(0x10000100+t*0x100)>>5);pf(mem+64+t*20,0);pf(mem+64+t*20+4,0);pf(mem+64+t*20+8,0);pf(mem+64+t*20+12,1);pf(mem+64+t*20+16,1);p32(mem+104+t*4,(1u<<24)|((4+t)<<16)|((30+t*3)<<8));}
    p32(mem+112,0x01010000);p32(mem+120,0xe4e4e4e4);p32(mem+124,0x77000000);p32(mem+128,0x01040500);
    ram[(pane&0x1fffffff)+0xe5]=2;for(unsigned t=0;t<2;++t)for(unsigned n=0;n<8;++n)pf(coords+t*32+n*4,uv[n]);
    for(unsigned c=0;c<4;++c){p16(material+0x24+c*2,0);p16(material+0x2c+c*2,40);p16(material+0x34+c*2,15);}
    custom_stage(mem+132,0,{4,2,8,15},{5,7,7,7});custom_stage(mem+148,1,{15,6,8,0},{5,7,7,7});
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error),error);
    require(out.effect==2 && out.addition.image.color.low[0]==40 && out.addition.image.color.high[0]==0 && out.addition.weight[0]==15 && out.vertices[0].color[3]==128.f/255,"Two-image additive ordering and pane alpha");
    custom_stage(mem+132,0,{15,15,15,15},{7,7,7,4});custom_stage(mem+148,1,{15,15,15,8},{7,4,0,7});
    pf(coords+32+8,1.33f);pf(coords+32+24,1.333f);pf(mem+84,1);
    require(wii_menu_picture_prepare(memory,pane,info,&view,&out,error,sizeof error),error);
    require(out.effect==3 && out.paired_uv && out.vertices[1].uv[0]==1.33f && out.vertices[2].uv[0]==1.333f && out.primary_uv[0][2]==1,"Non-affine independent raw UVs retained");
    effect=wii_menu_native_create(memory,&api);require(wii_menu_native_gui_effects(effect,&effects),"Paired effect binding");
    const auto paired_uploads=uploads;require(wii_menu_native_draw_picture(effect,pane,info,&view)==1,"Paired product submission");
    pf(mem+84,2);require(wii_menu_native_draw_picture(effect,pane,info,&view)==1 && uploads==paired_uploads+1 && last_product.primary_uv[0][2]==2,"Paired material transform animation retains geometry");
    wii_menu_native_destroy(effect);
    p32(0xa00,0x80001000);p32(0x1528,1);pf(0x152c,1);pf(0x1534,1);pf(0x153c,-1);pf(0x1540,0);
    pf(0x154c,640);pf(0x1550,480);pf(0x1558,1);p32(0x1148,(342u<<12)|342u);p32(0x114c,(981u<<12)|821u);p32(0x1220,24);
    WiiMenuView sdk{};require(wii_menu_picture_sdk_view(memory,&sdk,error,sizeof(error)),error);
    require(sdk.width==640 && sdk.height==480 && sdk.clip_width==640 && sdk.clip_height==480 && sdk.projection[15]==1,"Original SDK view layout");
    std::cout<<"Native Menu pane geometry, alpha, pose, textures, cache and explicit failures passed\n";
}
void windows() {
    fixture();p32(pane,0x81671a20);pf(pane+0x4c,100);pf(pane+0x50,80);ram[(pane&0x1fffffff)+0xce]=255;
    pf(pane+0xd4,2);pf(pane+0xd8,3);pf(pane+0xdc,4);pf(pane+0xe0,5);
    for(unsigned c=0;c<4;++c)p32(pane+0xe4+c*4,0xffffffff);
    ram[(pane&0x1fffffff)+0xf5]=1;p32(pane+0xf8,coords);p32(pane+0xfc,0x80000c00);ram[(pane&0x1fffffff)+0x100]=1;
    p32(material+0x4c,0x11100000);p32(material+0x50,0x11100000);p32(material+0x58,mem);
    p32(mem,0);p32(mem+8,(1u<<20)|(3u<<10)|7);p32(mem+12,0x10000100u>>5);
    pf(mem+44,1);pf(mem+48,1);p32(mem+52,0x01043cff);
    constexpr float uv[]{0,0,1,0,0,1,1,1};for(unsigned n=0;n<8;++n)pf(coords+n*4,uv[n]);
    for(unsigned frame=0;frame<8;++frame){p32(0x80000c00+frame*8,0);p32(0x80000c04+frame*8,material);}
    const WiiMenuMemory memory{nullptr,read};auto view=camera();WiiMenuWindow out{};char error[192]{};
    require(wii_menu_window_prepare(memory,pane,info,&view,&out,error,sizeof error),error);
    require(out.count==5 && out.quads[0].vertices[0].position[0]==-44 && out.quads[0].vertices[0].position[1]==-40 && out.quads[0].vertices[2].position[0]==45,"Window content inflation and pane origin");
    require(out.quads[1].vertices[1].uv[0]==11.5f && out.quads[1].vertices[2].uv[1]==1,"Single-image Window border repeats");
    require(out.quads[2].vertices[0].uv[0]==1 && out.quads[2].vertices[1].uv[0]==0 && out.quads[2].vertices[2].uv[1]==19,"Single-image Window mirrored right border");
    pf(pane+0x50,7);pf(pane+0xdc,0);pf(pane+0xe0,0);
    require(wii_menu_window_prepare(memory,pane,info,&view,&out,error,sizeof error),error);
    require(out.quads[0].vertices[2].position[1]-out.quads[0].vertices[0].position[1]==-1,"Original signed content extent when authored borders overlap");
    pf(pane+0x50,80);pf(pane+0xdc,4);pf(pane+0xe0,5);
    ram[(pane&0x1fffffff)+0x100]=4;ram[0xc00+3*8]=3;
    require(wii_menu_window_prepare(memory,pane,info,&view,&out,error,sizeof error) && out.count==5,error);
    require(out.quads[3].vertices[0].uv[0]==.5f && out.quads[3].vertices[0].uv[1]==23,"Four-image Window clockwise border flip");
    ram[(pane&0x1fffffff)+0x100]=8;
    require(wii_menu_window_prepare(memory,pane,info,&view,&out,error,sizeof error) && out.count==9,error);
    require(out.quads[2].vertices[0].position[0]==-42 && out.quads[2].vertices[1].position[0]==42,"Eight-image Window top edge extent");
    const auto previous=out;ram[(pane&0x1fffffff)+0x100]=2;
    require(!wii_menu_window_prepare(memory,pane,info,&view,&out,error,sizeof error) && !std::memcmp(&out,&previous,sizeof out),"Unsupported Window must not publish partial geometry");
    std::cout<<"Native Window content, four/eight borders and flips passed\n";
}
struct Fixture {std::vector<uint8_t> mem1,mem2;};
const uint8_t* fixture_read(void* user,uint32_t address,uint32_t size) {
    const auto& f=*static_cast<const Fixture*>(user);address&=0x1fffffff;
    const auto* bank=&f.mem1;if(address>=0x10000000){address-=0x10000000;bank=&f.mem2;}
    return address<=bank->size() && size<=bank->size()-address?bank->data()+address:nullptr;
}
std::vector<uint8_t> file(const char* path){std::ifstream input(path,std::ios::binary);require(bool(input),"Missing private memory fixture");return {std::istreambuf_iterator<char>(input),{}};}
void snapshot(const char* mem1,const char* mem2) {
    Fixture f{file(mem1),file(mem2)};WiiMenuMemory memory{&f,fixture_read};char error[192];WiiMenuView view{};
    require(wii_menu_picture_sdk_view(memory,&view,error,sizeof(error)),error);
    uint32_t info=0;
    for(const auto* bank:{&f.mem1,&f.mem2})for(size_t at=0;at+0x54<=bank->size();at+=4)if((*bank)[at]==0x81 && (*bank)[at+1]==0x67 && (*bank)[at+2]==0x1b && (*bank)[at+3]==0x70){info=uint32_t(at)+(bank==&f.mem1?0x80000000u:0x90000000u);break;}
    require(info!=0,"No original DrawInfo object in fixture");
    size_t accepted=0,rejected=0;
    for(size_t at=0;at+0xec<=f.mem2.size();at+=4)if(f.mem2[at]==0x81 && f.mem2[at+1]==0x67 && f.mem2[at+2]==0x17 && f.mem2[at+3]==0xf4) {
        WiiMenuPicture picture{};const auto address=uint32_t(at)+0x90000000u;const auto ok=wii_menu_picture_prepare(memory,address,info,&view,&picture,error,sizeof(error));
        const auto* name=reinterpret_cast<const char*>(f.mem2.data()+at+0xb4);
        std::cout<<(ok?"accepted ":"rejected ")<<std::hex<<address<<std::dec<<' '<<std::string(name,std::find(name,name+16,'\0'))<<' '<<(ok?"":error)<<'\n';
        if(ok)++accepted;else ++rejected;
    }
    std::cout<<"live_pictures_accepted="<<accepted<<" live_pictures_rejected="<<rejected<<" original_drawinfo="<<std::hex<<info<<std::dec<<'\n';
    require(accepted>0,"No real original Picture object reached native preparation");
}
void hbm_snapshot(const char* mem1,const char* mem2) {
    Fixture f{file(mem1),file(mem2)};
    const WiiMenuClasses classes{0x802a9aa0,0x802a9c44,0x802a99f0,0x802a9b5c,0x802aae38,0};
    const WiiMenuMemory memory{&f,fixture_read,&classes};
    const auto word=[&](uint32_t address){const auto* p=fixture_read(&f,address,4);require(p,"Unreadable HBM object");return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];};
    const auto byte=[&](uint32_t address){const auto* p=fixture_read(&f,address,1);require(p,"Unreadable HBM byte");return *p;};
    const auto object=word(0x804a6d30),draw_info=object+0x274;
    auto view=camera();std::fill_n(view.projection,16,0.f);
    view.projection[0]=1.f/(304.f*(640.f/670.f));view.projection[5]=1.f/240.f;
    view.projection[10]=-1.f/500.f;view.projection[11]=-1;view.projection[15]=1;view.depth_func=7;
    std::set<uint32_t> seen;std::map<std::string,unsigned> errors;unsigned pictures=0,windows=0,texts=0;
    std::function<void(uint32_t)> walk=[&](uint32_t pane){
        require(seen.size()<4096 && seen.insert(pane).second,"Cyclic HBM pane hierarchy");
        if(!(byte(pane+0xcf)&1))return;
        char error[192]{};bool ok=true;
        switch(word(pane)) {
        case 0x802a9a38:case 0x802a9820:require(!(byte(draw_info+0x50)&8),"HBM debug outlines need a provider");break;
        case 0x802a9aa0:{WiiMenuPicture out{};ok=wii_menu_picture_prepare(memory,pane,draw_info,&view,&out,error,sizeof error);++pictures;break;}
        case 0x802a9c44:{WiiMenuWindow out{};ok=wii_menu_window_prepare(memory,pane,draw_info,&view,&out,error,sizeof error);++windows;break;}
        case 0x802a9b5c:ok=wii_menu_text_validate(memory,pane,draw_info,&view,error,sizeof error);++texts;break;
        default:throw std::runtime_error("Unbound HBM pane class");
        }
        if(!ok){if(!errors[error]++)std::cerr<<"HBM rejected "<<std::hex<<pane<<std::dec<<": "<<error<<'\n';}
        auto node=word(pane+0x14);unsigned count=0;
        while(node!=pane+0x14){require(count++<1024,"Cyclic HBM child list");walk(node-4);node=word(node);}
    };
    for(const auto offset:{0x244u,0x240u,0x254u,0x250u,0x24cu,0x248u}) {
        const auto layout=word(object+offset);require(word(layout)==0x802a9960,"Unbound HBM Layout class");walk(word(layout+0x10));
    }
    std::cout<<"HBM prepared pictures="<<pictures<<" windows="<<windows<<" text="<<texts<<" error_families="<<errors.size()<<'\n';
    require(errors.empty(),"Original HBM scene still has unsupported materials");
}
}
int main(int argc,char** argv){try{bindings();units();windows();if(argc==3)snapshot(argv[1],argv[2]);if(argc==4 && std::string(argv[1])=="--hbm")hbm_snapshot(argv[2],argv[3]);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
