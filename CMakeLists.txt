cmake_minimum_required(VERSION 3.20)
project(rugby08 C CXX)
# Native (clang, macOS) and web (emcmake) builds of the statically recompiled game.
# Generated code: gen/recomp (tools/lift.py) and gen/runtime (tools/gen_imports.py), never committed.

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# ---- lifted game code ----------------------------------------------------------------
# -fwrapv -fno-strict-aliasing are required for correctness (runtime/DESIGN.md), as is the
# RECOMP_LOCAL_REGS model the difftest verifies.
file(GLOB GEN_SRCS CONFIGURE_DEPENDS ${ROOT}/gen/recomp/recomp_*.c ${ROOT}/gen/recomp/recomp_dispatch.c)
add_library(recomp_gen STATIC ${GEN_SRCS})
target_include_directories(recomp_gen PRIVATE ${ROOT}/runtime/include/recomp ${ROOT}/runtime/include)
target_compile_definitions(recomp_gen PRIVATE RECOMP_LOCAL_REGS)
option(RUGBY_TRACE "lifted bodies record entry in a per-thread ring (dumped on fatal)" ON)
if(RUGBY_TRACE)
  target_compile_definitions(recomp_gen PRIVATE RECOMP_TRACE)
endif()
target_compile_options(recomp_gen PRIVATE -w -fwrapv -fno-strict-aliasing -O1 -g0)

# ---- runtime core ------------------------------------------------------------------------
file(GLOB_RECURSE RT_ALL_SRCS CONFIGURE_DEPENDS ${ROOT}/runtime/*.c ${ROOT}/runtime/*.cpp ${ROOT}/platform/*.c ${ROOT}/platform/*.cpp)
# The import table lists which HLE(Name) handlers exist, so it is regenerated whenever any
# runtime/platform source changes.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT ${ROOT}/gen/runtime/imports.c
  COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/gen_imports.py
  DEPENDS ${ROOT}/tools/gen_imports.py ${ROOT}/analysis/pe_analysis.json ${RT_ALL_SRCS}
  WORKING_DIRECTORY ${ROOT} COMMENT "gen_imports")
file(GLOB RT_CORE_SRCS CONFIGURE_DEPENDS ${ROOT}/runtime/core/*.c ${ROOT}/runtime/win32/*.c)
add_library(rt_core STATIC ${RT_CORE_SRCS} ${ROOT}/gen/runtime/imports.c)
target_include_directories(rt_core PUBLIC ${ROOT}/runtime/include/recomp ${ROOT}/runtime/include ${ROOT}/runtime/win32)
target_compile_options(rt_core PRIVATE -Wall -Wno-unused-function -fwrapv -fno-strict-aliasing)

# ---- platform + executable -----------------------------------------------------------------
if(EMSCRIPTEN)
  file(GLOB PLATFORM_SRCS CONFIGURE_DEPENDS ${ROOT}/platform/web/*.c)
else()
  file(GLOB PLATFORM_SRCS CONFIGURE_DEPENDS ${ROOT}/platform/native/*.c)
endif()
add_executable(rugby08 ${PLATFORM_SRCS})
target_link_libraries(rugby08 PRIVATE rt_core recomp_gen rt_core m)
if(NOT EMSCRIPTEN)
  find_package(Threads REQUIRED)
  target_link_libraries(rugby08 PRIVATE Threads::Threads)
endif()
