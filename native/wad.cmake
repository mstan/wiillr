# SPDX-License-Identifier: MIT
# Call wiillr_add_wad_importer(<pinned-RecompCore-root>) once, then link wii_wad.
function(wiillr_add_wad_importer RECOMPCORE_ROOT)
    if(TARGET wii_wad)
        return()
    endif()
    set(_wad_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    set(_wad_mbed "${RECOMPCORE_ROOT}/Externals/mbedtls")
    foreach(_wad_file library/aes.c library/sha1.c library/platform_util.c include/mbedtls/aes.h)
        if(NOT EXISTS "${_wad_mbed}/${_wad_file}")
            message(FATAL_ERROR "WAD importer needs the pinned RecompCore mbedTLS sources: ${_wad_file}")
        endif()
    endforeach()
    add_library(wii_wad STATIC "${_wad_root}/native/wad.cpp"
        "${_wad_mbed}/library/aes.c" "${_wad_mbed}/library/sha1.c"
        "${_wad_mbed}/library/platform_util.c")
    target_compile_features(wii_wad PRIVATE cxx_std_20)
    target_include_directories(wii_wad PUBLIC "${_wad_root}/include"
        PRIVATE "${_wad_root}/native" "${_wad_mbed}/include")
    target_compile_definitions(wii_wad PRIVATE MBEDTLS_CONFIG_FILE="wad_crypto_config.h")
    set_target_properties(wii_wad PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()
