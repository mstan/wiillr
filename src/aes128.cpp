/* SPDX-License-Identifier: MIT */
#include "wiillr/aes128.hpp"
#include <cstring>
/* This translation unit owns the upstream macros and context ABI. */
#define CBC 0
#define CTR 0
#define ECB 1
extern "C" {
#include "aes.h"
}
namespace wiillr {
static_assert(AES_KEYLEN==16 && AES_keyExpSize==176 && sizeof(AES_ctx)==176);
Aes128Key aes128_expand(const std::array<uint8_t,16>& key) {
    AES_ctx context;AES_init_ctx(&context,key.data());
    Aes128Key result;std::memcpy(result.rounds.data(),context.RoundKey,result.rounds.size());return result;
}
void aes128_encrypt(const Aes128Key& key,std::array<uint8_t,16>& block) {
    AES_ctx context;std::memcpy(context.RoundKey,key.rounds.data(),key.rounds.size());
    AES_ECB_encrypt(&context,block.data());
}
void aes128_decrypt(const Aes128Key& key,std::array<uint8_t,16>& block) {
    AES_ctx context;std::memcpy(context.RoundKey,key.rounds.data(),key.rounds.size());
    AES_ECB_decrypt(&context,block.data());
}
}
