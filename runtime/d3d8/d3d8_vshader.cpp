// d3d8_vshader.cpp -- D3D8 vertex shaders: declaration parsing and vs.1.x -> GLSL ES 3.00 through
// MojoShader (third_party/mojoshader, docs/PRIOR_ART.md).
//
// D3D8 bytecode has no dcl instructions: the declaration binds v<reg> to stream data. As
// d3d8to9 does (d3d8to9_device.cpp CreateVertexShader), each declared register becomes a
// D3D9 dcl_<usage><index> v<reg> -- here inserted as tokens after the version token instead of
// through D3DX's disassembler. D3DVSD_CONST blocks are kept and loaded into the constant
// registers when the shader is set (D3D8 semantics).
#include "d3d8_int.hpp"
#include "mojoshader.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// D3DVSDE_* register -> (D3DDECLUSAGE, index); d3d8to9's DeclAddressUsages.
static const uint8_t k_usage[17]
                            [2] = {
                                { 0, 0 }, { 1, 0 },  { 2, 0 },  { 3, 0 },
                                { 4, 0 }, { 10, 0 }, { 10, 1 }, // POSITION BLENDWEIGHT BLENDINDICES NORMAL PSIZE COLOR0 COLOR1
                                { 5, 0 }, { 5, 1 },  { 5, 2 },  { 5, 3 },
                                { 5, 4 }, { 5, 5 },  { 5, 6 },  { 5, 7 }, // TEXCOORD0..7
                                { 0, 1 }, { 3, 1 }, // POSITION1 NORMAL1
                            };
// D3DVSDT_* -> size in bytes (FLOAT1..4, D3DCOLOR, UBYTE4, SHORT2, SHORT4)
static const uint8_t k_type_size[8] = { 4, 8, 12, 16, 4, 4, 4, 8 };

bool vs8_parse_decl(uint32_t p, Vs8 *vs) {
    uint32_t stream = 0, offset = 0;
    bool ok = true;
    for (uint32_t t; (t = rd32(p)) != 0xFFFFFFFFu; p += 4) {
        switch (t >> 29) {
        case 0: break; // D3DVSD_NOP
        case 1:
            stream = t & 0xF;
            offset = 0;
            vs->streams |= 1u << stream;
            break; // D3DVSD_STREAM
        case 2: // D3DVSD_REG / D3DVSD_SKIP
            if (t & 0x10000000u) {
                offset += ((t >> 16) & 0xF) * 4;
                break;
            }
            {
                Vs8Elem e = { (uint8_t)stream, (uint8_t)(t & 0x1F), (uint8_t)((t >> 16) & 0xF), (uint16_t)offset };
                if (e.type > 7 || e.reg > 16) {
                    ok = false;
                    break;
                }
                vs->elems.push_back(e);
                offset += k_type_size[e.type];
            }
            break;
        case 3: ok = false; break; // tessellator: not used on PC parts
        case 4: { // D3DVSD_CONST: count, address, data
            uint32_t n = (t >> 25) & 0xF, addr = t & 0x7F;
            for (uint32_t i = 0; i < n; i++, addr++) {
                Vs8Const c;
                c.reg = addr;
                for (int k = 0; k < 4; k++) {
                    uint32_t u = rd32(p + 4 + 16 * i + 4 * (uint32_t)k);
                    memcpy(&c.v[k], &u, 4);
                }
                vs->consts.push_back(c);
            }
            p += 16 * n;
            break;
        }
        default: ok = false; break; // D3DVSD_EXT and unknown tokens
        }
    }
    return ok;
}

// Length in tokens of the function at va, including the end token (comments may hold any value).
static uint32_t func_tokens(uint32_t va) {
    uint32_t n = 1;
    for (;;) {
        uint32_t t = rd32(va + 4 * n);
        if (t == 0x0000FFFFu) return n + 1;
        if ((t & 0xFFFF) == 0xFFFE) n += (t >> 16) & 0x7FFF; // comment block
        n++;
        if (n > 65536) return 0;
    }
}

static void dump(int id, const Vs8 *vs, const uint32_t *tok, uint32_t n) {
    mkdir("logs/shaders", 0755);
    char path[64];
    snprintf(path, sizeof path, "logs/shaders/vs_%03d.glsl", id);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "// version 0x%08X, %u tokens, streams 0x%X, %zu decl consts\n// decl:", tok[0], n, vs->streams,
            vs->consts.size());
    for (const Vs8Elem &e : vs->elems) fprintf(f, " s%u+%u:v%u/t%u", e.stream, e.offset, e.reg, e.type);
    fprintf(f, "\n");
    const MOJOSHADER_parseData *pd = vs->pd;
    for (int i = 0; i < pd->error_count; i++)
        fprintf(f, "// ERROR @%d: %s\n", pd->errors[i].error_position, pd->errors[i].error);
    for (int i = 0; i < pd->uniform_count; i++)
        fprintf(f, "// uniform %s type %d index %d count %d\n", pd->uniforms[i].name, pd->uniforms[i].type,
                pd->uniforms[i].index, pd->uniforms[i].array_count);
    for (int i = 0; i < pd->attribute_count; i++)
        fprintf(f, "// attribute %s usage %d index %d\n", pd->attributes[i].name, pd->attributes[i].usage,
                pd->attributes[i].index);
    for (int i = 0; i < pd->output_count; i++)
        fprintf(f, "// output %s usage %d index %d\n", pd->outputs[i].name, pd->outputs[i].usage, pd->outputs[i].index);
    if (pd->output) fwrite(pd->output, 1, (size_t)pd->output_len, f);
    fclose(f);
}

bool vs8_translate(uint32_t func_va, Vs8 *vs, int id) {
    uint32_t n = func_tokens(func_va);
    if (!n) {
        rt_log("CreateVertexShader #%d: no end token", id);
        return false;
    }
    std::vector<uint32_t> tok;
    tok.reserve(n + 3 * vs->elems.size());
    tok.push_back(rd32(func_va)); // version (vs.1.0 / vs.1.1)
    for (const Vs8Elem &e : vs->elems) {
        tok.push_back(0x0000001Fu); // D3DSIO_DCL
        tok.push_back(0x80000000u | k_usage[e.reg][0] | (uint32_t)k_usage[e.reg][1] << 16);
        tok.push_back(0x900F0000u | e.reg); // v<reg>, writemask xyzw
    }
    for (uint32_t i = 1; i < n; i++) tok.push_back(rd32(func_va + 4 * i));
    vs->pd = MOJOSHADER_parse(MOJOSHADER_PROFILE_GLSLES3, "main", (const unsigned char *)tok.data(),
                              (unsigned)(tok.size() * 4), nullptr, 0, nullptr, 0, nullptr, nullptr, nullptr);
    dump(id, vs, tok.data(), n);
    if (vs->pd->error_count) {
        rt_log("CreateVertexShader #%d: MojoShader: %s", id, vs->pd->errors[0].error);
        return false;
    }
    rt_log("CreateVertexShader #%d: vs.%u.%u, %d instructions, %zu inputs -> GLSL ES (%d bytes)", id,
           (tok[0] >> 8) & 0xFF, tok[0] & 0xFF, vs->pd->instruction_count, vs->elems.size(), vs->pd->output_len);
    return true;
}

// The GL program for a translated shader: its constant registers in vs_uniforms_vec4 slot order
// (MojoShader packs the registers it reads in the order it lists them) and its declaration.
D3D9WProgVS *vs8_gl(Vs8 *vs, int id) {
    if (vs->gl_tried) return vs->gl;
    vs->gl_tried = true;
    const MOJOSHADER_parseData *pd = vs->pd;
    if (!pd || pd->error_count || !pd->output) return nullptr;
    std::vector<unsigned char> regs;
    for (int i = 0; i < pd->uniform_count; i++)
        if (pd->uniforms[i].type == MOJOSHADER_UNIFORM_FLOAT)
            for (int k = 0; k < (pd->uniforms[i].array_count ? pd->uniforms[i].array_count : 1); k++)
                regs.push_back((unsigned char)(pd->uniforms[i].index + k));
    std::vector<D3D9WElem> el;
    for (const Vs8Elem &e : vs->elems) el.push_back({ e.stream, e.reg, e.type, e.offset });
    vs->gl = d3d9w_progvs_create(pd->output, regs.data(), (int)regs.size(), el.data(), (int)el.size());
    rt_log("SetVertexShader #%d: GL program %s (%zu constants, %zu inputs)", id, vs->gl ? "linked" : "FAILED",
           regs.size(), el.size());
    return vs->gl;
}

// ---- pixel shaders --------------------------------------------------------------------------
// D3D8 ps.1.x bytecode is D3D9's for these versions (no declarations), so MojoShader takes it as is.
bool ps8_translate(uint32_t func_va, Ps8 *ps, int id) {
    uint32_t n = func_tokens(func_va);
    if (!n) {
        rt_log("CreatePixelShader #%d: no end token", id);
        return false;
    }
    std::vector<uint32_t> tok(n);
    for (uint32_t i = 0; i < n; i++) tok[i] = rd32(func_va + 4 * i);
    ps->pd = MOJOSHADER_parse(MOJOSHADER_PROFILE_GLSLES3, "main", (const unsigned char *)tok.data(),
                              (unsigned)(tok.size() * 4), nullptr, 0, nullptr, 0, nullptr, nullptr, nullptr);
    {
        mkdir("logs/shaders", 0755);
        char path[64];
        snprintf(path, sizeof path, "logs/shaders/ps_%03d.glsl", id);
        if (FILE *f = fopen(path, "w")) {
            const MOJOSHADER_parseData *pd = ps->pd;
            fprintf(f, "// version 0x%08X, %u tokens\n", tok[0], n);
            for (int i = 0; i < pd->error_count; i++)
                fprintf(f, "// ERROR @%d: %s\n", pd->errors[i].error_position, pd->errors[i].error);
            for (int i = 0; i < pd->uniform_count; i++)
                fprintf(f, "// uniform %s type %d index %d count %d\n", pd->uniforms[i].name, pd->uniforms[i].type,
                        pd->uniforms[i].index, pd->uniforms[i].array_count);
            for (int i = 0; i < pd->sampler_count; i++)
                fprintf(f, "// sampler %s index %d type %d\n", pd->samplers[i].name, pd->samplers[i].index,
                        pd->samplers[i].type);
            if (pd->output) fwrite(pd->output, 1, (size_t)pd->output_len, f);
            fclose(f);
        }
    }
    if (ps->pd->error_count) {
        rt_log("CreatePixelShader #%d: MojoShader: %s", id, ps->pd->errors[0].error);
        return false;
    }
    return true;
}

D3D9WProgPS *ps8_gl(Ps8 *ps, int id) {
    if (ps->gl_tried) return ps->gl;
    ps->gl_tried = true;
    const MOJOSHADER_parseData *pd = ps->pd;
    if (!pd || pd->error_count || !pd->output) return nullptr;
    std::vector<unsigned char> regs;
    for (int i = 0; i < pd->uniform_count; i++)
        if (pd->uniforms[i].type == MOJOSHADER_UNIFORM_FLOAT)
            for (int k = 0; k < (pd->uniforms[i].array_count ? pd->uniforms[i].array_count : 1); k++)
                regs.push_back((unsigned char)(pd->uniforms[i].index + k));
    ps->gl = d3d9w_progps_create(pd->output, regs.data(), (int)regs.size());
    rt_log("SetPixelShader #%d: %s (%zu constants, %d samplers)", id, ps->gl ? "compiled" : "FAILED", regs.size(),
           pd->sampler_count);
    return ps->gl;
}
