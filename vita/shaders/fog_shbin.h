// sh2-vita: the game embeds the 3DS build of fog.v.pica; on the Vita the data is a tag that
// c3d_shader.c recognises, and the program is vita/shaders/fog.vert.
#pragma once
#include <3ds.h>
extern const u8 fog_shbin[];
extern const u32 fog_shbin_size;
