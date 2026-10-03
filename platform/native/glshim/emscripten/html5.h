/* Native stand-in for <emscripten/html5.h>: d3d9-webgl's WebGL context calls map to the SDL GL
 * context on the game window (platform/sdl/sdl_plat.c). There is one context and one "canvas". */
#pragma once
#include <string.h>
#include "plat.h"

typedef int EMSCRIPTEN_WEBGL_CONTEXT_HANDLE;
typedef int EM_BOOL;
#define EM_TRUE 1
#define EM_FALSE 0
typedef struct { EM_BOOL alpha, depth, stencil, antialias, premultipliedAlpha, preserveDrawingBuffer;
                 int powerPreference; EM_BOOL failIfMajorPerformanceCaveat; int majorVersion, minorVersion;
                 EM_BOOL enableExtensionsByDefault, explicitSwapControl, renderViaOffscreenBackBuffer, proxyContextToMainThread;
} EmscriptenWebGLContextAttributes;

static inline void emscripten_webgl_init_context_attributes(EmscriptenWebGLContextAttributes *a) { memset(a, 0, sizeof *a); }
static inline EMSCRIPTEN_WEBGL_CONTEXT_HANDLE emscripten_webgl_create_context(const char *target, const EmscriptenWebGLContextAttributes *a) {
    (void)target; (void)a; return plat_gl_create() == 0 ? 1 : 0;
}
static inline int emscripten_webgl_make_context_current(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE h) { (void)h; return 0; }
/* Desktop GL 4.1 core has S3TC and anisotropic filtering as extensions on every Mac GPU. */
static inline EM_BOOL emscripten_webgl_enable_extension(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE h, const char *ext) { (void)h; (void)ext; return EM_TRUE; }
static inline int emscripten_get_canvas_element_size(const char *target, int *w, int *h) { (void)target; plat_gl_size(w, h); return 0; }
static inline int emscripten_set_canvas_element_size(const char *target, int w, int h) { (void)target; plat_gl_set_size(w, h); return 0; }
