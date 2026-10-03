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
# gen/aems/aems.c: code inside the AEMS sound banks (tools/aems_lift.py), run from the heap.
file(GLOB GEN_SRCS CONFIGURE_DEPENDS ${ROOT}/gen/recomp/recomp_*.c ${ROOT}/gen/recomp/recomp_dispatch.c ${ROOT}/gen/aems/aems.c)
add_library(recomp_gen STATIC ${GEN_SRCS})
target_include_directories(recomp_gen PRIVATE ${ROOT}/runtime/include/recomp ${ROOT}/runtime/include)
target_compile_definitions(recomp_gen PRIVATE RECOMP_LOCAL_REGS)
option(RUGBY_TRACE "lifted bodies record entry in a per-thread ring (dumped on fatal)" ON)
if(RUGBY_TRACE)
  target_compile_definitions(recomp_gen PRIVATE RECOMP_TRACE)
endif()
target_compile_options(recomp_gen PRIVATE -w -fwrapv -fno-strict-aliasing -O1 -g0)

# ---- MojoShader: D3D8/9 shader bytecode -> GLSL (ES) for the D3D8 bridge (docs/PRIOR_ART.md) ----
# Only the parser and the GLSL profiles; its GL glue (mojoshader_opengl.c) is not used.
set(MOJO ${ROOT}/third_party/mojoshader)
add_library(mojoshader STATIC ${MOJO}/mojoshader.c ${MOJO}/mojoshader_common.c
            ${MOJO}/profiles/mojoshader_profile_glsl.c ${MOJO}/profiles/mojoshader_profile_common.c)
target_include_directories(mojoshader PUBLIC ${MOJO})
target_compile_definitions(mojoshader PUBLIC MOJOSHADER_NO_VERSION_INCLUDE
  PRIVATE MOJOSHADER_VERSION=-1 MOJOSHADER_CHANGESET="third_party"
  SUPPORT_PROFILE_D3D=0 SUPPORT_PROFILE_BYTECODE=0 SUPPORT_PROFILE_HLSL=0 SUPPORT_PROFILE_ARB1=0
  SUPPORT_PROFILE_ARB1_NV=0 SUPPORT_PROFILE_METAL=0 SUPPORT_PROFILE_SPIRV=0 SUPPORT_PROFILE_GLSPIRV=0)
target_compile_options(mojoshader PRIVATE -w)
if(EMSCRIPTEN)
  target_compile_options(mojoshader PRIVATE -pthread)
endif()

# ---- runtime core ------------------------------------------------------------------------
file(GLOB_RECURSE RT_ALL_SRCS CONFIGURE_DEPENDS ${ROOT}/runtime/*.c ${ROOT}/runtime/*.cpp ${ROOT}/platform/*.c ${ROOT}/platform/*.cpp)
# The import table lists which HLE(Name) handlers exist, so it is regenerated whenever any
# runtime/platform source changes.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT ${ROOT}/gen/runtime/imports.c
  COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/gen_imports.py
  DEPENDS ${ROOT}/tools/gen_imports.py ${ROOT}/analysis/pe_analysis.json ${RT_ALL_SRCS}
  WORKING_DIRECTORY ${ROOT} COMMENT "gen_imports")
# Guest D3D8 vtables, generated from d3d8to9's interface declarations (vtable order); slots bind
# the HLE_D3D handlers runtime/d3d8 defines, so it reruns when those sources change.
file(GLOB RT_D3D8_SRCS CONFIGURE_DEPENDS ${ROOT}/runtime/d3d8/*.cpp)
add_custom_command(OUTPUT ${ROOT}/gen/runtime/d3d8_vtables.c
  COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/gen_d3d8_bridges.py
  DEPENDS ${ROOT}/tools/gen_d3d8_bridges.py ${ROOT}/third_party/d3d8to9/source/d3d8.hpp ${RT_D3D8_SRCS}
  WORKING_DIRECTORY ${ROOT} COMMENT "gen_d3d8_bridges")
file(GLOB RT_CORE_SRCS CONFIGURE_DEPENDS ${ROOT}/runtime/core/*.c ${ROOT}/runtime/win32/*.c
     ${ROOT}/runtime/d3d8/*.c ${ROOT}/runtime/d3d8/*.cpp)
add_library(rt_core STATIC ${RT_CORE_SRCS} ${ROOT}/gen/runtime/imports.c ${ROOT}/gen/runtime/d3d8_vtables.c
            ${ROOT}/third_party/d3d9-webgl/d3d9.cpp)
target_include_directories(rt_core PUBLIC ${ROOT}/runtime/include/recomp ${ROOT}/runtime/include ${ROOT}/runtime/win32)
target_include_directories(rt_core PRIVATE ${ROOT}/runtime/d3d8 ${ROOT}/third_party/d3d9-webgl ${MOJO})
target_compile_definitions(rt_core PRIVATE MOJOSHADER_NO_VERSION_INCLUDE)
if(NOT EMSCRIPTEN)
  # d3d9-webgl on desktop GL 4.1 core: stand-ins for its emscripten/GLES3 headers (runtime/DESIGN.md)
  target_include_directories(rt_core PRIVATE ${ROOT}/platform/native/glshim)
endif()
target_compile_options(rt_core PRIVATE -Wall -Wno-unused-function -fwrapv -fno-strict-aliasing)
set_source_files_properties(${ROOT}/third_party/d3d9-webgl/d3d9.cpp PROPERTIES COMPILE_OPTIONS "-w")

# ---- platform + executable -----------------------------------------------------------------
if(EMSCRIPTEN)
  file(GLOB PLATFORM_SRCS CONFIGURE_DEPENDS ${ROOT}/platform/web/*.c ${ROOT}/platform/sdl/*.c)
else()
  file(GLOB PLATFORM_SRCS CONFIGURE_DEPENDS ${ROOT}/platform/native/*.c ${ROOT}/platform/sdl/*.c)
endif()
add_executable(rugby08 ${PLATFORM_SRCS})
target_link_libraries(rugby08 PRIVATE rt_core recomp_gen rt_core mojoshader m)
if(NOT EMSCRIPTEN)
  find_package(Threads REQUIRED)
  target_link_libraries(rugby08 PRIVATE Threads::Threads)
  find_package(SDL2 REQUIRED CONFIG)
  target_link_libraries(rugby08 PRIVATE SDL2::SDL2 "-framework OpenGL")
else()
  target_compile_options(rugby08 PRIVATE -sUSE_SDL=2)
endif()

if(EMSCRIPTEN)
  # build/web/{index.html,rugby08.js,rugby08.wasm}; the guest arena needs a large growable heap.
  set_target_properties(rugby08 PROPERTIES
    OUTPUT_NAME rugby08 SUFFIX .html
    RUNTIME_OUTPUT_DIRECTORY ${ROOT}/build/web)
  target_compile_options(rt_core PRIVATE -pthread)
  target_compile_options(recomp_gen PRIVATE -pthread)
  # -O0 at link skips the wasm-opt -O3 pass (>1h and >24 GB on the 91 MB module); revisit for release builds.
  target_link_options(rugby08 PRIVATE -O0 -pthread -sUSE_SDL=2 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=1gb -sMAXIMUM_MEMORY=2gb -sPTHREAD_POOL_SIZE=4 -sEXIT_RUNTIME=1
    --preload-file=${ROOT}/Rugby08_unpacked.exe@/Rugby08_unpacked.exe)
  add_custom_command(TARGET rugby08 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE_DIR:rugby08>/rugby08.html $<TARGET_FILE_DIR:rugby08>/index.html)
endif()
