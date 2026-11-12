/* SPDX-License-Identifier: MIT
 * Immutable GPU output with owner-thread observation. The row/token manager
 * is shared with gcnllr. Like its Windows memory watch, a vectored fault
 * handler covers raw compiled pointers without adding a check to every load.
 * No callback or GPU thread may publish guest RAM in the background. */
#include <wiillr/gx_output.h>
#include <gcnllr/resident_memory.h>
#include <cpu/native_code.h>
#include <memory/memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#if defined(_WIN32) && defined(_WIN64) && WIILLR_NATIVE_OUTPUT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
typedef struct {
    WiiGx* gx;
    GcnResidentMemory* memory;
    u8* pages;
    u8* armed;
    u32 page_size,page_count;
    DWORD thread;
    void* handler;
    bool materializing,failed;
    u64 faults,eager_copies,resident_presents;
} Output;
static Output* active;
static bool protect(Output* o,bool enable) {
    bool ok=true;
    for(u32 page=0;page<o->page_count;page++) {
        const bool arm=enable && o->pages[page];
        if(arm==!!o->armed[page])continue;
        DWORD before;
        if(!VirtualProtect(o->gx->cpu->ram+(size_t)page*o->page_size,o->page_size,
                           arm?PAGE_NOACCESS:PAGE_READWRITE,&before))ok=false;
        else o->armed[page]=arm;
    }
    return ok;
}
static bool materialize(void* user,const GcnResidentCopy* copy) {
    Output* o=user;
    if(o->materializing || GetCurrentThreadId()!=o->thread)return false;
    // The materializer only writes its real retained bytes. Its callback must
    // bypass access guards; no guest access occurs until protection is restored.
    if(!protect(o,false))return false;
    o->materializing=true;
    const bool ok=o->gx->api->materialize_copy(copy->token)!=0;
    o->materializing=false;
    if(!ok) {o->failed=true;(void)protect(o,true);}
    return ok;
}
static void release(void* user,const GcnResidentCopy* copy) {
    Output* o=user;o->gx->api->release_copy(copy->token);
    if(!protect(o,true))o->failed=true;
}
static LONG CALLBACK observe(EXCEPTION_POINTERS* exception) {
    Output* o=active;const EXCEPTION_RECORD* r=exception->ExceptionRecord;
    if(!o || o->failed || o->materializing || GetCurrentThreadId()!=o->thread ||
       r->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || r->NumberParameters<2 ||
       (r->ExceptionInformation[0]!=0 && r->ExceptionInformation[0]!=1))return EXCEPTION_CONTINUE_SEARCH;
    const uintptr_t address=(uintptr_t)r->ExceptionInformation[1],base=(uintptr_t)o->gx->cpu->ram;
    if(address<base || address-base>=o->gx->cpu->ram_size)return EXCEPTION_CONTINUE_SEARCH;
    const u32 offset=(u32)(address-base),page=offset/o->page_size;
    if(!o->armed[page])return EXCEPTION_CONTINUE_SEARCH;
    const DWORD error=GetLastError();const int saved_errno=errno;++o->faults;
    // Page-sharing padding also needs its page made readable. The manager
    // materializes every owned record touching this actual faulted page.
    const bool ok=gcn_resident_memory_prepare_read(o->memory,page*o->page_size,o->page_size);
    if(!ok || !protect(o,true)) {o->failed=true;errno=saved_errno;SetLastError(error);return EXCEPTION_CONTINUE_SEARCH;}
    errno=saved_errno;SetLastError(error);return EXCEPTION_CONTINUE_EXECUTION;
}
bool wii_gx_output_open(WiiGx* g) {
    SYSTEM_INFO system;GetSystemInfo(&system);const u32 page=system.dwPageSize;
    if(active || !g || !g->api || !g->api->materialize_copy || !g->api->release_copy || !g->api->present_copy ||
       !page || (uintptr_t)g->cpu->ram%page || g->cpu->ram_size%page)return false;
    MEMORY_BASIC_INFORMATION info;
    for(u32 at=0;at<g->cpu->ram_size;at+=page)
        if(!VirtualQuery(g->cpu->ram+at,&info,sizeof info) || info.State!=MEM_COMMIT ||
           info.Protect!=PAGE_READWRITE)return false;
    Output* o=calloc(1,sizeof *o);if(!o)return false;
    o->gx=g;o->page_size=page;o->page_count=g->cpu->ram_size/page;o->thread=GetCurrentThreadId();
    o->pages=calloc(o->page_count,1);o->armed=calloc(o->page_count,1);
    if(o->pages && o->armed) {
        const GcnResidentMemoryConfig c={g->cpu->ram_size,page,32,o->pages,o->page_count,materialize,release,o};
        o->memory=gcn_resident_memory_create(&c);
    }
    if(o->memory)o->handler=AddVectoredExceptionHandler(1,observe);
    if(!o->handler) {if(o->memory)gcn_resident_memory_destroy(o->memory);free(o->pages);free(o->armed);free(o);return false;}
    g->output=active=o;return true;
}
bool wii_gx_output_close(WiiGx* g) {
    Output* o=g->output;if(!o)return true;
    if(GetCurrentThreadId()!=o->thread || !gcn_resident_memory_flush(o->memory) || !protect(o,false) ||
       !gcn_resident_memory_destroy(o->memory))return false;
    o->memory=NULL;RemoveVectoredExceptionHandler(o->handler);active=NULL;g->output=NULL;
    fprintf(stderr,"[native-output] faults=%llu resident_presents=%llu eager_bank_copies=%llu\n",
        (unsigned long long)o->faults,(unsigned long long)o->resident_presents,(unsigned long long)o->eager_copies);
    free(o->pages);free(o->armed);free(o);return true;
}
static void logical_write(WiiGx* g,const GcnllrNativeGxCopy* c) {
    const bool dense=c->stride==c->row_bytes;
    const u32 rows=dense?1:c->rows,bytes=dense?c->row_bytes*c->rows:c->row_bytes;
    for(u32 row=0;row<rows;row++)gcn_native_code_content_dirty(c->address+row*c->stride,bytes);
    if(g->cpu->reserve_valid) {
        const u32 reservation=gcn_reservation_key(g->cpu->reserve_addr)&~31u;
        for(u32 row=0;row<rows;row++) {
            const u32 at=c->address+row*c->stride;
            if((u64)reservation+32>at && reservation<(u64)at+bytes)g->cpu->reserve_valid=false;
        }
    }
    ++g->copies;
}
int wii_gx_output_own(void* user,const GcnllrNativeGxCopy* c) {
    WiiGx* g=user;Output* o=g->output;
    if(!o || !c || o->failed || GetCurrentThreadId()!=o->thread)return 0;
    // MEM2 retains the real eager bank-qualified floor. It is heap allocated,
    // and protecting a shared allocator page would trap unrelated host data.
    if(c->address>=g->cpu->ram_size) {
        if(!g->api->materialize_copy(c->token))return 0;
        g->api->release_copy(c->token);++o->eager_copies;logical_write(g,c);return 1;
    }
    const GcnResidentRows rows={c->address,c->row_bytes,c->rows,c->stride};
    if(!gcn_resident_memory_publish(o->memory,c->token,&rows,NULL) || !protect(o,true))return 0;
    logical_write(g,c);return 1;
}
int wii_gx_output_pending(void* user,u32 address,u32 size) {
    WiiGx* g=user;Output* o=g->output;const u32 at=address&0x1fffffff;
    return o && at<g->cpu->ram_size && gcn_resident_memory_pending(o->memory,at,size);
}
int wii_gx_output_prepare(void* user,u32 address,u32 size) {
    WiiGx* g=user;Output* o=g->output;const u32 at=address&0x1fffffff;
    if(!o || at>=g->cpu->ram_size)return 1;
    if(o->failed || GetCurrentThreadId()!=o->thread || size>g->cpu->ram_size-at)return 0;
    if(!size)return 1;
    /* VirtualProtect guards complete pages, including padding outside the
     * retained rows. Prepare those pages before a foreign GPU API accesses
     * them; a fault inside its device lock cannot safely reenter the GPU. */
    const u32 first=at/o->page_size*o->page_size;
    const u32 end=(u32)(((u64)at+size+o->page_size-1)/o->page_size*o->page_size);
    return gcn_resident_memory_prepare_read(o->memory,first,end-first) && protect(o,true);
}
int wii_gx_output_present(WiiGx* g,u32 address,u32 width,u32 height,u32 stride) {
    Output* o=g->output;if(!o)return 0;const u32 at=address&0x1fffffff;GcnResidentCopy c;
    if(gcn_resident_memory_find(o->memory,at,&c) && c.destination.offset==at &&
       c.destination.row_bytes==width*2 && c.destination.rows==height && c.destination.stride==stride) {
        const int result=g->api->present_copy(c.token,width,height,stride);
        if(result>0)++o->resident_presents;
        if(result)return result;
    }
    return wii_gx_output_prepare(g,address,(height-1)*stride+width*2)?0:-1;
}
#else
bool wii_gx_output_open(WiiGx* g){(void)g;return false;}
bool wii_gx_output_close(WiiGx* g){(void)g;return true;}
int wii_gx_output_own(void* g,const GcnllrNativeGxCopy* c){(void)g;(void)c;return 0;}
int wii_gx_output_pending(void* g,u32 a,u32 n){(void)g;(void)a;(void)n;return 0;}
int wii_gx_output_prepare(void* g,u32 a,u32 n){(void)g;(void)a;(void)n;return 1;}
int wii_gx_output_present(WiiGx* g,u32 a,u32 w,u32 h,u32 s){(void)g;(void)a;(void)w;(void)h;(void)s;return 0;}
#endif
