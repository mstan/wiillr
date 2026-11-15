/* SPDX-License-Identifier: MIT */
#pragma once
#include <array>
#include <cstdint>
namespace wiillr {
/* Portable AES-128 block interface. Upstream configuration and C ABI remain
 * private to aes128.cpp; public callers never depend on AES_ctx's layout. */
struct Aes128Key {std::array<uint8_t,176> rounds;};
Aes128Key aes128_expand(const std::array<uint8_t,16>& key);
void aes128_encrypt(const Aes128Key&,std::array<uint8_t,16>& block);
void aes128_decrypt(const Aes128Key&,std::array<uint8_t,16>& block);
}
