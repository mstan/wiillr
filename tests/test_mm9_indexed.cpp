// SPDX-License-Identifier: MIT
#include <wiillr/mm9_indexed.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static unsigned uploads,draws,indexed_draws,reads,destroys;
static GcWiiNativeDraw last;
static void check(bool b,const char* why){if(!b){std::fprintf(stderr,"%s\n",why);std::abort();}}
static const uint8_t* read(void*,uint32_t,uint32_t){++reads;return nullptr;}
static uint64_t create(const GcWiiNativeMesh* p){check(p->vertex_count==4 && p->index_count==6,"native quad shape");return ++uploads;}
static int destroy(uint64_t){++destroys;return 1;}
static int draw(const GcWiiNativeDraw* p){last=*p;++draws;return 1;}
static int indexed(const GcWiiNativeDraw* p){last=*p;++indexed_draws;return 1;}
int main() {
    GcWiiNativeRenderApi render{1,create,destroy,draw,nullptr};GcWiiNativeIndexedApi api{1,indexed};
    auto* renderer=wii_mm9_indexed_create({nullptr,read},&render,&api);check(renderer,"indexed provider");
    WiiMm9Screen canvas{};canvas.draw.material.texture={0x8039b2c0,256*240*2,4,256,240};
    constexpr float x[]{-256,-256,256,256},y[]{240,-240,-240,240},u[]{0,0,1,1},v[]{1,0,0,1};
    for(unsigned n=0;n<4;++n){auto& p=canvas.vertices[n];p.position[0]=x[n];p.position[1]=y[n];p.position[2]=50;p.uv[0]=u[n];p.uv[1]=v[n];for(float& c:p.color)c=1;}
    WiiMm9IndexedTile input{0x80600000,0x80500000,256,256,8,16,8,8,0,32,24,8,8};
    WiiMm9Screen out{};char error[192];check(wii_mm9_indexed_prepare(&input,&canvas,&out,error,sizeof error),error);
    check(out.vertices[1].position[0]==-192 && out.vertices[1].position[1]==-192 && out.vertices[3].position[0]==-176 && out.vertices[3].position[1]==-176,"logical source tile uses actual queued canvas placement");
    check(out.vertices[1].uv[0]==8.f/256 && out.vertices[1].uv[1]==16.f/256,"CI4 atlas rectangle");
    check(out.draw.material.texture.palette_format==GC_WII_NATIVE_PALETTE_RGB5551_ZERO && out.draw.material.texture.size==32768,"native asset descriptor, not composed pixels");
    check(wii_mm9_screen_draw_prepared(renderer,1,&out)==1 && uploads==1 && indexed_draws==1 && !draws,"retained native indexed draw");
    GcWiiNativeDraw retained{};
    check(wii_mm9_indexed_retain(renderer,1,&out,&retained)==1 && retained.mesh==last.mesh &&
        uploads==1 && indexed_draws==1 && !draws,"offscreen view shares stock mesh without a second draw or upload");
    input.palette+=32;check(wii_mm9_indexed_prepare(&input,&canvas,&out,error,sizeof error),error);
    check(wii_mm9_screen_draw_prepared(renderer,1,&out)==1 && uploads==1 && indexed_draws==2 && last.material.texture.palette_address==input.palette,"live palette does not reupload geometry");
    input.flips=3;check(wii_mm9_indexed_prepare(&input,&canvas,&out,error,sizeof error),error);
    check(out.vertices[1].uv[0]==16.f/256 && out.vertices[1].uv[1]==24.f/256,"independent sprite flips");
    const auto unchanged=out;input.source_x=256;
    check(!wii_mm9_indexed_prepare(&input,&canvas,&out,error,sizeof error) && !std::memcmp(&out,&unchanged,sizeof out),"invalid asset publishes nothing");
    input.source_x=8;canvas.draw.material.texture.linear=1;
    check(!wii_mm9_indexed_prepare(&input,&canvas,&out,error,sizeof error),"filtered canvas is not silently substituted");
    check(reads==0,"tile provider must not rasterize/read source pixels on CPU");
    wii_mm9_screen_destroy(renderer);check(destroys==1,"retained mesh lifetime");
    std::puts("MM9 indexed provider: native source rectangles, flips, retained geometry and failure atomicity passed");
}
