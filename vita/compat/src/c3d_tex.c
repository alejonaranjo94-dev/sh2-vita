// sh2-vita: citro3d textures and tex3ds (.t3x) loading on vitaGL.
//
// A C3D_Tex keeps its pixels in the 3DS's layout in tex->data (8x8 tiles in Z-order, first row of
// memory = top of the picture), because the game reads and writes them there (fonts, the movie
// player, the room textures it halves). The GL copy is made from it on C3D_TexFlush / first use:
// detiled, converted to a format GL takes, rows kept in memory order. So in GL, t = 0 is the top
// row while on the 3DS t = 1 is; the generated shaders sample uploaded textures at (s, 1 - t).
// Textures a render target draws into (gl_rt) are GL's own and are sampled as they are.
#include "c3d_internal.h"

#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include <tex3ds.h>

// ---- formats ------------------------------------------------------------------------------------
static int bits_per_pixel(GPU_TEXCOLOR f)
{
	switch (f) {
	case GPU_RGBA8: return 32;
	case GPU_RGB8: return 24;
	case GPU_RGBA5551: case GPU_RGB565: case GPU_RGBA4: case GPU_LA8: case GPU_HILO8: return 16;
	case GPU_L8: case GPU_A8: case GPU_LA4: case GPU_ETC1A4: return 8;
	case GPU_L4: case GPU_A4: case GPU_ETC1: return 4;
	}
	return 32;
}

static u32 level_size(const C3D_Tex* t, int level)
{
	u32 w = t->width >> level, h = t->height >> level;
	if (w < 8) w = 8;
	if (h < 8) h = 8;
	return w * h * bits_per_pixel(t->fmt) / 8;
}

u32 C3D_TexCalcTotalSize(u32 size, int maxLevel)
{
	// each level is a quarter of the one before
	u32 total = 0;
	for (int i = 0; i <= maxLevel; i++)
		total += size >> (2 * i);
	return total;
}

// Morton (Z-order) index of pixel (x, y) inside an 8x8 tile
static inline u32 morton8(u32 x, u32 y)
{
	static const u8 tbl[8] = { 0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15 };
	return tbl[x & 7] | (tbl[y & 7] << 1);
}

// ---- ETC1 ---------------------------------------------------------------------------------------
static const int ETC1_MOD[8][4] = {
	{ 2, 8, -2, -8 }, { 5, 17, -5, -17 }, { 9, 29, -9, -29 }, { 13, 42, -13, -42 },
	{ 18, 60, -18, -60 }, { 24, 80, -24, -80 }, { 33, 106, -33, -106 }, { 47, 183, -47, -183 },
};

static inline u8 clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : (u8)v; }

// Decodes one ETC1 block (64 bits, as the 3DS stores it: little endian) into 4x4 RGBA pixels,
// out[y * 4 + x].
static void etc1_block(u64 blk, u8 out[16][4])
{
	u32 hi = (u32)(blk >> 32), lo = (u32)blk;
	bool diff = hi & 2, flip = hi & 1;
	int base[2][3];
	if (diff) {
		int r = (hi >> 27) & 31, g = (hi >> 19) & 31, b = (hi >> 11) & 31;
		int dr = ((int)(hi << 5) >> 29), dg = ((int)(hi << 13) >> 29), db = ((int)(hi << 21) >> 29);
		int r2 = r + dr, g2 = g + dg, b2 = b + db;
		base[0][0] = (r << 3) | (r >> 2); base[0][1] = (g << 3) | (g >> 2); base[0][2] = (b << 3) | (b >> 2);
		base[1][0] = (r2 << 3) | (r2 >> 2); base[1][1] = (g2 << 3) | (g2 >> 2); base[1][2] = (b2 << 3) | (b2 >> 2);
	} else {
		base[0][0] = ((hi >> 28) & 15) * 17; base[0][1] = ((hi >> 20) & 15) * 17; base[0][2] = ((hi >> 12) & 15) * 17;
		base[1][0] = ((hi >> 24) & 15) * 17; base[1][1] = ((hi >> 16) & 15) * 17; base[1][2] = ((hi >> 8) & 15) * 17;
	}
	int table[2] = { (hi >> 5) & 7, (hi >> 2) & 7 };
	for (int x = 0; x < 4; x++)
		for (int y = 0; y < 4; y++) {
			int i = x * 4 + y;   // pixel index bits are stored column by column
			int sub = flip ? (y >= 2) : (x >= 2);
			int idx = ((lo >> i) & 1) | (((lo >> (16 + i)) & 1) << 1);
			int m = ETC1_MOD[table[sub]][idx == 0 ? 0 : idx == 1 ? 1 : idx == 2 ? 2 : 3];
			u8* p = out[y * 4 + x];
			p[0] = clamp255(base[sub][0] + m);
			p[1] = clamp255(base[sub][1] + m);
			p[2] = clamp255(base[sub][2] + m);
			p[3] = 255;
		}
}

// ETC1 / ETC1A4 level -> RGBA8, rows in memory order. 8x8 tiles of four 4x4 blocks in Z-order;
// ETC1A4 puts 8 bytes of alpha (4 bits a pixel, column by column) before each block.
static void decode_etc1(const u8* src, u8* rgba, int w, int h, bool alpha)
{
	u8 px[16][4];
	for (int ty = 0; ty < h; ty += 8)
		for (int tx = 0; tx < w; tx += 8)
			for (int b = 0; b < 4; b++) {
				int bx = tx + (b & 1) * 4, by = ty + (b >> 1) * 4;
				u64 a = 0xFFFFFFFFFFFFFFFFull;
				if (alpha) {
					memcpy(&a, src, 8);
					src += 8;
				}
				u64 blk;
				memcpy(&blk, src, 8);
				src += 8;
				etc1_block(blk, px);
				for (int y = 0; y < 4; y++)
					for (int x = 0; x < 4; x++) {
						u8* o = rgba + ((size_t)(by + y) * w + bx + x) * 4;
						memcpy(o, px[y * 4 + x], 3);
						int ai = x * 4 + y;
						o[3] = alpha ? (u8)(((a >> (ai * 4)) & 15) * 17) : 255;
					}
			}
}

// Other formats -> (GL format, GL type, pixels), rows in memory order.
static void* decode_level(const C3D_Tex* t, const u8* src, int w, int h, GLenum* gl_fmt, GLenum* gl_type)
{
	int bpp = bits_per_pixel(t->fmt);
	if (t->fmt == GPU_ETC1 || t->fmt == GPU_ETC1A4) {
		u8* out = malloc((size_t)w * h * 4);
		if (out)
			decode_etc1(src, out, w, h, t->fmt == GPU_ETC1A4);
		*gl_fmt = GL_RGBA;
		*gl_type = GL_UNSIGNED_BYTE;
		return out;
	}
	bool packed16 = t->fmt == GPU_RGB565 || t->fmt == GPU_RGBA5551 || t->fmt == GPU_RGBA4;
	int out_bpp = packed16 ? 2 : 4;
	u8* out = malloc((size_t)w * h * out_bpp);
	if (!out)
		return NULL;
	if (packed16) {
		*gl_fmt = t->fmt == GPU_RGB565 ? GL_RGB : GL_RGBA;
		*gl_type = t->fmt == GPU_RGB565 ? GL_UNSIGNED_SHORT_5_6_5 : t->fmt == GPU_RGBA5551 ? GL_UNSIGNED_SHORT_5_5_5_1
		                                                                               : GL_UNSIGNED_SHORT_4_4_4_4;
	} else {
		*gl_fmt = GL_RGBA;
		*gl_type = GL_UNSIGNED_BYTE;
	}
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			u32 tile = ((u32)(y >> 3) * (w >> 3) + (x >> 3)) * 64;
			u32 i = tile + morton8(x, y);
			u8* o = out + ((size_t)y * w + x) * out_bpp;
			const u8* s;
			switch (t->fmt) {
			case GPU_RGB565: case GPU_RGBA5551: case GPU_RGBA4:
				memcpy(o, src + i * 2, 2);
				break;
			case GPU_RGBA8: s = src + i * 4; o[0] = s[3]; o[1] = s[2]; o[2] = s[1]; o[3] = s[0]; break;
			case GPU_RGB8: s = src + i * 3; o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = 255; break;
			case GPU_LA8: case GPU_HILO8: s = src + i * 2; o[0] = o[1] = o[2] = s[1]; o[3] = s[0]; break;
			case GPU_L8: o[0] = o[1] = o[2] = src[i]; o[3] = 255; break;
			case GPU_A8: o[0] = o[1] = o[2] = 0; o[3] = src[i]; break;
			case GPU_LA4: { u8 v = src[i]; o[0] = o[1] = o[2] = (v >> 4) * 17; o[3] = (v & 15) * 17; break; }
			case GPU_L4: { u8 v = (src[i >> 1] >> ((i & 1) * 4)) & 15; o[0] = o[1] = o[2] = v * 17; o[3] = 255; break; }
			case GPU_A4: { u8 v = (src[i >> 1] >> ((i & 1) * 4)) & 15; o[0] = o[1] = o[2] = 0; o[3] = v * 17; break; }
			default: memset(o, 255, out_bpp); break;
			}
		}
	(void)bpp;
	return out;
}

// ---- GL side ------------------------------------------------------------------------------------
static GLenum gl_filter(int f) { return f == GPU_LINEAR ? GL_LINEAR : GL_NEAREST; }

GLenum c3d_wrap(int wrap)
{
	switch (wrap) {
	case GPU_REPEAT: return GL_REPEAT;
	case GPU_MIRRORED_REPEAT: return GL_MIRRORED_REPEAT;
	default: return GL_CLAMP_TO_EDGE;   // GL ES has no clamp-to-border; edge is the closest
	}
}

// param bits as citro3d keeps them: mag filter bit 1, min filter bit 2, wrap T bits 8-9, wrap S
// bits 12-13, mipmap filter bit 24
#define P_MAG(p) (((p) >> 1) & 1)
#define P_MIN(p) (((p) >> 2) & 1)
#define P_WRAPT(p) (((p) >> 8) & 3)
#define P_WRAPS(p) (((p) >> 12) & 3)
#define P_MIP(p) (((p) >> 24) & 1)

static void apply_params(C3D_Tex* t)
{
	glBindTexture(GL_TEXTURE_2D, t->gl_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter(P_MAG(t->param)));
	GLenum min = gl_filter(P_MIN(t->param));
	if (t->maxLevel > 0 && !t->gl_rt)
		min = P_MIN(t->param) ? (P_MIP(t->param) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST)
		                      : (P_MIP(t->param) ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, c3d_wrap(P_WRAPS(t->param)));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, c3d_wrap(P_WRAPT(t->param)));
}

static void upload(C3D_Tex* t)
{
	u64 t0 = c3d_now_us();
	if (!t->gl_tex)
		glGenTextures(1, &t->gl_tex);
	glBindTexture(GL_TEXTURE_2D, t->gl_tex);
	const u8* src = t->data;
	for (int lv = 0; lv <= t->maxLevel && src; lv++) {
		int w = t->width >> lv, h = t->height >> lv;
		if (w < 8 || h < 8)
			break;
		GLenum fmt, type;
		void* px = decode_level(t, src, w, h, &fmt, &type);
		if (px) {
			glTexImage2D(GL_TEXTURE_2D, lv, fmt, w, h, 0, fmt, type, px);
			free(px);
		}
		src += level_size(t, lv);
	}
	apply_params(t);
	t->gl_dirty = 0;
	g_c3d_stats.tex_n++;
	g_c3d_stats.tex_ms += (c3d_now_us() - t0) / 1000.0f;
}

void c3d_tex_upload_if_dirty(C3D_Tex* tex)
{
	if (tex && tex->gl_dirty && !tex->gl_rt)
		upload(tex);
}

// ---- registry: C3D_SyncTextureCopy finds the destination texture by its data pointer ----------
#define MAX_TEX 4096
static C3D_Tex* s_live[MAX_TEX];

static void reg_add(C3D_Tex* t)
{
	for (int i = 0; i < MAX_TEX; i++)
		if (!s_live[i] || s_live[i] == t) {
			s_live[i] = t;
			return;
		}
}

static void reg_remove(C3D_Tex* t)
{
	for (int i = 0; i < MAX_TEX; i++)
		if (s_live[i] == t)
			s_live[i] = NULL;
}

C3D_Tex* c3d_tex_find_by_data(const void* data)
{
	// The game may copy a C3D_Tex by value, so the pointer we saw at init can be stale; only trust
	// an entry whose data still matches.
	for (int i = 0; i < MAX_TEX; i++)
		if (s_live[i] && s_live[i]->data == data)
			return s_live[i];
	return NULL;
}

// ---- citro3d API --------------------------------------------------------------------------------
bool C3D_TexInitWithParams(C3D_Tex* tex, C3D_TexCube* cube, C3D_TexInitParams p)
{
	(void)cube;
	memset(tex, 0, sizeof *tex);
	if (p.type != GPU_TEX_2D || p.width < 8 || p.height < 8)
		return false;
	tex->width = p.width;
	tex->height = p.height;
	tex->fmt = p.format;
	tex->maxLevel = p.maxLevel;
	tex->param = (GPU_NEAREST << 1) | (GPU_NEAREST << 2) | (GPU_CLAMP_TO_EDGE << 8) | (GPU_CLAMP_TO_EDGE << 12);
	u32 size = C3D_TexCalcTotalSize((u32)p.width * p.height * bits_per_pixel(p.format) / 8, p.maxLevel);
	tex->data = memalign(0x80, size);
	if (!tex->data)
		return false;
	tex->size = size;
	tex->gl_dirty = 1;
	tex->gl_keep = 1;
	reg_add(tex);
	return true;
}

void* C3D_TexGetImagePtr(C3D_Tex* tex, void* data, int level, u32* size)
{
	u8* p = data;
	for (int i = 0; i < level; i++)
		p += level_size(tex, i);
	if (size)
		*size = level < 0 ? (u32)tex->size : level_size(tex, level);
	return p;
}

void C3D_TexUpload(C3D_Tex* tex, const void* data)
{
	if (tex->data && data && data != tex->data)
		memcpy(tex->data, data, tex->size);
	tex->gl_dirty = 1;
}

void C3D_TexLoadImage(C3D_Tex* tex, const void* data, GPU_TEXFACE face, int level)
{
	(void)face;
	u32 size;
	void* dst = C3D_TexGetImagePtr(tex, tex->data, level, &size);
	memcpy(dst, data, size);
	tex->gl_dirty = 1;
}

void C3D_TexGenerateMipmap(C3D_Tex* tex, GPU_TEXFACE face)
{
	(void)face;
	c3d_tex_upload_if_dirty(tex);
	if (tex->gl_tex) {
		glBindTexture(GL_TEXTURE_2D, tex->gl_tex);
		glGenerateMipmap(GL_TEXTURE_2D);
	}
}

void C3D_TexFlush(C3D_Tex* tex) { tex->gl_dirty = 1; }

void C3D_TexBind(int unitId, C3D_Tex* tex)
{
	if (unitId >= 0 && unitId < 3)
		g_c3d.tex[unitId] = tex;
}

void C3D_TexDelete(C3D_Tex* tex)
{
	for (int i = 0; i < 3; i++)
		if (g_c3d.tex[i] == tex)
			g_c3d.tex[i] = NULL;
	reg_remove(tex);
	if (tex->gl_tex)
		glDeleteTextures(1, &tex->gl_tex);
	free(tex->data);
	tex->data = NULL;
	tex->gl_tex = 0;
}

void C3D_TexSetFilter(C3D_Tex* tex, GPU_TEXTURE_FILTER_PARAM magFilter, GPU_TEXTURE_FILTER_PARAM minFilter)
{
	tex->param = (tex->param & ~6u) | ((u32)magFilter << 1) | ((u32)minFilter << 2);
	if (tex->gl_tex)
		apply_params(tex);
}

void C3D_TexSetFilterMipmap(C3D_Tex* tex, GPU_TEXTURE_FILTER_PARAM filter)
{
	tex->param = (tex->param & ~(1u << 24)) | ((u32)filter << 24);
	if (tex->gl_tex)
		apply_params(tex);
}

void C3D_TexSetWrap(C3D_Tex* tex, GPU_TEXTURE_WRAP_PARAM wrapS, GPU_TEXTURE_WRAP_PARAM wrapT)
{
	tex->param = (tex->param & ~0x3300u) | ((u32)wrapT << 8) | ((u32)wrapS << 12);
	if (tex->gl_tex)
		apply_params(tex);
}

// ---- tex3ds -------------------------------------------------------------------------------------
struct Tex3DS_Texture_s {
	u16 num;
	Tex3DS_SubTexture sub[];
};

typedef struct {
	const u8* p;
	size_t left;
	FILE* f;
} Reader;

static bool rd(Reader* r, void* out, size_t n)
{
	if (r->f)
		return fread(out, 1, n, r->f) == n;
	if (r->left < n)
		return false;
	memcpy(out, r->p, n);
	r->p += n;
	r->left -= n;
	return true;
}

static Tex3DS_Texture import(Reader* r, C3D_Tex* tex)
{
	u8 head[5];
	if (!rd(r, head, 5))
		return NULL;
	u16 num = head[0] | head[1] << 8;
	int lw = (head[2] & 7) + 3, lh = ((head[2] >> 3) & 7) + 3;
	GPU_TEXCOLOR fmt = (GPU_TEXCOLOR)head[3];
	int mips = head[4];

	Tex3DS_Texture t = calloc(1, sizeof *t + num * sizeof(Tex3DS_SubTexture));
	if (!t)
		return NULL;
	t->num = num;
	for (u16 i = 0; i < num; i++) {
		u16 v[6];
		if (!rd(r, v, sizeof v)) {
			free(t);
			return NULL;
		}
		t->sub[i].width = v[0];
		t->sub[i].height = v[1];
		t->sub[i].left = v[2] / 1024.0f;
		t->sub[i].top = v[3] / 1024.0f;
		t->sub[i].right = v[4] / 1024.0f;
		t->sub[i].bottom = v[5] / 1024.0f;
	}

	C3D_TexInitParams p = { (u16)(1 << lw), (u16)(1 << lh), (u8)mips, fmt, GPU_TEX_2D, false };
	if (!C3D_TexInitWithParams(tex, NULL, p)) {
		free(t);
		return NULL;
	}

	// Compression header: type byte + 24-bit size, or 0x80 | type + 56-bit size. The pack is
	// written without compression ("-z none"); other types are not supported here.
	u8 ch[4];
	if (!rd(r, ch, 4))
		goto fail;
	u32 size = ch[1] | ch[2] << 8 | ch[3] << 16;
	if (ch[0] & 0x80) {
		u8 ext[4];
		if (!rd(r, ext, 4))
			goto fail;
		size = ch[1] | ch[2] << 8 | ch[3] << 16 | (u32)ext[0] << 24;
	}
	if ((ch[0] & 0x7F) != 0) {
		vita_log("Tex3DS: compression type 0x%02X not supported", ch[0]);
		goto fail;
	}
	if (size > tex->size)
		size = tex->size;
	if (!rd(r, tex->data, size))
		goto fail;
	tex->gl_dirty = 1;
	return t;

fail:
	C3D_TexDelete(tex);
	free(t);
	return NULL;
}

Tex3DS_Texture Tex3DS_TextureImport(const void* input, size_t insize, C3D_Tex* tex, C3D_TexCube* texcube, bool vram)
{
	(void)texcube; (void)vram;
	Reader r = { input, insize, NULL };
	return import(&r, tex);
}

Tex3DS_Texture Tex3DS_TextureImportStdio(FILE* fp, C3D_Tex* tex, C3D_TexCube* texcube, bool vram)
{
	(void)texcube; (void)vram;
	Reader r = { NULL, 0, fp };
	return import(&r, tex);
}

Tex3DS_Texture Tex3DS_TextureImportCallback(C3D_Tex* tex, C3D_TexCube* texcube, bool vram,
                                            ssize_t (*callback)(void* userdata, void* buffer, size_t size), void* userdata)
{
	(void)tex; (void)texcube; (void)vram; (void)callback; (void)userdata;
	vita_log("Tex3DS_TextureImportCallback: not implemented");
	return NULL;
}

size_t Tex3DS_GetNumSubTextures(const Tex3DS_Texture texture) { return texture ? texture->num : 0; }

const Tex3DS_SubTexture* Tex3DS_GetSubTexture(const Tex3DS_Texture texture, size_t index)
{
	return texture && index < texture->num ? &texture->sub[index] : NULL;
}

void Tex3DS_TextureFree(Tex3DS_Texture texture) { free(texture); }
