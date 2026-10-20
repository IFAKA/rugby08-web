// device.h -- D3D9Device: IDirect3DDevice9 on GL. One file per group of methods (named below).
#pragma once
#include "resources.h"

// draw_setup.cpp: byte offsets of a vertex's FVF components
struct FvfLayout {
    bool xyz, rhw, normal, diffuse;
    int tex_count;
    GLsizei stride;
    size_t pos, nrm, col, tex, tex1;
};
enum PrimSet { PRIM_ALL, PRIM_TRIANGLES, PRIM_INDEXED_UP };   // each draw path's primitive table
enum Lightmap { LM_OFF, LM_BIND, LM_BIND_SAMPLED };           // how a draw binds stage 1
FvfLayout fvf_layout(DWORD fvf, bool legacy);
bool gl_primitive(D3DPRIMITIVETYPE t, UINT prims, PrimSet set, GLenum *mode, UINT *count);
const void *up_vertices(const void *data, size_t size, UINT n, GLsizei stride, const FvfLayout &l);
void up_upload(GLenum target, size_t *capacity, size_t size, const void *data);

class D3D9Device : public IDirect3DDevice9 {
  private:
    D3DMATRIX m_world;
    D3DMATRIX m_world1;   // D3DTS_WORLDMATRIX(1): vertex blending
    D3DMATRIX m_view;
    D3DMATRIX m_projection;
    GLuint m_current_index_buffer;
    D3DFORMAT m_current_ib_format;
    GLuint m_current_vertex_buffer;
    UINT m_current_stride;

    D3D9Surface *m_backbuffer_surface;
    D3D9Surface *m_current_rt;
    D3D9Surface *m_current_ds;

    float m_clip_planes[1][4];   // one user clip plane, applied in the fragment shader
    bool m_clip_plane_enabled = false;
    bool m_fbo_rendering = false;   // drawing into a texture: the shaders flip Y (D3D vs GL row order)

    GLenum m_src_blend = GL_ONE;
    GLenum m_dst_blend = GL_ZERO;
    DWORD m_alpha_ref = 0;
    bool m_alpha_test_enabled = false;
    D3DCOLOR m_texture_factor;

    bool m_fog_enabled = false;
    float m_fog_start = 0.0f;
    float m_fog_end = 1.0f;
    D3DCOLOR m_fog_color = 0xFFFFFFFF;

    // draw_setup.cpp
    void draw_begin(const char *path, unsigned count);
    void viewport_size(float *w, float *h) const;
    void target_size(float *w, float *h) const;
    void draw_uniforms(float rhw_w, float rhw_h);
    void draw_attribs(const FvfLayout &l, GLsizei stride, size_t base, bool tex1);
    void draw_textures(int tex_count, bool unbind, Lightmap lightmap);

    // device_state.cpp
    bool set_output_state(D3DRENDERSTATETYPE State, DWORD Value);
    void set_test_state(D3DRENDERSTATETYPE State, DWORD Value);

  public:
    DWORD m_render_states[256];

    // device.cpp
    D3D9Device();
    virtual ~D3D9Device();
    virtual ULONG AddRef() override;
    virtual ULONG Release() override;
    virtual HRESULT BeginScene() override;
    virtual HRESULT EndScene() override;
    virtual HRESULT Present(const void *pSourceRect, const void *pDestRect, HWND hDestWindowOverride,
                            const void *pDirtyRegion) override;
    virtual HRESULT Clear(DWORD Count, const void *pRects, DWORD Flags, D3DCOLOR Color, FLOAT Z,
                          DWORD Stencil) override;
    virtual HRESULT Reset(D3DPRESENT_PARAMETERS *pPresentationParameters) override;

    // device_targets.cpp
    virtual HRESULT GetRenderTarget(DWORD Index, IDirect3DSurface9 **ppRenderTarget) override;
    virtual HRESULT SetRenderTarget(DWORD Index, IDirect3DSurface9 *pRenderTarget) override;
    virtual HRESULT SetDepthStencilSurface(IDirect3DSurface9 *pZStencilSurface) override;
    virtual HRESULT CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format,
                                              D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality, BOOL Discard,
                                              IDirect3DSurface9 **ppSurface, void *pSharedHandle) override;
    virtual HRESULT SetViewport(const D3DVIEWPORT9 *pViewport) override;

    // device_resources.cpp
    virtual HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                  IDirect3DTexture9 **ppTexture, void *pSharedHandle) override;
    virtual HRESULT SetTexture(DWORD Stage, IDirect3DTexture9 *pTexture) override;
    virtual HRESULT CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                      IDirect3DIndexBuffer9 **ppIndexBuffer, void *pSharedHandle) override;
    virtual HRESULT CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
                                       IDirect3DVertexBuffer9 **ppVertexBuffer, void *pSharedHandle) override;
    virtual HRESULT SetIndices(IDirect3DIndexBuffer9 *pIndexData) override;
    virtual HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9 *pStreamData, UINT OffsetInBytes,
                                    UINT Stride) override;
    virtual HRESULT SetFVF(DWORD FVF) override;

    // device_state.cpp
    virtual HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override;
    virtual HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMatrix) override;
    virtual HRESULT SetClipPlane(DWORD Index, const float *pPlane) override;
    virtual HRESULT SetLight(DWORD Index, const D3DLIGHT9 *pLight) override;
    virtual HRESULT LightEnable(DWORD Index, BOOL Enable) override;
    virtual HRESULT SetMaterial(const D3DMATERIAL9 *pMaterial) override;
    virtual HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override;
    virtual HRESULT SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) override;

    // draw.cpp
    virtual HRESULT DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) override;
    virtual HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex,
                                         UINT NumVertices, UINT startIndex, UINT primCount) override;
    virtual HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount,
                                    const void *pVertexStreamZeroData, UINT VertexStreamZeroStride) override;
    virtual HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices,
                                           UINT PrimitiveCount, const void *pIndexData, D3DFORMAT IndexDataFormat,
                                           const void *pVertexStreamZeroData, UINT VertexStreamZeroStride) override;

    // interp.cpp: a draw or Clear recorded instead of run (fps.interp)
    bool rec_draw(unsigned op, unsigned a, unsigned b, std::function<void(D3D9Device *)> f);

    // trace.cpp
    void trace_draw(const char *path, unsigned count);
    void trace_blend(GLsizei stride);
};
