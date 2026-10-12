// sampler.cpp -- D3D sampler state -> GL texture parameters of the texture bound on the active unit.
#include "internal.h"

// D3DTADDRESS_WRAP 1, MIRROR 2, CLAMP 3, BORDER 4, MIRRORONCE 5. GLES has no border colour (BORDER
// clamps to the edge texel) and no mirror-once (it mirrors, then repeats the mirror).
static GLint gl_wrap(DWORD m) {
    switch (m) {
    case 2:
    case 5: return GL_MIRRORED_REPEAT;
    case 3:
    case 4: return GL_CLAMP_TO_EDGE;
    default: return GL_REPEAT;
    }
}

// Sampler st's address modes and filters. D3D has separate MINFILTER and MIPFILTER, GL one MIN_FILTER:
// MIN x MIP (POINT/LINEAR) -> GL_{NEAREST,LINEAR}_MIPMAP_{NEAREST,LINEAR}; MIP NONE, or a texture
// without a mip chain, -> GL_NEAREST/GL_LINEAR. aniso: anisotropic filtering while mipmapping (it
// samples sharper mips along surfaces at oblique angles: the pitch, stands).
void apply_sampler_states(DWORD st, bool aniso) {
    if (st >= 8) return;
    const DWORD *s = g_sampler_states[st];
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_wrap(s[D3DSAMP_ADDRESSU]));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_wrap(s[D3DSAMP_ADDRESSV]));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    s[D3DSAMP_MAGFILTER] == D3DTEXF_POINT ? GL_NEAREST : GL_LINEAR);

    GLuint tex = g_texture_stages[st];
    bool mips = tex != 0 && g_mipmapped_textures.count(tex) && s[D3DSAMP_MIPFILTER] != D3DTEXF_NONE;
    bool point = s[D3DSAMP_MINFILTER] == D3DTEXF_POINT, mip_linear = s[D3DSAMP_MIPFILTER] == D3DTEXF_LINEAR;
    GLint min_filter = !mips   ? (point ? GL_NEAREST : GL_LINEAR)
                       : point ? (mip_linear ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST)
                               : (mip_linear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
    if (aniso && g_max_anisotropy > 1.0f)
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, mips ? g_max_anisotropy : 1.0f);
}
