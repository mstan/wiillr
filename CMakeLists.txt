cmake_minimum_required(VERSION 3.25)
project(wiillr VERSION 0.1.0 LANGUAGES C CXX)
include(CTest)
find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)

add_subdirectory(external/powerpc-recomp-core powerpc)
set(GC_WII_POWERPC_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/powerpc-recomp-core" CACHE PATH "Shared PowerPC core")
option(GCN_VULKAN "Build reference Vulkan renderer" OFF)
option(GCN_USE_CCACHE "Use ccache for reference core" OFF)
add_subdirectory(external/gc-wii-core machine-reference)

add_test(NAME wii_input_formats COMMAND "${Python3_EXECUTABLE}" -m unittest discover
  -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p "test_*.py")
set_tests_properties(wii_input_formats PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
