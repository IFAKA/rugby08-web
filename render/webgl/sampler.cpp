// sampler.cpp -- D3D sampler state -> GL texture parameters.
#include "internal.h"

void ApplySamplerStates(DWORD Sampler) {
if (Sampler >= 8) return;

// Address U/V: D3DTADDRESS_WRAP 1, MIRROR 2, CLAMP 3, BORDER 4, MIRRORONCE 5. GLES has no border
// colour (BORDER clamps to the edge texel) and no mirror-once (mirrors, then repeats the mirror).
auto gl_wrap = [](DWORD m) -> GLint {
    switch (m) {
        case 2: case 5: return GL_MIRRORED_REPEAT;
        case 3: case 4: return GL_CLAMP_TO_EDGE;
        default: return GL_REPEAT;
    }
};
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_wrap(g_sampler_states[Sampler][D3DSAMP_ADDRESSU]));
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_wrap(g_sampler_states[Sampler][D3DSAMP_ADDRESSV]));

// Mag Filter (mipmaps don't affect magnification)
GLint mag_filter = GL_LINEAR;
if (g_sampler_states[Sampler][D3DSAMP_MAGFILTER] == D3DTEXF_POINT) mag_filter = GL_NEAREST;
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);

// Min Filter: combine D3D9 MINFILTER + MIPFILTER into GL enum.
// D3D9 has separate MINFILTER and MIPFILTER states, but OpenGL combines
// them into a single GL_TEXTURE_MIN_FILTER value:
//   MIN=LINEAR  + MIP=LINEAR  → GL_LINEAR_MIPMAP_LINEAR   (trilinear)
//   MIN=LINEAR  + MIP=POINT   → GL_LINEAR_MIPMAP_NEAREST  (bilinear)
//   MIN=POINT   + MIP=LINEAR  → GL_NEAREST_MIPMAP_LINEAR
//   MIN=POINT   + MIP=POINT   → GL_NEAREST_MIPMAP_NEAREST
//   MIN=*       + MIP=NONE    → GL_LINEAR / GL_NEAREST    (no mip)
DWORD d3d_min = g_sampler_states[Sampler][D3DSAMP_MINFILTER];
DWORD d3d_mip = g_sampler_states[Sampler][D3DSAMP_MIPFILTER];

GLuint current_tex = g_texture_stages[Sampler];
bool has_mipmaps = (current_tex != 0) && g_mipmapped_textures.count(current_tex);

GLint min_filter;
if (has_mipmaps && d3d_mip != D3DTEXF_NONE) {
    if (d3d_min == D3DTEXF_POINT) {
        min_filter = (d3d_mip == D3DTEXF_LINEAR) ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
    } else {
        min_filter = (d3d_mip == D3DTEXF_LINEAR) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST;
    }
} else {
    min_filter = (d3d_min == D3DTEXF_POINT) ? GL_NEAREST : GL_LINEAR;
}
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);

// Anisotropic filtering: apply when mipmapping is active.
// This dramatically improves quality on surfaces at oblique angles (floors,
// walls receding into distance) by sampling higher mip levels along the
// less-compressed texture axis instead of using the worst-case mip level.
if (g_max_anisotropy > 1.0f && has_mipmaps && d3d_mip != D3DTEXF_NONE) {
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, g_max_anisotropy);
} else if (g_max_anisotropy > 1.0f) {
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f);
}
}
