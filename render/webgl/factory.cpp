// factory.cpp -- IDirect3D9 (CreateDevice makes the GL context) and the exported entry points.
#include "internal.h"
#include "device.h"

// WebGL context
static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE g_webgl_context = 0;

class D3D9 : public IDirect3D9 {
  public:
    virtual HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
                                 D3DPRESENT_PARAMETERS *pPresentationParameters,
                                 IDirect3DDevice9 **ppReturnedDeviceInterface) override {
        if (!ppReturnedDeviceInterface) return -1;
        RQ_CALL(HRESULT, CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters,
                                      ppReturnedDeviceInterface));

        // Create WebGL context
        EmscriptenWebGLContextAttributes attrs;
        emscripten_webgl_init_context_attributes(&attrs);
        attrs.majorVersion = 2;
        attrs.minorVersion = 0;

        // D3D9 backbuffer is D3DFMT_X8R8G8B8 (no alpha channel)
        // Canvas alpha must be disabled to match, otherwise DXT3 texture alpha
        // leaks into framebuffer and browser compositing darkens the output
        attrs.alpha = EM_FALSE;
        // A frame can give the browser a turn before its Present (a party game waiting for the bytes another
        // player's game read back, net_external.c): the browser must not clear what is drawn so far
        attrs.preserveDrawingBuffer = EM_TRUE;

        g_webgl_context = emscripten_webgl_create_context(g_canvas_selector, &attrs);
        if (g_webgl_context <= 0) {
            printf("Failed to create WebGL context\n");
            return -1;
        }
        emscripten_webgl_make_context_current(g_webgl_context);

        // DXT textures need S3TC (every desktop GPU and browser the game runs on has it)
        emscripten_webgl_enable_extension(g_webgl_context, "WEBGL_compressed_texture_s3tc");

        // [WASM] Enable anisotropic filtering extension
        if (emscripten_webgl_enable_extension(g_webgl_context, "EXT_texture_filter_anisotropic")) {
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &g_max_anisotropy);
            if (g_max_anisotropy > 16.0f) g_max_anisotropy = 16.0f;
            printf("[D3D9] Anisotropic filtering enabled (max %.0fx)\n", g_max_anisotropy);
        } else {
            g_max_anisotropy = 1.0f;
            printf("[D3D9] Anisotropic filtering not available\n");
        }

        // Determine backbuffer resolution: use app's request, fall back to canvas size
        if (pPresentationParameters && pPresentationParameters->BackBufferWidth > 0 &&
            pPresentationParameters->BackBufferHeight > 0) {
            g_logical_width = pPresentationParameters->BackBufferWidth;
            g_logical_height = pPresentationParameters->BackBufferHeight;
        } else {
            int w, h;
            emscripten_get_canvas_element_size(g_canvas_selector, &w, &h);
            g_logical_width = w;
            g_logical_height = h;
        }

        // The window takes the back buffer's size, as a fullscreen mode switch does on Windows: the
        // canvas drawing buffer, and the window the game's mouse coordinates are in (a canvas resized
        // behind SDL's back left the pointer in the old desktop-sized window: the cursor went astray)
        plat_gl_set_size(g_logical_width, g_logical_height);

        *ppReturnedDeviceInterface = new D3D9Device();

        g_viewport.Width = g_logical_width;
        g_viewport.Height = g_logical_height;
        glViewport(0, 0, g_logical_width, g_logical_height);
        plat_render_start();

        return D3D_OK;
    }

    virtual ULONG AddRef() override { return 1; }

    virtual ULONG Release() override {
        delete this;
        return 0;
    }
};

// Entry point
extern "C" IDirect3D9 *WINAPI Direct3DCreate9(UINT SDKVersion) { return new D3D9(); }

extern "C" unsigned d3d9w_texture_gl(IDirect3DTexture9 *t) {
    RQ_CALL(unsigned, d3d9w_texture_gl(t));
    return t ? static_cast<D3D9Texture *>(t)->GetGLTexture() : 0;
}
