// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted for wiillr; arithmetic from NetKDRequest.cpp (see UPSTREAM.md).
#include "NWC24Identity.h"
#include <array>
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;using u64=uint64_t;using s32=int32_t;
namespace {
s32 NWC24MakeUserID(u64* nwc24_id, u32 hollywood_id, u16 id_ctr, u8 hardware_model,
                    u8 area_code)
{
  static constexpr std::array<u8, 8> table2{
      0x1, 0x5, 0x0, 0x4, 0x2, 0x3, 0x6, 0x7,
  };
  static constexpr std::array<u8, 16> table1{
      0x4, 0xB, 0x7, 0x9, 0xF, 0x1, 0xD, 0x3, 0xC, 0x2, 0x6, 0xE, 0x8, 0x0, 0xA, 0x5,
  };

  constexpr auto u64_get_byte = [](u64 value, u32 shift) -> u8 { return u8(value >> (shift * 8)); };

  constexpr auto u64_insert_byte = [](u64 value, u32 shift, u8 byte) -> u64 {
    const u64 mask = 0x00000000000000FFULL << (shift * 8);
    const u64 inst = u64{byte} << (shift * 8);
    return (value & ~mask) | inst;
  };

  u64 mix_id = (u64{area_code} << 50) | (u64(hardware_model) << 47) | (u64{hollywood_id} << 15) |
               (u64{id_ctr} << 10);
  const u64 mix_id_copy1 = mix_id;

  u32 ctr = 0;
  for (ctr = 0; ctr <= 42; ctr++)
  {
    u64 value = mix_id >> (52 - ctr);
    if ((value & 1) != 0)
    {
      value = 0x0000000000000635ULL << (42 - ctr);
      mix_id ^= value;
    }
  }

  mix_id = (mix_id_copy1 | (mix_id & 0xFFFFFFFFUL)) ^ 0x0000B3B3B3B3B3B3ULL;
  mix_id = (mix_id >> 10) | ((mix_id & 0x3FF) << (11 + 32));

  for (ctr = 0; ctr <= 5; ctr++)
  {
    const u8 ret = u64_get_byte(mix_id, ctr);
    const u8 foobar = u8((u32{table1[(ret >> 4) & 0xF]} << 4) | table1[ret & 0xF]);
    mix_id = u64_insert_byte(mix_id, ctr, foobar & 0xff);
  }

  const u64 mix_id_copy2 = mix_id;

  for (ctr = 0; ctr <= 5; ctr++)
  {
    const u8 ret = u64_get_byte(mix_id_copy2, ctr);
    mix_id = u64_insert_byte(mix_id, table2[ctr], ret);
  }

  mix_id &= 0x001FFFFFFFFFFFFFULL;
  mix_id = (mix_id << 1) | ((mix_id >> 52) & 1);

  mix_id ^= 0x00005E5E5E5E5E5EULL;
  mix_id &= 0x001FFFFFFFFFFFFFULL;

  *nwc24_id = mix_id;

  if (mix_id > 9999999999999999ULL)
    return -1;

  return 0;
}
}
int32_t wii_nwc24_make_id(uint64_t* id,uint32_t chip,uint16_t counter,uint8_t model,uint8_t area) {
  return NWC24MakeUserID(id,chip,counter,model,area);
}
