# render/webgl/glsl/

The fixed-function shaders (GLSL ES 1.00 dialect; `program_ps.cpp`/`program_vs.cpp` derive GLSL ES 3.00
variants, `platform/native/gl_native.c` rewrites them for desktop GL). CMake embeds them as raw string
literals (`ffp_vert.inc`, `ffp_frag.inc` in the build tree).
