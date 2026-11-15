// SPDX-License-Identifier: MIT
#include <wiillr/mm9_screen.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
namespace {
std::array<uint8_t,4096> ram{};std::vector<uint8_t> texture(WII_MM9_TEXTURE_BYTES);
constexpr uint32_t object=0x80000100;uint64_t uploads{},draws{},destroyed{};bool fail{};GcWiiNativeDraw last{};
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
const uint8_t* read(void*,uint32_t a,uint32_t n){a&=0x1fffffff;if(a>=0x39b2c0 && a-0x39b2c0<=texture.size() && n<=texture.size()-(a-0x39b2c0))return texture.data()+a-0x39b2c0;return a<=ram.size() && n<=ram.size()-a?ram.data()+a:nullptr;}
void p16(uint32_t a,uint16_t v){a&=0x1fffffff;ram.at(a)=v>>8;ram.at(a+1)=v&255;}
void p32(uint32_t a,uint32_t v){p16(a,v>>16);p16(a+2,v&65535);}
void pf(uint32_t a,float f){p32(a,std::bit_cast<uint32_t>(f));}
uint64_t create(const GcWiiNativeMesh* m){require(m && m->vertex_count==4 && m->index_count==6 && m->indices[5]==3,"Final-screen native quad topology");return ++uploads;}
int destroy(uint64_t h){require(h>0,"Final-screen mesh identity");++destroyed;return 1;}
int draw(const GcWiiNativeDraw* d){last=*d;if(fail)return 0;++draws;return 1;}
void status(GcWiiNativeRenderStatus*){}
void view_projection_contract() {
    GcWiiNativeDraw camera{};
    camera.model_view[0]=camera.model_view[5]=camera.model_view[10]=1;
    camera.projection[5]=1.f/240;camera.projection[10]=-0.001f;
    camera.projection[11]=-1;camera.projection[15]=1;
    camera.x=12;camera.width=640;camera.height=480;camera.far_depth=1;
    camera.clip_x=3;camera.clip_width=637;camera.clip_height=480;
    camera.material.flags=GC_WII_NATIVE_BLEND;
    const auto original=camera;
    for(const float guest_half:{320.f,416.f}) {
        require(wii_mm9_screen_view_projection(&camera,320,-guest_half,guest_half,0),
                "Original guest camera is admitted");
        require(camera.projection[0]==1/guest_half && camera.projection[3]==0,
                "Original view retains the guest 4:3 or anamorphic-wide camera");
        require(wii_mm9_screen_view_projection(&camera,320,-guest_half,guest_half,1),
                "Enhanced native camera is admitted for either Wii aspect");
        // The real final-screen rectangle spans [-320,320]. Its transformed
        // edges must meet the presentation ABI's 256-pixel center and margins.
        require(camera.projection[0]*-320+camera.projection[3]==-1 &&
                camera.projection[0]*320+camera.projection[3]==1,
                "Enhanced center and margins meet under both guest Wii aspect modes");
        auto untouched=camera;untouched.projection[0]=untouched.projection[3]=0;
        require(!std::memcmp(&untouched,&original,sizeof original),
                "Host view preserves vertical/depth projection, pose, viewport, clip and material");
    }
    const auto valid=camera;
    for(const float invalid:{0.f,-1.f,std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN()})
        require(!wii_mm9_screen_view_projection(&camera,invalid,-416,416,1) &&
                !std::memcmp(&camera,&valid,sizeof valid),"Invalid original width is atomic");
    require(!wii_mm9_screen_view_projection(&camera,320,416,-416,1) &&
            !std::memcmp(&camera,&valid,sizeof valid),"Reversed guest camera is atomic");
    require(!wii_mm9_screen_view_projection(&camera,320,-416,
            std::numeric_limits<float>::infinity(),1) &&
            !std::memcmp(&camera,&valid,sizeof valid),"Nonfinite guest camera is atomic");
    require(!wii_mm9_screen_view_projection(nullptr,320,-416,416,1),"Missing camera is rejected");
}
}
int main(){try {
    view_projection_contract();
    p32(object,0x100);p32(object+8,(4u<<20)|(239u<<10)|255);p32(object+12,0x39b2c0>>5);
    p16(object+0x20,10);p16(object+0x22,20);p16(object+0x24,641);p16(object+0x26,481);pf(object+0x2c,1);pf(object+0x34,1);ram[(object&0x1fffffff)+0x38]=3;
    GcWiiNativeDraw camera{};camera.projection[0]=camera.projection[5]=camera.projection[10]=camera.projection[15]=1;camera.model_view[0]=camera.model_view[5]=camera.model_view[10]=1;
    camera.width=camera.clip_width=640;camera.height=camera.clip_height=480;camera.far_depth=1;camera.material.flags=GC_WII_NATIVE_BLEND;
    WiiMm9Screen screen{};char error[192];const WiiMm9Memory memory{nullptr,read};
    require(wii_mm9_screen_prepare(memory,object,32,&camera,&screen,error,sizeof(error)),error);
    require(screen.vertices[0].position[0]==-310 && screen.vertices[0].position[1]==261 && screen.vertices[2].position[0]==331 && screen.vertices[2].position[1]==-220 && screen.vertices[0].position[2]==75,"Original odd-size screen rectangle and depth");
    require(screen.vertices[0].uv[1]==1 && screen.vertices[1].uv[1]==0 && screen.draw.material.texture.address==0x39b2c0 && screen.draw.material.texture.size==WII_MM9_TEXTURE_BYTES,"Live final-screen UV and real resource identity");
    require(wii_mm9_screen_prepare(memory,object,31,&camera,&screen,error,sizeof(error)) && std::abs(screen.vertices[0].color[1]-251.f/255)<1e-7f && screen.vertices[0].color[0]==1,"Original packed RGB565 brightness");
    const GcWiiNativeRenderApi api{1,create,destroy,draw,status};auto* renderer=wii_mm9_screen_create(memory,&api);require(renderer,"Native screen provider");
    require(wii_mm9_screen_draw(renderer,object,32,&camera)==1,"Initial screen submission");camera.model_view[3]=12;require(wii_mm9_screen_draw(renderer,object,32,&camera)==1 && uploads==1 && last.model_view[3]==12,"Native screen pose reuses retained geometry");
    require(wii_mm9_screen_draw(renderer,object,31,&camera)==1 && uploads==2 && destroyed==1,"Changed packed brightness updates geometry");
    const auto original=screen;p32(object+12,0x39b2e0>>5);require(!wii_mm9_screen_prepare(memory,object,31,&camera,&screen,error,sizeof(error)) && std::memcmp(&screen,&original,sizeof(screen))==0,"Unsupported screen target is atomic");
    require(wii_mm9_screen_draw(renderer,object,31,&camera)==0 && draws==3,"Unsupported queued family cannot submit");p32(object+12,0x39b2c0>>5);fail=true;require(wii_mm9_screen_draw(renderer,object,31,&camera)==-1,"Screen backend failure is distinct from unsupported input");
    wii_mm9_screen_destroy(renderer);require(destroyed==2,"Final-screen resources released");
    p32(object+12,0x800>>5);
    for(unsigned format: {0u,1u,2u,3u,4u,5u,6u,14u}) {
        p32(object+8,format<<20);
        require(wii_mm9_screen_prepare(memory,object,32,&camera,&screen,error,sizeof(error)),error);
        require(screen.draw.material.texture.address==0x800 && screen.draw.material.texture.width==1 && screen.draw.material.texture.height==1 && screen.draw.material.texture.size==(format==6?64u:32u),"Queued texture format preserves actual storage/block size");
    }
    p32(object+8,8u<<20);require(!wii_mm9_screen_prepare(memory,object,32,&camera,&screen,error,sizeof(error)),"Unresolved palette is explicitly rejected");
    p32(object+8,6u<<20);std::memcpy(ram.data()+0x180,ram.data()+0x100,64);
    renderer=wii_mm9_screen_create(memory,&api);fail=false;
    require(wii_mm9_screen_draw(renderer,object,32,&camera)==1 && wii_mm9_screen_draw(renderer,object+0x80,32,&camera)==1 && uploads==4,"Independent queued objects upload their own retained geometry");
    require(wii_mm9_screen_draw(renderer,object,32,&camera)==1 && uploads==4,"Alternating queued objects retain geometry");
    wii_mm9_screen_destroy(renderer);require(destroyed==4,"All queued resources released");std::cout<<"MM9 queued native geometry, brightness, base textures and failures passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
