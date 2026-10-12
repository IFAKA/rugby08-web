// device.h -- D3D9Device: IDirect3DDevice9 on GL. Methods are defined in device*.cpp, draw_*.cpp.
#pragma once
#include "resources.h"

class D3D9Device : public IDirect3DDevice9 {
private:
    D3DMATRIX m_world;
    D3DMATRIX m_world1;                  // D3DTS_WORLDMATRIX(1): vertex blending
    D3DMATRIX m_view;
    D3DMATRIX m_projection;
    GLuint m_current_index_buffer;
    D3DFORMAT m_current_ib_format;
    GLuint m_current_vertex_buffer;
    UINT m_current_stride;
    
    D3D9Surface* m_backbuffer_surface;
    D3D9Surface* m_current_rt;
    D3D9Surface* m_current_ds;
    
    // Clip Planes
    float m_clip_planes[1][4]; // Support 1 clip plane
    bool m_clip_plane_enabled = false;

    // FBO rendering state (for D3D9/OpenGL Y-convention fix)
    bool m_fbo_rendering = false;

    // State Tracking
    GLenum m_src_blend = GL_ONE;
    GLenum m_dst_blend = GL_ZERO;
    DWORD m_alpha_ref = 0;
    bool m_alpha_test_enabled = false;
    D3DCOLOR m_texture_factor;

    // Fog state
    bool m_fog_enabled = false;
    float m_fog_start = 0.0f;
    float m_fog_end = 1.0f;
    D3DCOLOR m_fog_color = 0xFFFFFFFF;

    // Debug: Flags to suppress repetitive shader warnings (performance optimization)
    static bool s_pixel_shader_warning_shown;
    static bool s_vertex_shader_warning_shown;
    static bool s_vertex_shader_constant_warning_shown;
    RECT m_scissor_rect = {};

public:
    


    
    // Render target and depth stencil methods
    
    
    
    
    // Shader methods
    
    
    
    
    
    // Drawing methods
    
    // Query and utility methods
    
    
    
    
    
    

    
    
    


    
    
    // d3d9w_trace_draws: one line per draw with the state that decides what it looks like

    
    // D3D9Device methods continued...
    
    DWORD m_render_states[256];







    // FFP APIs Implementation


    
    


    
    
    
    
    
    
    
    // Shader stubs (log warnings if called)
    
    
    
    










    D3D9Device();
    virtual ~D3D9Device();
    virtual ULONG AddRef() override;
    virtual ULONG Release() override;
    virtual HRESULT GetRenderTarget(DWORD Index, IDirect3DSurface9** ppRenderTarget) override;
    virtual HRESULT SetRenderTarget(DWORD Index, IDirect3DSurface9* pRenderTarget) override;
    virtual HRESULT GetDepthStencilSurface(IDirect3DSurface9** ppZStencilSurface) override;
    virtual HRESULT SetDepthStencilSurface(IDirect3DSurface9* pZStencilSurface) override;
    virtual HRESULT GetVertexShader(IDirect3DVertexShader9** ppShader) override;
    virtual HRESULT GetPixelShader(IDirect3DPixelShader9** ppShader) override;
    virtual HRESULT CreateVertexShader(const DWORD* pFunction, IDirect3DVertexShader9** ppShader) override;
    virtual HRESULT CreatePixelShader(const DWORD* pFunction, IDirect3DPixelShader9** ppShader) override;
    virtual HRESULT SetPixelShaderConstantF(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) override;
    virtual HRESULT DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) override;
    virtual HRESULT CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery) override;
    virtual HRESULT StretchRect(IDirect3DSurface9* pSourceSurface, const void* pSourceRect,
                               IDirect3DSurface9* pDestSurface, const void* pDestRect,
                               D3DTEXTUREFILTERTYPE Filter) override;
    virtual HRESULT Clear(DWORD Count, const void* pRects, DWORD Flags,
                         D3DCOLOR Color, FLOAT Z, DWORD Stencil) override;
    virtual HRESULT BeginScene() override;
    virtual HRESULT EndScene() override;
    virtual HRESULT Present(const void* pSourceRect, const void* pDestRect,
                           HWND hDestWindowOverride, const void* pDirtyRegion) override;
    virtual HRESULT SetFVF(DWORD FVF) override;
    virtual HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType,
                                   UINT PrimitiveCount,
                                   const void* pVertexStreamZeroData,
                                   UINT VertexStreamZeroStride) override;
    virtual HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex,
                                           UINT NumVertices, UINT PrimitiveCount,
                                           const void* pIndexData, D3DFORMAT IndexDataFormat,
                                           const void* pVertexStreamZeroData,
                                           UINT VertexStreamZeroStride) override;
    virtual HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels,
                                  DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                  IDirect3DTexture9** ppTexture, void* pSharedHandle) override;
    virtual HRESULT SetTexture(DWORD Stage, IDirect3DTexture9* pTexture) override;
    virtual HRESULT CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format,
                                     D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer,
                                     void* pSharedHandle) override;
    virtual HRESULT CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF,
                                      D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer,
                                      void* pSharedHandle) override;
    virtual HRESULT SetIndices(IDirect3DIndexBuffer9* pIndexData) override;
    virtual HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9* pStreamData,
                                   UINT OffsetInBytes, UINT Stride) override;
    void trace_draw(const char* path, unsigned count);
    virtual HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) override;
    virtual HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override;
    virtual HRESULT SetScissorRect(const RECT* pRect) override;
    virtual HRESULT GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue) override;
    virtual HRESULT CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format,
                                             D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality,
                                             BOOL Discard, IDirect3DSurface9** ppSurface, void* pSharedHandle) override;
    virtual HRESULT SetClipPlane(DWORD Index, const float* pPlane) override;
    virtual HRESULT GetClipPlane(DWORD Index, float* pPlane) override;
    virtual HRESULT GetDeviceCaps(D3DCAPS9* pCaps) override;
    virtual UINT GetAvailableTextureMem() override;
    virtual HRESULT ValidateDevice(DWORD* pNumPasses) override;
    virtual HRESULT SetLight(DWORD Index, const D3DLIGHT9* pLight) override;
    virtual HRESULT LightEnable(DWORD Index, BOOL Enable) override;
    virtual HRESULT GetLightEnable(DWORD Index, BOOL* pEnable) override;
    virtual HRESULT GetLight(DWORD Index, D3DLIGHT9* pLight) override;
    virtual HRESULT SetMaterial(const D3DMATERIAL9* pMaterial) override;
    virtual HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override;
    virtual HRESULT SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) override;
    virtual HRESULT GetViewport(D3DVIEWPORT9* pViewport) override;
    virtual HRESULT SetViewport(const D3DVIEWPORT9* pViewport) override;
    virtual HRESULT GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) override;
    virtual HRESULT CreateVertexDeclaration(const void* pVertexElements, IDirect3DVertexDeclaration9** ppDecl) override;
    virtual HRESULT SetVertexDeclaration(void* pDecl) override;
    virtual HRESULT SetVertexShader(void* pShader) override;
    virtual HRESULT SetPixelShader(void* pShader) override;
    virtual HRESULT SetVertexShaderConstantF(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) override;
    virtual HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType,
                                        INT BaseVertexIndex,
                                        UINT MinVertexIndex,
                                        UINT NumVertices,
                                        UINT startIndex,
                                        UINT primCount) override;
    virtual HRESULT CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool, IDirect3DSurface9** ppSurface, void* pSharedHandle) override;
    virtual HRESULT GetRenderTargetData(IDirect3DSurface9* pRenderTarget, IDirect3DSurface9* pDestSurface) override;
    virtual HRESULT GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9* pDestSurface) override;
    virtual HRESULT TestCooperativeLevel() override;
    virtual HRESULT Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) override;
    virtual void SetGammaRamp(UINT iSwapChain, DWORD Flags, const D3DGAMMARAMP* pRamp) override;
    virtual void GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP* pRamp) override;
    virtual HRESULT GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE* pMode) override;
    virtual HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9** ppSB) override;
};
