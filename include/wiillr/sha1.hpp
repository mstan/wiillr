/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <array>
#include <bit>
#include <cstdint>
namespace wiillr {
/* Raw SHA-1 compression, shared by installed-content verification and the
 * Hollywood engine. The caller owns initialization and message padding. */
inline void sha1_compress(std::array<uint32_t,5>& h,const uint8_t* block) {
    uint32_t w[80];
    for(unsigned n=0;n<16;++n) {
        const auto* p=block+4*n;w[n]=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
    }
    for(unsigned n=16;n<80;++n)w[n]=std::rotl(w[n-3]^w[n-8]^w[n-14]^w[n-16],1);
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
    for(unsigned n=0;n<80;++n) {
        const uint32_t f=n<20?(b&c)|(~b&d):n<40?b^c^d:n<60?(b&c)|(b&d)|(c&d):b^c^d;
        const uint32_t k=n<20?0x5a827999:n<40?0x6ed9eba1:n<60?0x8f1bbcdc:0xca62c1d6;
        const uint32_t t=std::rotl(a,5)+f+e+k+w[n];e=d;d=c;c=std::rotl(b,30);b=a;a=t;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;
}
}
