// sh2-vita: stand-in for tex3ds's runtime (loading .t3x textures into a C3D_Tex).
// Implemented in vita/compat/src/c3d_tex.c.
#pragma once

#include <citro3d.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Tex3DS_Texture_s* Tex3DS_Texture;

typedef struct {
	u16 width, height;
	float left, top, right, bottom;
} Tex3DS_SubTexture;

Tex3DS_Texture Tex3DS_TextureImport(const void* input, size_t insize, C3D_Tex* tex, C3D_TexCube* texcube, bool vram);
Tex3DS_Texture Tex3DS_TextureImportStdio(FILE* fp, C3D_Tex* tex, C3D_TexCube* texcube, bool vram);
Tex3DS_Texture Tex3DS_TextureImportCallback(C3D_Tex* tex, C3D_TexCube* texcube, bool vram,
                                            ssize_t (*callback)(void* userdata, void* buffer, size_t size), void* userdata);
size_t Tex3DS_GetNumSubTextures(const Tex3DS_Texture texture);
const Tex3DS_SubTexture* Tex3DS_GetSubTexture(const Tex3DS_Texture texture, size_t index);
void Tex3DS_TextureFree(Tex3DS_Texture texture);

static inline bool Tex3DS_SubTextureRotated(const Tex3DS_SubTexture* sub) { return sub->top < sub->bottom; }

#ifdef __cplusplus
}
#endif
