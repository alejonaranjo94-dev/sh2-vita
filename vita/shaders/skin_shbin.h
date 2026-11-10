// sh2-vita: the game embeds the 3DS build of skin.v.pica; on the Vita the data is a tag that
// c3d_shader.c recognises, and the program is vita/shaders/skin.vert.
#pragma once
#include <3ds.h>
extern const u8 skin_shbin[];
extern const u32 skin_shbin_size;
