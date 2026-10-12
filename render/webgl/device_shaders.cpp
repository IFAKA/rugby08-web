// device_shaders.cpp -- D3D9 shader entry points (D3D8 shaders go through d3d9w_prog*).
#include "internal.h"
#include "device.h"

HRESULT D3D9Device::GetVertexShader(IDirect3DVertexShader9** ppShader) {
    if (ppShader) *ppShader = nullptr;
    return D3D_OK;
}

HRESULT D3D9Device::GetPixelShader(IDirect3DPixelShader9** ppShader) {
    if (ppShader) *ppShader = nullptr;
    return D3D_OK;
}

HRESULT D3D9Device::CreateVertexShader(const DWORD* pFunction, IDirect3DVertexShader9** ppShader) {
    if (ppShader) *ppShader = nullptr;
    return D3D_OK;
}

HRESULT D3D9Device::CreatePixelShader(const DWORD* pFunction, IDirect3DPixelShader9** ppShader) {
    if (ppShader) *ppShader = nullptr;
    return D3D_OK;
}

HRESULT D3D9Device::SetPixelShaderConstantF(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) {
    return D3D_OK;
}

HRESULT D3D9Device::CreateVertexDeclaration(const void* pVertexElements, IDirect3DVertexDeclaration9** ppDecl) {
    printf("[D3D9 Wrapper WARNING] CreateVertexDeclaration called - shaders should be disabled!\n");
    if (ppDecl) *ppDecl = nullptr;
    return D3D_OK; // Return success to avoid crashes
}

HRESULT D3D9Device::SetVertexDeclaration(void* pDecl) {
    printf("[D3D9 Wrapper WARNING] SetVertexDeclaration called - shaders should be disabled!\n");
    return D3D_OK;
}

HRESULT D3D9Device::SetVertexShader(void* pShader) {
    // Debug: Only log warning once to reduce log spam during loading
    if (!s_vertex_shader_warning_shown) {
        printf("[D3D9 Wrapper WARNING] SetVertexShader called - shaders should be disabled! (further warnings suppressed)\n");
        s_vertex_shader_warning_shown = true;
    }
    return D3D_OK;
}

HRESULT D3D9Device::SetPixelShader(void* pShader) {
    // Debug: Only log warning once to reduce log spam during loading
    if (!s_pixel_shader_warning_shown) {
        printf("[D3D9 Wrapper WARNING] SetPixelShader called - shaders should be disabled! (further warnings suppressed)\n");
        s_pixel_shader_warning_shown = true;
    }
    return D3D_OK;
}

HRESULT D3D9Device::SetVertexShaderConstantF(UINT StartRegister, const float* pConstantData, UINT Vector4fCount) {
    // Debug: Only log warning once to reduce log spam during loading
    if (!s_vertex_shader_constant_warning_shown) {
        printf("[D3D9 Wrapper WARNING] SetVertexShaderConstantF called - shaders should be disabled! (further warnings suppressed)\n");
        s_vertex_shader_constant_warning_shown = true;
    }
    return D3D_OK;
}
