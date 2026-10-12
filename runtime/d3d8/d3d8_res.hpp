// d3d8_res.hpp -- texture-level helpers shared by d3d8_levels.cpp and d3d8_textures.cpp.
#pragma once
#include "d3d8_int.hpp"

bool compressed(uint32_t f);
uint32_t bpp(uint32_t f);
void level_geom(uint32_t fmt, uint32_t w, uint32_t h, uint32_t l, uint32_t *pitch, uint32_t *rows, uint32_t *size);
void upload_level(Obj *t, uint32_t l);
uint32_t new_texture(Obj *dev, uint32_t w, uint32_t h, uint32_t levels, uint32_t usage, uint32_t fmt, uint32_t pool,
                     uint32_t *hr);
void readback_level(Obj *t, uint32_t l);
uint32_t lock_level(Obj *t, uint32_t l, uint32_t lr, uint32_t rect);
void level_desc(Obj *t, uint32_t l, uint32_t p);
uint32_t level_surface(Obj *t, uint32_t l);
