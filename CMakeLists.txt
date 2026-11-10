cmake_minimum_required(VERSION 3.25)
project(wiillr VERSION 0.1.0 LANGUAGES C CXX)
include(CTest)
find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)

add_subdirectory(external/powerpc-recomp-core powerpc)
set(GC_WII_POWERPC_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/powerpc-recomp-core" CACHE PATH "Shared PowerPC core")
option(GCN_VULKAN "Build reference Vulkan renderer" OFF)
option(GCN_USE_CCACHE "Use ccache for reference core" OFF)
add_subdirectory(external/gc-wii-core machine-reference)
include(external/gc-wii-core/dsp_lle/native/CMakeLists.txt)
set(WIILLR_DSP "interpreter-development" CACHE STRING "Build-selected DSP engine")
set(WIILLR_DSP_CATALOG "" CACHE PATH "Private static DSP catalog")
gcnllr_add_native_dsp(wiillr_dsp "${WIILLR_DSP}" "${WIILLR_DSP_CATALOG}")

add_library(wii_boot STATIC src/boot.c src/dol.c src/machine.c src/ipc.c src/dsp.c
  external/gc-wii-core/src/pi.c external/gc-wii-core/src/mi.c external/gc-wii-core/src/vi.c
  external/gc-wii-core/src/ai.c external/gc-wii-core/src/exi.c
  external/gc-wii-core/src/exi_cpu.c external/gc-wii-core/src/rtc.c
  external/gc-wii-core/src/memcard.c external/gc-wii-core/src/di.c external/gc-wii-core/src/si.c)
target_compile_definitions(wii_boot PRIVATE GCNLLR_VI_DEVICE_ONLY=1)
target_include_directories(wii_boot PUBLIC include)
target_link_libraries(wii_boot PUBLIC gcn_runtime_core)
target_link_libraries(wii_boot PUBLIC wiillr_dsp)
target_link_libraries(wii_boot PRIVATE ipl_descramble_core)
add_executable(wiillr-discover src/discover.c)
target_link_libraries(wiillr-discover PRIVATE wii_boot)
add_executable(test_wii_dol tests/test_dol.c)
target_link_libraries(test_wii_dol PRIVATE wii_boot)
add_test(NAME wii_dol_launch COMMAND test_wii_dol)
add_executable(test_wii_devices tests/test_devices.c)
target_link_libraries(test_wii_devices PRIVATE wii_boot)
add_test(NAME wii_pi_ipc COMMAND test_wii_devices)
if(WIILLR_DSP STREQUAL "interpreter-development" OR WIILLR_DSP STREQUAL "aot")
  add_executable(test_wii_dsp_memory tests/test_dsp_memory.cpp)
  target_include_directories(test_wii_dsp_memory PRIVATE external/gc-wii-core/dsp_lle
    external/gc-wii-core/dsp_lle/native)
  target_link_libraries(test_wii_dsp_memory PRIVATE wiillr_dsp)
  add_test(NAME wii_dsp_banks COMMAND test_wii_dsp_memory)
endif()

add_test(NAME wii_input_formats COMMAND "${Python3_EXECUTABLE}" -m unittest discover
  -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p "test_*.py")
set_tests_properties(wii_input_formats PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
