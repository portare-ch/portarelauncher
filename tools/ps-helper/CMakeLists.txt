cmake_minimum_required(VERSION 3.10)
project(portare_ps_chd_id C)
# Title lookup reads only a handful of hunks. Keep the map sparse and create
# codec buffers on demand instead of preparing the whole disc for playback.
set(CHDR_LOWRAM_TARGET ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CHDR_WANT_TESTS OFF CACHE BOOL "" FORCE)
# Optimize across the statically linked codecs, leaving debug/sanitizer builds
# alone. Toolchains without IPO support still get a normal size-focused build.
include(CheckIPOSupported)
check_ipo_supported(RESULT PS_ID_IPO_SUPPORTED LANGUAGES C)
if(PS_ID_IPO_SUPPORTED AND NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
endif()
add_subdirectory(../../third_party/libchdr libchdr)
add_executable(ps-chd-id ../ps-chd-id.c ../../src/ps_iso.c ../../src/ps_serial.c ../../src/ps_sfo.c)
target_include_directories(ps-chd-id PRIVATE ../../src ../../third_party/libchdr/include)
target_compile_definitions(ps-chd-id PRIVATE _GNU_SOURCE LOWRAM_TARGET=1)
option(PS_ID_SANITIZED "Disable address-space limit for ASan's virtual mapping" OFF)
if(PS_ID_SANITIZED)
  target_compile_definitions(ps-chd-id PRIVATE PS_ID_SANITIZED)
endif()
target_compile_options(ps-chd-id PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps-chd-id PRIVATE chdr-static m)
