// sh2-vita: the 3DS GPU state the compat layer records, shared by the c3d_*.c files.
#pragma once

#include <citro3d.h>
#include <vitaGL.h>

#include "vita_compat.h"

// Shader ids (the tag in each embedded *_shbin)
enum { SHBIN_WORLD = 1, SHBIN_SKIN, SHBIN_SHADOW, SHBIN_FOG, SHBIN_MOVIE, SHBIN_TOUCHUI, SHBIN_COUNT };

typedef struct {
	// texture combiners
	C3D_TexEnv env[6];
	u8 env_buf_rgb;      // bit i: stage i writes the combiner buffer (RGB)
	u8 env_buf_alpha;
	u32 env_buf_color;

	// fragment operations
	bool depth_test;
	GPU_TESTFUNC depth_func;
	u32 write_mask;
	float z_scale, z_offset;
	bool alpha_test;
	GPU_TESTFUNC alpha_func;
	int alpha_ref;
	bool stencil_test;
	GPU_TESTFUNC stencil_func;
	int stencil_ref, stencil_in_mask, stencil_write_mask;
	GPU_STENCILOP stencil_fail, stencil_zfail, stencil_pass;
	GPU_BLENDEQUATION blend_eq_rgb, blend_eq_a;
	GPU_BLENDFACTOR blend_src_rgb, blend_dst_rgb, blend_src_a, blend_dst_a;
	bool logic_op;       // a logic op replaces blending (only COPY is supported)
	u32 blend_color;
	GPU_CULLMODE cull;

	// fog
	GPU_FOGMODE fog_mode;
	bool fog_zflip;
	u32 fog_color;
	C3D_FogLut* fog_lut;

	// procedural texture (unit 3)
	C3D_ProcTex* proctex;
	int proctex_coord;
	C3D_ProcTexLut* pt_lut[6];          // by GPU_PROCTEX_LUTID (noise, -, rgbmap, alphamap)
	C3D_ProcTexColorLut* pt_color;

	// lighting
	C3D_LightEnv* light_env;

	// textures
	C3D_Tex* tex[3];

	// vertex input
	C3D_AttrInfo attr;
	C3D_BufInfo buf;
	C3D_FVec fixed[16];
	shaderProgram_s* program;

	// target
	C3D_RenderTarget* target;
	int vp_x, vp_y, vp_w, vp_h;
	int scissor_mode;
	u32 sc_l, sc_t, sc_r, sc_b;

	bool in_frame;
	float frame_rate;
} C3D_State;

extern C3D_State g_c3d;

// Counters for the once-every-few-seconds line in log.txt (c3d_target.c): where the time goes.
typedef struct {
	u32 tex_n, prog_n, draws;
	float tex_ms, prog_ms;
	float prep_ms, gl_ms;          // in prepare() (state, uniforms, textures) and in vitaGL's draw calls
	u32 vb_new, vb_kb;             // vertex buffers put on the GPU this time; all of them now, in KB
} C3D_Stats;
extern C3D_Stats g_c3d_stats;
u64 c3d_now_us(void);
extern u32 g_c3d_frame;            // frames ended so far (c3d_target.c)
// c3d_shader.c: the game wrote [adr, adr + size) (GSPGPU_FlushDataCache): vertex data kept on the
// GPU from there is stale; and the once-a-frame clean-up of vertex data no longer drawn.
void c3d_vbuf_mark_written(const void* adr, u32 size);
void c3d_vbuf_frame(void);

// c3d_tex.c
void c3d_tex_upload_if_dirty(C3D_Tex* tex);
GLenum c3d_wrap(int wrap);

// c3d_target.c
void c3d_target_bind(C3D_RenderTarget* rt);
void c3d_targets_frame_end(void);

// c3d_shader.c
void c3d_shader_init(void);
void c3d_draw_prepare(void);   // applies all state and binds the program for the next draw

// c3d_lut.c: LUT atlas (a 256-wide texture, one row per LUT) for lighting, fog and procedural tex
int c3d_lut_row(const void* key, const float* values, int n);   // returns the row (and uploads if changed)
GLuint c3d_lut_texture(void);
void c3d_lut_frame_begin(void);
