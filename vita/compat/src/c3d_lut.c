// sh2-vita: the lookup tables of the 3DS GPU (lighting, distance attenuation, fog, procedural
// texture maps and colours) live in one RGBA texture, 256 wide, one table per row. The shader
// generator asks for a table's row and samples it; a row is uploaded again only when the table's
// contents changed.
#include "c3d_internal.h"

#include <string.h>

#define ROWS 64

static GLuint s_tex;
static struct {
	const void* key;
	u32 hash;
	u32 used_frame;
} s_rows[ROWS];
static u32 s_frame;

static u32 hash_bytes(const void* p, size_t n)
{
	const u8* b = p;
	u32 h = 2166136261u;
	for (size_t i = 0; i < n; i++)
		h = (h ^ b[i]) * 16777619u;
	return h;
}

GLuint c3d_lut_texture(void)
{
	if (!s_tex) {
		glGenTextures(1, &s_tex);
		glBindTexture(GL_TEXTURE_2D, s_tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, ROWS, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	return s_tex;
}

void c3d_lut_frame_begin(void) { s_frame++; }

static u8 to_u8(float v) { return v <= 0 ? 0 : v >= 1 ? 255 : (u8)(v * 255.0f + 0.5f); }

// values: n floats (n <= 256), stretched over the row; for colour tables pass n = 0 and use
// c3d_lut_row_rgba.
static int row_for(const void* key, u32 h, bool* changed)
{
	int free_row = -1, oldest = 0;
	for (int i = 0; i < ROWS; i++) {
		if (s_rows[i].key == key) {
			*changed = s_rows[i].hash != h;
			s_rows[i].hash = h;
			s_rows[i].used_frame = s_frame;
			return i;
		}
		if (!s_rows[i].key && free_row < 0)
			free_row = i;
		if (s_rows[i].used_frame < s_rows[oldest].used_frame)
			oldest = i;
	}
	int r = free_row >= 0 ? free_row : oldest;
	s_rows[r].key = key;
	s_rows[r].hash = h;
	s_rows[r].used_frame = s_frame;
	*changed = true;
	return r;
}

int c3d_lut_row(const void* key, const float* values, int n)
{
	bool changed;
	int r = row_for(key, hash_bytes(values, n * sizeof(float)), &changed);
	if (changed) {
		u8 row[256 * 4];
		for (int i = 0; i < 256; i++) {
			// stretch n entries over 256 texels
			float v = values[i * n / 256];
			row[i * 4] = row[i * 4 + 1] = row[i * 4 + 2] = row[i * 4 + 3] = to_u8(v);
		}
		glBindTexture(GL_TEXTURE_2D, c3d_lut_texture());
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, r, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, row);
	}
	return r;
}

// A colour table (procedural texture colours, 0xAABBGGRR as the 3DS keeps them).
int c3d_lut_row_rgba(const void* key, const u32* colors, int n)
{
	bool changed;
	int r = row_for(key, hash_bytes(colors, n * sizeof(u32)), &changed);
	if (changed) {
		u8 row[256 * 4];
		for (int i = 0; i < 256; i++) {
			u32 c = colors[i * n / 256];
			row[i * 4] = c & 0xFF;
			row[i * 4 + 1] = (c >> 8) & 0xFF;
			row[i * 4 + 2] = (c >> 16) & 0xFF;
			row[i * 4 + 3] = (c >> 24) & 0xFF;
		}
		glBindTexture(GL_TEXTURE_2D, c3d_lut_texture());
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, r, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, row);
	}
	return r;
}

float c3d_lut_v(int row) { return (row + 0.5f) / ROWS; }
