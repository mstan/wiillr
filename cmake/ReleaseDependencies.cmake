# SPDX-License-Identifier: MIT
# Reuse the exact public dependency versions from the pinned Aurora graph.
# Platform packages win; source fallback is local to the build tree.
include(FetchContent)
find_package(fmt 12.1.0 EXACT CONFIG QUIET)
if(NOT TARGET fmt::fmt)
  FetchContent_Declare(fmt
    URL https://github.com/fmtlib/fmt/archive/refs/tags/12.1.0.tar.gz
    URL_HASH SHA256=ea7de4299689e12b6dddd392f9896f08fb0777ac7168897a244a6d6085043fea
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
  set(FMT_TEST OFF CACHE BOOL "Build fmt tests")
  FetchContent_MakeAvailable(fmt)
endif()
find_library(WIILLR_RELEASE_XXHASH NAMES xxhash)
if(NOT WIILLR_RELEASE_XXHASH)
  FetchContent_Declare(xxhash
    URL https://github.com/Cyan4973/xxHash/archive/refs/tags/v0.8.3.tar.gz
    URL_HASH SHA256=aae608dfe8213dfd05d909a57718ef82f30722c392344583d3f39050c7f29a80
    SOURCE_SUBDIR cmake_unofficial
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
  set(XXHASH_BUILD_XXHSUM OFF CACHE BOOL "Build xxhsum")
  set(XXHASH_BUILD_ENABLE_INLINE_API OFF CACHE BOOL "Expose inline xxHash internals")
  FetchContent_MakeAvailable(xxhash)
endif()
