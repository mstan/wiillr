/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
/* Local WAD primitives from the pinned RecompCore; no TLS/networking. */
#define MBEDTLS_AES_C
#define MBEDTLS_AES_ROM_TABLES
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_SHA1_C
