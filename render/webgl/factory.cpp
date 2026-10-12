// factory.cpp -- IDirect3D9 (CreateDevice makes the GL context) and the exported entry points.
#include "internal.h"
#include "device.h"

// WebGL context
static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE g_webgl_context = 0;

class D3D9 : public IDirect3D9 {
public:
    virtual HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType,
                                HWND hFocusWindow, DWORD BehaviorFlags,
                                D3DPRESENT_PARAMETERS* pPresentationParameters,
                                IDirect3DDevice9** ppReturnedDeviceInterface) override {
        if (!ppReturnedDeviceInterface) return -1;
        
        // Create WebGL context
        EmscriptenWebGLContextAttributes attrs;
        emscripten_webgl_init_context_attributes(&attrs);
        attrs.majorVersion = 2;
        attrs.minorVersion = 0;
        
        // D3D9 backbuffer is D3DFMT_X8R8G8B8 (no alpha channel)
        // Canvas alpha must be disabled to match, otherwise DXT3 texture alpha
        // leaks into framebuffer and browser compositing darkens the output
        attrs.alpha = EM_FALSE;
        
        g_webgl_context = emscripten_webgl_create_context(g_canvas_selector, &attrs);
        if (g_webgl_context <= 0) {
            printf("Failed to create WebGL context\n");
            return -1;
        }
        emscripten_webgl_make_context_current(g_webgl_context);

        // [WASM] Enable S3TC extensions for compressed textures
        if (emscripten_webgl_enable_extension(g_webgl_context, "WEBGL_compressed_texture_s3tc")) {
        } else {
        }

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
        if (pPresentationParameters &&
            pPresentationParameters->BackBufferWidth > 0 &&
            pPresentationParameters->BackBufferHeight > 0) {
            g_logical_width = pPresentationParameters->BackBufferWidth;
            g_logical_height = pPresentationParameters->BackBufferHeight;
        } else {
            int w, h;
            emscripten_get_canvas_element_size(g_canvas_selector, &w, &h);
            g_logical_width = w;
            g_logical_height = h;
        }

        // Set canvas drawingBuffer to match backbuffer size (D3D9-compliant: fixed at CreateDevice)
        emscripten_set_canvas_element_size(g_canvas_selector, g_logical_width, g_logical_height);

        *ppReturnedDeviceInterface = new D3D9Device();

        g_viewport.Width = g_logical_width;
        g_viewport.Height = g_logical_height;
        glViewport(0, 0, g_logical_width, g_logical_height);

        return D3D_OK;
    }

    // Extended overload: specify which canvas element to render to.
    // Falls back to the standard CreateDevice with g_canvas_selector set.
    HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType,
                         HWND hFocusWindow, DWORD BehaviorFlags,
                         D3DPRESENT_PARAMETERS* pPresentationParameters,
                         IDirect3DDevice9** ppReturnedDeviceInterface,
                         const char* canvasSelector) {
        if (canvasSelector && canvasSelector[0]) {
            g_canvas_selector = canvasSelector;
        }
        return CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags,
                            pPresentationParameters, ppReturnedDeviceInterface);
    }

    virtual ULONG AddRef() override {
        return 1; // Stub: no ref counting for D3D9 interface
    }

    virtual ULONG Release() override {
        delete this;
        return 0;
    }
    
    virtual UINT GetAdapterCount() override {
        return 1; // Single adapter for WebGL
    }

    virtual HRESULT GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier) override {
        if (!pIdentifier) return D3DERR_INVALIDCALL;
        memset(pIdentifier, 0, sizeof(D3DADAPTER_IDENTIFIER9));
        strncpy(pIdentifier->Driver, "emscripten_webgl", MAX_DEVICE_IDENTIFIER_STRING);
        strncpy(pIdentifier->Description, "Emscripten WebGL Wrapper", MAX_DEVICE_IDENTIFIER_STRING);
        return D3D_OK;
    }



    virtual UINT GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) override {
        return 1;
    }

    virtual HRESULT EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode) override {
        if (!pMode) return D3DERR_INVALIDCALL;
        if (Mode >= 1) return D3DERR_INVALIDCALL;

        int w, h;
        emscripten_get_canvas_element_size(g_canvas_selector, &w, &h);
        pMode->Width = w;
        pMode->Height = h;
        pMode->RefreshRate = 60;
        pMode->Format = Format;
        return D3D_OK;
    }

    virtual HRESULT GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode) override {
        if (!pMode) return D3DERR_INVALIDCALL;
        int w, h;
        emscripten_get_canvas_element_size(g_canvas_selector, &w, &h);
        pMode->Width = w;
        pMode->Height = h;
        pMode->RefreshRate = 60;
        pMode->Format = D3DFMT_X8R8G8B8;
        return D3D_OK;
    }

    virtual HRESULT CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) override {
        return D3D_OK;
    }

    virtual HRESULT CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) override {
        return D3D_OK;
    }


    virtual HRESULT GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps) override {
        // Shared implementation "lie"
        if (!pCaps) return -1;
        memset(pCaps, 0, sizeof(D3DCAPS9));
#ifdef __EMSCRIPTEN__
        pCaps->VertexShaderVersion = 0;
        pCaps->PixelShaderVersion = 0;
#else
        pCaps->VertexShaderVersion = D3DVS_VERSION(2, 0);
        pCaps->PixelShaderVersion = D3DPS_VERSION(2, 0);
#endif
        pCaps->DevCaps = D3DDEVCAPS_HWTRANSFORMANDLIGHT;
        pCaps->MaxPrimitiveCount = 0xFFFFFFFF;
        pCaps->NumSimultaneousRTs = 1;
        pCaps->MaxStreams = 16;
        pCaps->MaxUserClipPlanes = 1; // Emulated via fragment shader discard
        pCaps->RasterCaps = D3DPRASTERCAPS_WFOG | D3DPRASTERCAPS_ZTEST;
        pCaps->MaxVertexShaderConst = 0;
        return D3D_OK;
    }
};

// Entry point
extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion) {
    return new D3D9();
}

extern "C" void D3D9_RegisterTextureName(IDirect3DTexture9*, const char*) {}

// Accessor functions for external modules (e.g. text rendering bridge)
extern "C" {
    void D3D9_GetTextureInfo(IDirect3DTexture9* pTex, unsigned int* gl_id, unsigned int* w, unsigned int* h) {
        if (!pTex) return;
        D3D9Texture* tex = (D3D9Texture*)pTex;
        if (gl_id) *gl_id = tex->m_texture;
        if (w) *w = tex->m_width;
        if (h) *h = tex->m_height;
    }

    void D3D9_InvalidateTextureCache() {
        g_last_bound_texture_id = 0xFFFFFFFF;
        g_last_bound_lightmap_id = 0xFFFFFFFF;
        g_cached_texture = 0xFFFFFFFF;
    }
}

extern "C" unsigned d3d9w_texture_gl(IDirect3DTexture9 *t) { return t ? static_cast<D3D9Texture *>(t)->GetGLTexture() : 0; }
