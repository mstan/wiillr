/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MENU_PROFILE_H
#define WIILLR_MENU_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* SYSCONF keeps zero-based offsets plus a terminal offset. Parse the complete
 * bounded container before changing either value; unknown settings stay byte
 * exact. No guessed replacement, insertion, or existing-profile migration. */
static inline unsigned wii_menu_profile_word(const uint8_t* p){return (unsigned)p[0]<<8|p[1];}
static inline bool wii_menu_profile_fields(const uint8_t* p,size_t size,size_t* aspect,size_t* bias){
    if(!p || size!=16384 || memcmp(p,"SCv0",4) || memcmp(p+size-4,"SCed",4))return false;
    const unsigned count=wii_menu_profile_word(p+4);const size_t table=6+2*((size_t)count+1);
    if(table>size-4)return false;
    *aspect=*bias=0;
    for(unsigned i=0;i<count;i++){
        size_t at=wii_menu_profile_word(p+6+2*i),end=wii_menu_profile_word(p+8+2*i);
        if(at<table || at>=end || end>size-4)return false;
        const unsigned type=p[at]>>5,n=(p[at]&31)+1;++at;
        if(n>end-at)return false;
        const uint8_t* name=p+at;at+=n;size_t bytes=0;
        if(type==1){if(end-at<2)return false;bytes=wii_menu_profile_word(p+at)+1;at+=2;}
        else if(type==2){if(at==end)return false;bytes=(size_t)p[at++]+1;}
        else if(type>=3 && type<=7)bytes=type==3 || type==7?1:type==4?2:type==5?4:8;
        else return false;
        if(bytes!=end-at)return false;
        if(n==6 && !memcmp(name,"IPL.AR",6)){if(*aspect || type!=3)return false;*aspect=at;}
        if(n==6 && !memcmp(name,"IPL.CB",6)){if(*bias || type!=5)return false;*bias=at;}
    }
    return *aspect && *bias;
}
static inline bool wii_menu_profile_seed(uint8_t* p,size_t size){
    size_t aspect,bias;if(!wii_menu_profile_fields(p,size,&aspect,&bias))return false;
    p[aspect]=1;memset(p+bias,0,4);return true;
}
static inline bool wii_menu_profile_local_seconds(const uint8_t* p,size_t size,uint32_t rtc,uint32_t* local){
    size_t aspect,bias;if(!local || !wii_menu_profile_fields(p,size,&aspect,&bias))return false;
    const uint32_t cb=(uint32_t)p[bias]<<24|(uint32_t)p[bias+1]<<16|(uint32_t)p[bias+2]<<8|p[bias+3];
    *local=rtc+cb; /* Wii's 32-bit RTC plus persisted counter bias, including wrap. */
    return true;
}
#endif
