// sh2-vita: the 3DS shaders and drawing on vitaGL.
//
// Vertex side: the game's six .pica programs, translated by hand to GLSL ES 1.00 (VS_* below).
// They read the 3DS uniform registers (u_c[96], as the game wrote them with C3D_FVUnif*) and the
// input registers v0..v4 (attributes a_v0..a_v4, as the game's AttrInfo loads them).
// Fragment side: generated from the recorded 3DS state (texture combiners, fragment lighting, fog,
// procedural texture, alpha test), one GLSL program per distinct state, cached.
#include "c3d_internal.h"

#include <stdarg.h>
#include <stdio.h>

#include <stdlib.h>
#include <string.h>

// ======================================================================= the embedded shader tags
#define TAG(name, id) const u8 name##_shbin[12] __attribute__((aligned(4))) = { 'S', 'H', '2', 'V', 'S', 'H', 'B', 'N', id, 0, 0, 0 }; \
	const u32 name##_shbin_size = 12;
TAG(world, SHBIN_WORLD)
TAG(skin, SHBIN_SKIN)
TAG(shadow, SHBIN_SHADOW)
TAG(fog, SHBIN_FOG)
TAG(movie, SHBIN_MOVIE)
TAG(touchui, SHBIN_TOUCHUI)

// Uniform registers of each program, in the order its .fvec lines declare them.
typedef struct { const char* name; int reg; } UnifLoc;
static const UnifLoc UNIFS[SHBIN_COUNT][9] = {
	[SHBIN_WORLD] = { { "projection", 0 }, { "modelView", 4 }, { "lightDir", 8 }, { "lightCol", 11 },
	                  { "amb", 14 }, { "gain", 15 }, { "vgain", 16 }, { "base", 17 }, { NULL, 0 } },
	[SHBIN_SKIN] = { { "projection", 0 }, { "modelView", 4 }, { "bones", 8 }, { NULL, 0 } },
	[SHBIN_SHADOW] = { { "projection", 0 }, { "modelView", 4 }, { NULL, 0 } },
	[SHBIN_FOG] = { { "projection", 0 }, { "modelView", 4 }, { NULL, 0 } },
	[SHBIN_MOVIE] = { { "projection", 0 }, { NULL, 0 } },
	[SHBIN_TOUCHUI] = { { "projection", 0 }, { NULL, 0 } },
};

DVLB_s* DVLB_ParseFile(u32* shbinData, u32 shbinSize)
{
	const u8* d = (const u8*)shbinData;
	if (shbinSize < 9 || memcmp(d, "SH2VSHBN", 8) != 0 || d[8] == 0 || d[8] >= SHBIN_COUNT) {
		vita_log("DVLB_ParseFile: unknown shader");
		return NULL;
	}
	DVLB_s* dvlb = calloc(1, sizeof *dvlb + sizeof(DVLE_s));
	dvlb->numDVLE = 1;
	dvlb->DVLE = (DVLE_s*)(dvlb + 1);
	dvlb->DVLE[0].id = d[8];
	dvlb->shbin_id = d[8];
	return dvlb;
}

void DVLB_Free(DVLB_s* dvlb) { free(dvlb); }

Result shaderProgramInit(shaderProgram_s* sp)
{
	memset(sp, 0, sizeof *sp);
	return 0;
}

Result shaderProgramFree(shaderProgram_s* sp)
{
	free(sp->vertexShader);
	sp->vertexShader = NULL;
	return 0;
}

Result shaderProgramSetVsh(shaderProgram_s* sp, DVLE_s* dvle)
{
	if (!dvle)
		return -1;
	free(sp->vertexShader);
	sp->vertexShader = calloc(1, sizeof(shaderInstance_s));
	sp->vertexShader->dvle = dvle;
	sp->vertexShader->id = dvle->id;
	sp->shbin_id = dvle->id;
	return 0;
}

s8 shaderInstanceGetUniformLocation(shaderInstance_s* si, const char* name)
{
	if (!si || si->id >= SHBIN_COUNT)
		return -1;
	for (const UnifLoc* u = UNIFS[si->id]; u->name; u++)
		if (!strcmp(u->name, name))
			return (s8)u->reg;
	return -1;
}

void C3D_BindProgram(shaderProgram_s* program) { g_c3d.program = program; }

// ============================================================================ vertex shaders
// Varyings (at most 10 on the Vita): v_tc = tc0.xy, tc1.xy; v_color; v_view; v_nq.
static const char* VS_COMMON =
	"uniform vec4 u_c[96];\n"
	"uniform vec2 u_depth;\n"            // (2 * zScale, 2 * zOffset - 1): the 3DS depth map into GL's range
	"varying vec4 v_tc;\n"
	"varying vec4 v_color;\n"
	"varying vec3 v_view;\n"
	"varying vec4 v_nq;\n"
	"vec4 xf(int b, vec4 p) { return vec4(dot(u_c[b], p), dot(u_c[b + 1], p), dot(u_c[b + 2], p), dot(u_c[b + 3], p)); }\n"
	"void out_pos(vec4 c) { gl_Position = vec4(c.x, c.y, u_depth.x * c.z + u_depth.y * c.w, c.w); }\n"
	// the quaternion that turns (0,0,1) to the normal n, as the .pica programs build it
	"vec4 nquat(vec3 n) {\n"
	"  float h = 0.5 * (1.0 + n.z);\n"
	"  if (h <= 0.0) return vec4(1.0, 0.0, 0.0, 0.0);\n"
	"  float r = inversesqrt(h);\n"
	"  return vec4(0.5 * n.x * r, 0.5 * n.y * r, sqrt(h), 0.0);\n"
	"}\n"
	"vec3 view_normal(vec3 n) { return normalize(vec3(dot(u_c[4].xyz, n), dot(u_c[5].xyz, n), dot(u_c[6].xyz, n))); }\n";

static const char* VS_BODY[SHBIN_COUNT] = {
	[SHBIN_WORLD] =
		"attribute vec4 a_v0; attribute vec4 a_v1; attribute vec4 a_v2; attribute vec4 a_v3;\n"
		"void main() {\n"
		"  vec4 mv = xf(4, vec4(a_v0.xyz, 1.0));\n"
		"  v_view = -mv.xyz;\n"
		"  out_pos(xf(0, mv));\n"
		"  v_tc = vec4(a_v1.x, 1.0 - a_v1.y, a_v1.x, 1.0 - a_v1.y);\n"
		"  vec3 n = normalize(a_v3.xyz);\n"
		"  vec4 lit = u_c[11] * max(dot(u_c[8].xyz, n), 0.0) + u_c[12] * max(dot(u_c[9].xyz, n), 0.0)\n"
		"           + u_c[13] * max(dot(u_c[10].xyz, n), 0.0);\n"
		"  vec4 col = (u_c[14] + lit) * u_c[15];\n"
		"  vec4 vc = a_v2 * (1.0 / 255.0);\n"
		"  col = min(vc * u_c[16] + col + u_c[17], vec4(1.0));\n"
		"  v_color = vec4(col.rgb, vc.a);\n"
		"  v_nq = nquat(view_normal(n));\n"
		"}\n",
	[SHBIN_SKIN] =
		"attribute vec4 a_v0; attribute vec4 a_v1; attribute vec4 a_v2; attribute vec4 a_v3; attribute vec4 a_v4;\n"
		"void bone(float idx, float w, vec4 p, inout vec3 pos, inout vec3 nrm) {\n"
		"  int b = 8 + int(idx);\n"
		"  pos += w * vec3(dot(u_c[b], p), dot(u_c[b + 1], p), dot(u_c[b + 2], p));\n"
		"  nrm += w * vec3(dot(u_c[b].xyz, a_v4.xyz), dot(u_c[b + 1].xyz, a_v4.xyz), dot(u_c[b + 2].xyz, a_v4.xyz));\n"
		"}\n"
		"void main() {\n"
		"  vec4 p = vec4(a_v0.xyz, 1.0);\n"
		"  vec4 w = a_v3 * (1.0 / 255.0);\n"
		"  vec3 pos = vec3(0.0), nrm = vec3(0.0);\n"
		"  bone(a_v2.x, w.x, p, pos, nrm);\n"
		"  bone(a_v2.y, w.y, p, pos, nrm);\n"
		"  bone(a_v2.z, w.z, p, pos, nrm);\n"
		"  bone(a_v2.w, w.w, p, pos, nrm);\n"
		"  vec4 mv = xf(4, vec4(pos, 1.0));\n"
		"  v_view = -mv.xyz;\n"
		"  out_pos(xf(0, mv));\n"
		"  v_tc = vec4(a_v1.x, 1.0 - a_v1.y, a_v1.x, 1.0 - a_v1.y);\n"
		"  v_color = vec4(1.0);\n"
		"  v_nq = nquat(view_normal(nrm));\n"
		"}\n",
	[SHBIN_SHADOW] =
		"attribute vec4 a_v0; attribute vec4 a_v1;\n"
		"void main() {\n"
		"  vec4 mv = xf(4, vec4(a_v0.xyz, 1.0));\n"
		"  out_pos(xf(0, mv));\n"
		"  v_tc = vec4(0.0); v_view = vec3(0.0); v_nq = vec4(0.0, 0.0, 1.0, 0.0);\n"
		"  v_color = a_v1 * (1.0 / 255.0);\n"
		"}\n",
	[SHBIN_FOG] =
		"attribute vec4 a_v0; attribute vec4 a_v1; attribute vec4 a_v2; attribute vec4 a_v3;\n"
		"void main() {\n"
		"  vec4 mv = xf(4, vec4(a_v0.xyz, 1.0));\n"
		"  out_pos(xf(0, mv));\n"
		"  v_tc = vec4(a_v1.xy, a_v2.xy);\n"
		"  v_view = vec3(0.0); v_nq = vec4(0.0, 0.0, 1.0, 0.0);\n"
		"  v_color = a_v3 * (1.0 / 255.0);\n"
		"}\n",
	[SHBIN_MOVIE] =
		"attribute vec4 a_v0; attribute vec4 a_v1;\n"
		"void main() {\n"
		"  out_pos(xf(0, vec4(a_v0.xy, 0.5, 1.0)));\n"
		"  v_tc = vec4(a_v1.xy, a_v1.xy);\n"
		"  v_color = vec4(1.0); v_view = vec3(0.0); v_nq = vec4(0.0, 0.0, 1.0, 0.0);\n"
		"}\n",
	[SHBIN_TOUCHUI] =
		"attribute vec4 a_v0; attribute vec4 a_v1; attribute vec4 a_v2;\n"
		"void main() {\n"
		"  out_pos(xf(0, vec4(a_v0.xy, 0.5, 1.0)));\n"
		"  v_tc = vec4(a_v1.x, 1.0 - a_v1.y, a_v1.x, 1.0 - a_v1.y);\n"
		"  v_color = a_v2 * (1.0 / 255.0);\n"
		"  v_view = vec3(0.0); v_nq = vec4(0.0, 0.0, 1.0, 0.0);\n"
		"}\n",
};

// ======================================================================= fragment shader key
typedef struct {
	u8 shbin;
	struct { u16 src_rgb, src_a; u32 op; u8 func_rgb, func_a, scale_rgb, scale_a; } env[6];
	u8 buf_rgb, buf_a;
	u8 alpha_func;       // GPU_ALWAYS when the test is off
	u8 fog;              // 0 off, 1 fog, 2 fog with z flip
	u8 proctex;          // 0 off, else 1 + coord
	u8 pt_u_clamp, pt_v_clamp, pt_rgb_func, pt_a_func, pt_separate;
	u8 lighting;         // number of light slots in use (0 = off)
	u8 light_flags[GPU_LIGHTS];   // bit0 on, bit1 directional, bit2 spot, bit3 distance attenuation
	u8 d0_on, d0_input, d0_abs;
} FragKey;

typedef struct {
	FragKey key;
	u32 hash;
	GLuint prog;
	GLint u_c, u_depth, u_tex[3], u_tflip, u_lut, u_env_color, u_buf_color, u_alpha_ref, u_fog_color, u_fog_row;
	GLint u_pt_rows, u_pt_offset;
	GLint u_scene_amb, u_lpos, u_ldiff, u_lamb, u_lspec, u_lspot, u_lda, u_lrows, u_d0_row;
	bool failed;
} Program;

#define MAX_PROGRAMS 512
static Program s_progs[MAX_PROGRAMS];
static int s_nprogs;

static u32 hash_key(const FragKey* k)
{
	const u8* b = (const u8*)k;
	u32 h = 2166136261u;
	for (size_t i = 0; i < sizeof *k; i++)
		h = (h ^ b[i]) * 16777619u;
	return h;
}

static bool env_is_passthrough(const C3D_TexEnv* e)
{
	return (e->srcRgb & 0xF) == GPU_PREVIOUS && (e->srcAlpha & 0xF) == GPU_PREVIOUS && e->funcRgb == GPU_REPLACE &&
	       e->funcAlpha == GPU_REPLACE && (e->opRgb & 0xF) == 0 && (e->opAlpha & 0xF) == 0 && e->scaleRgb == 0 &&
	       e->scaleAlpha == 0;
}

static bool uses_source(const FragKey* k, int src)
{
	for (int i = 0; i < 6; i++)
		for (int j = 0; j < 3; j++)
			if (((k->env[i].src_rgb >> (4 * j)) & 0xF) == src || ((k->env[i].src_a >> (4 * j)) & 0xF) == src)
				return true;
	return false;
}

static void make_key(FragKey* k)
{
	memset(k, 0, sizeof *k);
	k->shbin = g_c3d.program ? (u8)g_c3d.program->shbin_id : SHBIN_WORLD;
	for (int i = 0; i < 6; i++) {
		const C3D_TexEnv* e = &g_c3d.env[i];
		if (env_is_passthrough(e)) {
			k->env[i].src_rgb = k->env[i].src_a = GPU_PREVIOUS;
			continue;
		}
		k->env[i].src_rgb = e->srcRgb;
		k->env[i].src_a = e->srcAlpha;
		k->env[i].op = e->opAll;
		k->env[i].func_rgb = (u8)e->funcRgb;
		k->env[i].func_a = (u8)e->funcAlpha;
		k->env[i].scale_rgb = (u8)e->scaleRgb;
		k->env[i].scale_a = (u8)e->scaleAlpha;
	}
	k->buf_rgb = g_c3d.env_buf_rgb;
	k->buf_a = g_c3d.env_buf_alpha;
	k->alpha_func = g_c3d.alpha_test ? (u8)g_c3d.alpha_func : GPU_ALWAYS;
	if (g_c3d.fog_mode == GPU_FOG && g_c3d.fog_lut)
		k->fog = g_c3d.fog_zflip ? 2 : 1;
	if (g_c3d.proctex && uses_source(k, GPU_TEXTURE3)) {
		const C3D_ProcTex* pt = g_c3d.proctex;
		k->proctex = (u8)(1 + g_c3d.proctex_coord);
		k->pt_u_clamp = pt->uClamp;
		k->pt_v_clamp = pt->vClamp;
		k->pt_rgb_func = pt->rgbFunc;
		k->pt_a_func = pt->alphaFunc;
		k->pt_separate = pt->alphaSeparate;
	}
	const C3D_LightEnv* le = g_c3d.light_env;
	bool frag_light = uses_source(k, GPU_FRAGMENT_PRIMARY_COLOR) || uses_source(k, GPU_FRAGMENT_SECONDARY_COLOR);
	if (le && frag_light && (k->shbin == SHBIN_WORLD || k->shbin == SHBIN_SKIN)) {
		int n = 0;
		for (int i = 0; i < GPU_LIGHTS; i++) {
			const C3D_Light* l = le->lights[i];
			if (!l || !l->enabled)
				continue;
			u8 f = 1;
			if (l->directional) f |= 2;
			if (l->spot_enabled && l->spot_lut) f |= 4;
			if (l->da_enabled && l->da_lut) f |= 8;
			k->light_flags[i] = f;
			n = i + 1;
		}
		k->lighting = (u8)(n ? n : 0xFF);   // 0xFF: lighting on with no lights (ambient only)
		if (le->luts[GPU_LUT_D0]) {
			k->d0_on = 1;
			k->d0_input = le->lut_input[GPU_LUT_D0];
			k->d0_abs = le->lut_abs[GPU_LUT_D0];
		}
	}
}

// ======================================================================= fragment generator
typedef struct { char* p; size_t len, cap; } Str;

static void cat(Str* s, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void cat(Str* s, const char* fmt, ...)
{
	va_list ap;
	for (;;) {
		va_start(ap, fmt);
		int n = vsnprintf(s->p + s->len, s->cap - s->len, fmt, ap);
		va_end(ap);
		if (n >= 0 && s->len + (size_t)n < s->cap) {
			s->len += (size_t)n;
			return;
		}
		s->cap = s->cap * 2 + (size_t)(n > 0 ? n : 256);
		s->p = realloc(s->p, s->cap);
	}
}

static const char* src_expr(int src, int stage)
{
	switch (src) {
	case GPU_PRIMARY_COLOR: return "v_color";
	case GPU_FRAGMENT_PRIMARY_COLOR: return "t_lp";
	case GPU_FRAGMENT_SECONDARY_COLOR: return "t_ls";
	case GPU_TEXTURE0: return "t_t0";
	case GPU_TEXTURE1: return "t_t1";
	case GPU_TEXTURE2: return "t_t2";
	case GPU_TEXTURE3: return "t_t3";
	case GPU_PREVIOUS_BUFFER: return stage == 0 ? "u_buf_color" : "t_buf";
	case GPU_CONSTANT: return "t_k";
	case GPU_PREVIOUS: return stage == 0 ? "vec4(0.0)" : "t_prev";
	default: return "vec4(0.0)";
	}
}

static void rgb_operand(Str* s, const char* v, int op)
{
	switch (op) {
	case GPU_TEVOP_RGB_SRC_COLOR: cat(s, "%s.rgb", v); break;
	case GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR: cat(s, "(vec3(1.0) - %s.rgb)", v); break;
	case GPU_TEVOP_RGB_SRC_ALPHA: cat(s, "%s.aaa", v); break;
	case GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA: cat(s, "vec3(1.0 - %s.a)", v); break;
	case GPU_TEVOP_RGB_SRC_R: cat(s, "%s.rrr", v); break;
	case GPU_TEVOP_RGB_ONE_MINUS_SRC_R: cat(s, "vec3(1.0 - %s.r)", v); break;
	case GPU_TEVOP_RGB_SRC_G: cat(s, "%s.ggg", v); break;
	case GPU_TEVOP_RGB_ONE_MINUS_SRC_G: cat(s, "vec3(1.0 - %s.g)", v); break;
	case GPU_TEVOP_RGB_SRC_B: cat(s, "%s.bbb", v); break;
	case GPU_TEVOP_RGB_ONE_MINUS_SRC_B: cat(s, "vec3(1.0 - %s.b)", v); break;
	default: cat(s, "%s.rgb", v); break;
	}
}

static void a_operand(Str* s, const char* v, int op)
{
	static const char* ch[4] = { "a", "r", "g", "b" };
	int c = (op >> 1) & 3;
	if (op & 1)
		cat(s, "(1.0 - %s.%s)", v, ch[c]);
	else
		cat(s, "%s.%s", v, ch[c]);
}

static void combine(Str* s, int func, const char* a, const char* b, const char* c, bool rgb)
{
	const char* one = rgb ? "vec3(1.0)" : "1.0";
	const char* half = rgb ? "vec3(0.5)" : "0.5";
	switch (func) {
	case GPU_REPLACE: cat(s, "%s", a); break;
	case GPU_MODULATE: cat(s, "%s * %s", a, b); break;
	case GPU_ADD: cat(s, "%s + %s", a, b); break;
	case GPU_ADD_SIGNED: cat(s, "%s + %s - %s", a, b, half); break;
	case GPU_INTERPOLATE: cat(s, "mix(%s, %s, %s)", b, a, c); break;
	case GPU_SUBTRACT: cat(s, "%s - %s", a, b); break;
	case GPU_DOT3_RGB:
	case GPU_DOT3_RGBA:
		if (rgb) cat(s, "vec3(4.0 * dot(%s - vec3(0.5), %s - vec3(0.5)))", a, b);
		else cat(s, "%s", a);   // alpha of DOT3_RGBA is replaced after the stage
		break;
	case GPU_MULTIPLY_ADD: cat(s, "%s * %s + %s", a, b, c); break;
	case GPU_ADD_MULTIPLY: cat(s, "min(%s + %s, %s) * %s", a, b, one, c); break;
	default: cat(s, "%s", a); break;
	}
}

static const char* PT_FUNC[] = {
	"t_u", "t_u * t_u", "t_v", "t_v * t_v", "0.5 * (t_u + t_v)", "0.5 * (t_u * t_u + t_v * t_v)",
	"min(sqrt(t_u * t_u + t_v * t_v), 1.0)", "min(t_u, t_v)", "max(t_u, t_v)",
	"min(0.5 * (t_u + t_v) * 2.0 - max(t_u, t_v), 1.0)",
};

static void pt_clamp(Str* s, const char* var, int mode)
{
	switch (mode) {
	case GPU_PT_CLAMP_TO_ZERO: cat(s, "  if (%s > 1.0) %s = 0.0;\n", var, var); break;
	case GPU_PT_CLAMP_TO_EDGE: cat(s, "  %s = min(%s, 1.0);\n", var, var); break;
	case GPU_PT_REPEAT: cat(s, "  %s = fract(%s);\n", var, var); break;
	case GPU_PT_MIRRORED_REPEAT: cat(s, "  %s = abs(fract(%s * 0.5) * 2.0 - 1.0);\n", var, var); break;
	case GPU_PT_PULSE: cat(s, "  %s = %s > 0.5 ? 1.0 : 0.0;\n", var, var); break;
	}
}

static char* gen_fragment(const FragKey* k)
{
	Str s = { malloc(4096), 0, 4096 };
	cat(&s, "precision highp float;\n"
	        "varying vec4 v_tc;\nvarying vec4 v_color;\nvarying vec3 v_view;\nvarying vec4 v_nq;\n"
	        "uniform sampler2D u_tex0;\nuniform sampler2D u_tex1;\nuniform sampler2D u_tex2;\nuniform sampler2D u_lut;\n"
	        "uniform vec3 u_tflip;\nuniform vec4 u_env_color[6];\nuniform vec4 u_buf_color;\n"
	        "uniform float u_alpha_ref;\nuniform vec3 u_fog_color;\nuniform float u_fog_row;\n"
	        "uniform vec3 u_pt_rows;\nuniform vec2 u_pt_offset;\n"
	        "uniform vec3 u_scene_amb;\nuniform vec3 u_lpos[8];\nuniform vec3 u_ldiff[8];\nuniform vec3 u_lamb[8];\n"
	        "uniform vec3 u_lspec[8];\nuniform vec3 u_lspot[8];\nuniform vec2 u_lda[8];\nuniform vec2 u_lrows[8];\n"
	        "uniform float u_d0_row;\n"
	        "float lut(float lrow, float x) { return texture2D(u_lut, vec2(clamp(x, 0.0, 1.0) * (255.0 / 256.0) + (0.5 / 256.0), lrow)).r; }\n"
	        "vec2 tflip(vec2 t, float f) { return vec2(t.x, mix(t.y, 1.0 - t.y, f)); }\n"
	        "void main() {\n");

	// textures (sampled only when a stage reads them)
	const char* tc[3] = { "v_tc.xy", "v_tc.zw", "v_tc.zw" };
	static const char* flip[3] = { "u_tflip.x", "u_tflip.y", "u_tflip.z" };
	for (int u = 0; u < 3; u++) {
		if (uses_source(k, GPU_TEXTURE0 + u))
			cat(&s, "  vec4 t_t%d = texture2D(u_tex%d, tflip(%s, %s));\n", u, u, tc[u], flip[u]);
		else
			cat(&s, "  vec4 t_t%d = vec4(0.0);\n", u);
	}

	// procedural texture (unit 3)
	if (k->proctex) {
		cat(&s, "  float t_u = abs(%s.x);\n  float t_v = abs(%s.y);\n", k->proctex == 2 ? "v_tc.zw" : "v_tc.xy",
		    k->proctex == 2 ? "v_tc.zw" : "v_tc.xy");
		pt_clamp(&s, "t_u", k->pt_u_clamp);
		pt_clamp(&s, "t_v", k->pt_v_clamp);
		int rf = k->pt_rgb_func < 10 ? k->pt_rgb_func : 0;
		int af = k->pt_a_func < 10 ? k->pt_a_func : 0;
		cat(&s, "  float t_prgb = lut(u_pt_rows.x, %s);\n", PT_FUNC[rf]);
		cat(&s, "  vec4 t_t3 = texture2D(u_lut, vec2((u_pt_offset.x + t_prgb * u_pt_offset.y) / 256.0, u_pt_rows.z));\n");
		if (k->pt_separate) {
			cat(&s, "  float t_pa = lut(u_pt_rows.y, %s);\n", PT_FUNC[af]);
			cat(&s, "  t_t3.a = texture2D(u_lut, vec2((u_pt_offset.x + t_pa * u_pt_offset.y) / 256.0, u_pt_rows.z)).a;\n");
		}
	} else {
		cat(&s, "  vec4 t_t3 = vec4(0.0);\n");
	}

	// fragment lighting
	if (k->lighting) {
		cat(&s, "  vec4 t_q = normalize(v_nq);\n"
		        "  vec3 t_n = vec3(0.0, 0.0, 1.0) + 2.0 * cross(t_q.xyz, cross(t_q.xyz, vec3(0.0, 0.0, 1.0)) + t_q.w * vec3(0.0, 0.0, 1.0));\n"
		        "  vec3 t_vv = normalize(v_view);\n"
		        "  vec3 t_dsum = u_scene_amb;\n  vec3 t_ssum = vec3(0.0);\n");
		int n = k->lighting == 0xFF ? 0 : k->lighting;
		for (int i = 0; i < n; i++) {
			u8 f = k->light_flags[i];
			if (!(f & 1))
				continue;
			// directional lights: the position is the direction towards the light; positional ones:
			// from the fragment (view = -position) to the light
			cat(&s, "  {\n    vec3 t_lr = u_lpos[%d]%s;\n    vec3 t_l = normalize(t_lr);\n    float t_at = 1.0;\n", i,
			    (f & 2) ? "" : " + v_view");
			if (f & 4)
				cat(&s, "    t_at *= lut(u_lrows[%d].x, dot(t_l, u_lspot[%d]) * 0.5 + 0.5);\n", i, i);
			if (f & 8)
				cat(&s, "    t_at *= lut(u_lrows[%d].y, length(t_lr) * u_lda[%d].x + u_lda[%d].y);\n", i, i, i);
			cat(&s, "    t_dsum += (u_ldiff[%d] * max(dot(t_n, t_l), 0.0) + u_lamb[%d]) * t_at;\n", i, i);
			if (k->d0_on) {
				const char* in;
				switch (k->d0_input) {
				case GPU_LUTINPUT_VH: in = "dot(t_vv, normalize(t_l + t_vv))"; break;
				case GPU_LUTINPUT_NV: in = "dot(t_n, t_vv)"; break;
				case GPU_LUTINPUT_LN: in = "dot(t_l, t_n)"; break;
				default: in = "dot(t_n, normalize(t_l + t_vv))"; break;
				}
				if (k->d0_abs)
					cat(&s, "    t_ssum += u_lspec[%d] * lut(u_d0_row, abs(%s)) * t_at;\n", i, in);
				else
					cat(&s, "    t_ssum += u_lspec[%d] * lut(u_d0_row, %s * 0.5 + 0.5) * t_at;\n", i, in);
			} else {
				cat(&s, "    t_ssum += u_lspec[%d] * t_at;\n", i);
			}
			cat(&s, "  }\n");
		}
		cat(&s, "  vec4 t_lp = vec4(clamp(t_dsum, 0.0, 1.0), 1.0);\n  vec4 t_ls = vec4(clamp(t_ssum, 0.0, 1.0), 1.0);\n");
	} else {
		cat(&s, "  vec4 t_lp = vec4(0.0);\n  vec4 t_ls = vec4(0.0);\n");
	}

	// texture combiners
	cat(&s, "  vec4 t_prev = vec4(0.0);\n  vec4 t_buf = u_buf_color;\n  vec4 t_next = u_buf_color;\n  vec4 t_k;\n");
	for (int i = 0; i < 6; i++) {
		const typeof(k->env[0])* e = &k->env[i];
		bool pass = (e->src_rgb & 0xF) == GPU_PREVIOUS && (e->src_a & 0xF) == GPU_PREVIOUS && e->func_rgb == GPU_REPLACE &&
		            e->func_a == GPU_REPLACE && e->op == 0 && e->scale_rgb == 0 && e->scale_a == 0;
		if (!pass) {
			cat(&s, "  t_k = u_env_color[%d];\n  {\n", i);
			const char* sr[3];
			const char* sa[3];
			for (int j = 0; j < 3; j++) {
				sr[j] = src_expr((e->src_rgb >> (4 * j)) & 0xF, i);
				sa[j] = src_expr((e->src_a >> (4 * j)) & 0xF, i);
			}
			u32 oprgb = e->op & 0xFFF, opa = (e->op >> 12) & 0xFFF;
			for (int j = 0; j < 3; j++) {
				cat(&s, "    vec3 t_r%d = ", j);
				rgb_operand(&s, sr[j], (oprgb >> (4 * j)) & 0xF);
				cat(&s, ";\n    float t_a%d = ", j);
				a_operand(&s, sa[j], (opa >> (4 * j)) & 0x7);
				cat(&s, ";\n");
			}
			static const float SCALE[3] = { 1.0f, 2.0f, 4.0f };
			cat(&s, "    vec3 t_rgb = clamp((");
			combine(&s, e->func_rgb, "t_r0", "t_r1", "t_r2", true);
			cat(&s, ") * %.1f, 0.0, 1.0);\n    float t_al = clamp((", SCALE[e->scale_rgb < 3 ? e->scale_rgb : 0]);
			combine(&s, e->func_a, "t_a0", "t_a1", "t_a2", false);
			cat(&s, ") * %.1f, 0.0, 1.0);\n", SCALE[e->scale_a < 3 ? e->scale_a : 0]);
			if (e->func_rgb == GPU_DOT3_RGBA)
				cat(&s, "    t_al = t_rgb.r;\n");
			cat(&s, "    t_prev = vec4(t_rgb, t_al);\n  }\n");
		}
		// the combiner buffer: what stage i + 1 reads is what stage i - 1 left (as the hardware does)
		if (i < 5) {
			cat(&s, "  t_buf = t_next;\n");
			if (i < 4) {
				if (k->buf_rgb & (1 << i)) cat(&s, "  t_next.rgb = t_prev.rgb;\n");
				if (k->buf_a & (1 << i)) cat(&s, "  t_next.a = t_prev.a;\n");
			}
		}
	}

	// alpha test
	static const char* CMP[8] = { "false", "true", "==", "!=", "<", "<=", ">", ">=" };
	if (k->alpha_func == GPU_NEVER)
		cat(&s, "  discard;\n");
	else if (k->alpha_func != GPU_ALWAYS)
		cat(&s, "  if (!(floor(t_prev.a * 255.0 + 0.5) %s u_alpha_ref)) discard;\n", CMP[k->alpha_func & 7]);

	// fog: the factor for this depth from the table, between the fog colour and the fragment
	if (k->fog)
		cat(&s, "  float t_fz = %s;\n  float t_ff = lut(u_fog_row, t_fz);\n  t_prev.rgb = mix(u_fog_color, t_prev.rgb, t_ff);\n",
		    k->fog == 2 ? "1.0 - gl_FragCoord.z" : "gl_FragCoord.z");

	cat(&s, "  gl_FragColor = t_prev;\n}\n");
	return s.p;
}

// ======================================================================= program building
static GLuint compile(GLenum type, const char* a, const char* b)
{
	const char* srcs[2] = { a, b };
	GLuint sh = glCreateShader(type);
	glShaderSource(sh, b ? 2 : 1, srcs, NULL);
	vita_shader_compiling(a, b);
	glCompileShader(sh);
	vita_shader_compiling(NULL, NULL);
	return sh;
}

static Program* build(const FragKey* k, u32 h)
{
	if (s_nprogs >= MAX_PROGRAMS) {
		vita_log("shader cache full, starting over");
		for (int i = 0; i < s_nprogs; i++)
			if (s_progs[i].prog)
				glDeleteProgram(s_progs[i].prog);
		s_nprogs = 0;
	}
	Program* p = &s_progs[s_nprogs++];
	memset(p, 0, sizeof *p);
	p->key = *k;
	p->hash = h;

	char* fs = gen_fragment(k);
	int id = k->shbin > 0 && k->shbin < SHBIN_COUNT ? k->shbin : SHBIN_WORLD;
	GLuint vs_sh = compile(GL_VERTEX_SHADER, VS_COMMON, VS_BODY[id]);
	GLuint fs_sh = compile(GL_FRAGMENT_SHADER, fs, NULL);
	p->prog = glCreateProgram();
	glAttachShader(p->prog, vs_sh);
	glAttachShader(p->prog, fs_sh);
	static const char* ATTR[5] = { "a_v0", "a_v1", "a_v2", "a_v3", "a_v4" };
	for (int i = 0; i < 5; i++)
		glBindAttribLocation(p->prog, i, ATTR[i]);
	glLinkProgram(p->prog);
	GLint ok = 0;
	glGetProgramiv(p->prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		vita_log("shader link failed (program %d); fragment source:\n%s", id, fs);
		p->failed = true;
	}
	free(fs);

	GLuint g = p->prog;
	p->u_c = glGetUniformLocation(g, "u_c");
	p->u_depth = glGetUniformLocation(g, "u_depth");
	p->u_tex[0] = glGetUniformLocation(g, "u_tex0");
	p->u_tex[1] = glGetUniformLocation(g, "u_tex1");
	p->u_tex[2] = glGetUniformLocation(g, "u_tex2");
	p->u_lut = glGetUniformLocation(g, "u_lut");
	p->u_tflip = glGetUniformLocation(g, "u_tflip");
	p->u_env_color = glGetUniformLocation(g, "u_env_color");
	p->u_buf_color = glGetUniformLocation(g, "u_buf_color");
	p->u_alpha_ref = glGetUniformLocation(g, "u_alpha_ref");
	p->u_fog_color = glGetUniformLocation(g, "u_fog_color");
	p->u_fog_row = glGetUniformLocation(g, "u_fog_row");
	p->u_pt_rows = glGetUniformLocation(g, "u_pt_rows");
	p->u_pt_offset = glGetUniformLocation(g, "u_pt_offset");
	p->u_scene_amb = glGetUniformLocation(g, "u_scene_amb");
	p->u_lpos = glGetUniformLocation(g, "u_lpos");
	p->u_ldiff = glGetUniformLocation(g, "u_ldiff");
	p->u_lamb = glGetUniformLocation(g, "u_lamb");
	p->u_lspec = glGetUniformLocation(g, "u_lspec");
	p->u_lspot = glGetUniformLocation(g, "u_lspot");
	p->u_lda = glGetUniformLocation(g, "u_lda");
	p->u_lrows = glGetUniformLocation(g, "u_lrows");
	p->u_d0_row = glGetUniformLocation(g, "u_d0_row");
	return p;
}

static Program* program_for_state(void)
{
	FragKey k;
	make_key(&k);
	u32 h = hash_key(&k);
	for (int i = 0; i < s_nprogs; i++)
		if (s_progs[i].hash == h && !memcmp(&s_progs[i].key, &k, sizeof k))
			return &s_progs[i];
	return build(&k, h);
}

void c3d_shader_init(void) { s_nprogs = 0; }

// ======================================================================= per-draw state
static GLenum gl_test(int f)
{
	static const GLenum T[8] = { GL_NEVER, GL_ALWAYS, GL_EQUAL, GL_NOTEQUAL, GL_LESS, GL_LEQUAL, GL_GREATER, GL_GEQUAL };
	return T[f & 7];
}

static GLenum gl_stencil_op(int op)
{
	static const GLenum O[8] = { GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT, GL_INCR_WRAP, GL_DECR_WRAP };
	return O[op & 7];
}

static GLenum gl_blend_eq(int e)
{
	switch (e) {
	case GPU_BLEND_SUBTRACT: return GL_FUNC_SUBTRACT;
	case GPU_BLEND_REVERSE_SUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
#ifdef GL_MIN
	case GPU_BLEND_MIN: return GL_MIN;
	case GPU_BLEND_MAX: return GL_MAX;
#endif
	default: return GL_FUNC_ADD;
	}
}

static GLenum gl_blend_factor(int f)
{
	static const GLenum F[15] = {
		GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
		GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA,
		GL_ONE, GL_ZERO, GL_ONE, GL_ZERO,   // constant colour / alpha: GXM has none (docs/vita/NOTAS.md)
		GL_SRC_ALPHA_SATURATE,
	};
	return f >= 0 && f < 15 ? F[f] : GL_ONE;
}

static void rgba_of(u32 c, float out[4])
{
	// 0xAABBGGRR, as citro3d packs colours
	out[0] = (c & 0xFF) / 255.0f;
	out[1] = ((c >> 8) & 0xFF) / 255.0f;
	out[2] = ((c >> 16) & 0xFF) / 255.0f;
	out[3] = ((c >> 24) & 0xFF) / 255.0f;
}

extern float c3d_fog_value(const C3D_FogLut* lut, int i);
extern float c3d_light_lut_value(const C3D_LightLut* lut, int i);
extern int c3d_lut_row_rgba(const void* key, const u32* colors, int n);
extern float c3d_lut_v(int row);

static float light_lut_row(const C3D_LightLut* lut)
{
	// signed tables are reordered to run -1..1 across the row (the shader maps x to x * 0.5 + 0.5)
	float v[256];
	for (int j = 0; j < 256; j++)
		v[j] = c3d_light_lut_value(lut, lut->is_signed ? (j >= 128 ? j - 128 : j + 128) : j);
	return c3d_lut_v(c3d_lut_row(lut, v, 256));
}

static void set_fragment_uniforms(Program* p)
{
	float env[6][4];
	for (int i = 0; i < 6; i++)
		rgba_of(g_c3d.env[i].color, env[i]);
	glUniform4fv(p->u_env_color, 6, &env[0][0]);
	float buf[4];
	rgba_of(g_c3d.env_buf_color, buf);
	glUniform4fv(p->u_buf_color, 1, buf);
	glUniform1f(p->u_alpha_ref, (float)(g_c3d.alpha_ref & 0xFF));

	float flip[3];
	for (int u = 0; u < 3; u++)
		flip[u] = g_c3d.tex[u] && !g_c3d.tex[u]->gl_rt ? 1.0f : 0.0f;
	glUniform3fv(p->u_tflip, 1, flip);

	if (p->key.fog && g_c3d.fog_lut) {
		float v[128];
		for (int i = 0; i < 128; i++)
			v[i] = c3d_fog_value(g_c3d.fog_lut, i);
		glUniform1f(p->u_fog_row, c3d_lut_v(c3d_lut_row(g_c3d.fog_lut, v, 128)));
		float fc[4];
		rgba_of(g_c3d.fog_color, fc);
		glUniform3fv(p->u_fog_color, 1, fc);
	}

	if (p->key.proctex) {
		float rows[3] = { 0, 0, 0 };
		for (int m = 0; m < 2; m++) {
			C3D_ProcTexLut* lut = g_c3d.pt_lut[m == 0 ? GPU_LUT_RGBMAP : GPU_LUT_ALPHAMAP];
			if (!lut)
				continue;
			float v[128];
			for (int i = 0; i < 128; i++) {
				u32 b = (*lut)[i];
				memcpy(&v[i], &b, 4);
			}
			rows[m] = c3d_lut_v(c3d_lut_row(lut, v, 128));
		}
		if (g_c3d.pt_color)
			rows[2] = c3d_lut_v(c3d_lut_row_rgba(g_c3d.pt_color, g_c3d.pt_color->color, 256));
		glUniform3fv(p->u_pt_rows, 1, rows);
		float off[2] = { (float)g_c3d.proctex->offset, (float)(g_c3d.proctex->width ? g_c3d.proctex->width - 1 : 0) };
		glUniform2fv(p->u_pt_offset, 1, off);
	}

	const C3D_LightEnv* le = g_c3d.light_env;
	if (p->key.lighting && le) {
		const C3D_Material* m = &le->material;
		float amb[3];
		for (int c = 0; c < 3; c++)
			amb[c] = m->emission[c] + m->ambient[c] * le->ambient[c];
		glUniform3fv(p->u_scene_amb, 1, amb);
		float pos[8][3] = { { 0 } }, dif[8][3] = { { 0 } }, lam[8][3] = { { 0 } }, spe[8][3] = { { 0 } }, spot[8][3] = { { 0 } };
		float da[8][2] = { { 0 } }, rows[8][2] = { { 0 } };
		for (int i = 0; i < GPU_LIGHTS; i++) {
			const C3D_Light* l = le->lights[i];
			if (!l || !l->enabled)
				continue;
			for (int c = 0; c < 3; c++) {
				pos[i][c] = l->position[c];
				dif[i][c] = l->diffuse[c] * m->diffuse[c];
				lam[i][c] = l->color[c] * m->ambient[c];
				spe[i][c] = l->specular0[c] * m->specular0[c];
				spot[i][c] = l->spot_dir[c];
			}
			if (l->spot_enabled && l->spot_lut)
				rows[i][0] = light_lut_row(l->spot_lut);
			if (l->da_enabled && l->da_lut) {
				rows[i][1] = light_lut_row(&l->da_lut->lut);
				da[i][0] = l->da_lut->scale;
				da[i][1] = l->da_lut->bias;
			}
		}
		glUniform3fv(p->u_lpos, 8, &pos[0][0]);
		glUniform3fv(p->u_ldiff, 8, &dif[0][0]);
		glUniform3fv(p->u_lamb, 8, &lam[0][0]);
		glUniform3fv(p->u_lspec, 8, &spe[0][0]);
		glUniform3fv(p->u_lspot, 8, &spot[0][0]);
		glUniform2fv(p->u_lda, 8, &da[0][0]);
		glUniform2fv(p->u_lrows, 8, &rows[0][0]);
		if (p->key.d0_on && le->luts[GPU_LUT_D0])
			glUniform1f(p->u_d0_row, light_lut_row(le->luts[GPU_LUT_D0]));
	}
}

static void apply_fixed_function(void)
{
	// viewport and scissor, in the target's own coordinates
	glViewport(g_c3d.vp_x, g_c3d.vp_y, g_c3d.vp_w, g_c3d.vp_h);
	if (g_c3d.scissor_mode == GPU_SCISSOR_NORMAL) {
		glEnable(GL_SCISSOR_TEST);
		glScissor((GLint)g_c3d.sc_l, (GLint)g_c3d.sc_t, (GLsizei)(g_c3d.sc_r - g_c3d.sc_l), (GLsizei)(g_c3d.sc_b - g_c3d.sc_t));
	} else {
		glDisable(GL_SCISSOR_TEST);
	}

	// depth: the 3DS writes depth even with the test off when the mask asks for it
	bool depth_write = (g_c3d.write_mask & GPU_WRITE_DEPTH) != 0;
	if (g_c3d.depth_test || depth_write) {
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(g_c3d.depth_test ? gl_test(g_c3d.depth_func) : GL_ALWAYS);
	} else {
		glDisable(GL_DEPTH_TEST);
	}
	glDepthMask(depth_write ? GL_TRUE : GL_FALSE);
	u32 m = g_c3d.write_mask;
	glColorMask((m & GPU_WRITE_RED) != 0, (m & GPU_WRITE_GREEN) != 0, (m & GPU_WRITE_BLUE) != 0, (m & GPU_WRITE_ALPHA) != 0);

	if (g_c3d.stencil_test) {
		glEnable(GL_STENCIL_TEST);
		glStencilFunc(gl_test(g_c3d.stencil_func), g_c3d.stencil_ref, (GLuint)g_c3d.stencil_in_mask);
		glStencilOp(gl_stencil_op(g_c3d.stencil_fail), gl_stencil_op(g_c3d.stencil_zfail), gl_stencil_op(g_c3d.stencil_pass));
		glStencilMask((GLuint)g_c3d.stencil_write_mask);
	} else {
		glDisable(GL_STENCIL_TEST);
	}

	if (g_c3d.logic_op) {
		glDisable(GL_BLEND);
	} else {
		glEnable(GL_BLEND);
		glBlendEquationSeparate(gl_blend_eq(g_c3d.blend_eq_rgb), gl_blend_eq(g_c3d.blend_eq_a));
		glBlendFuncSeparate(gl_blend_factor(g_c3d.blend_src_rgb), gl_blend_factor(g_c3d.blend_dst_rgb),
		                    gl_blend_factor(g_c3d.blend_src_a), gl_blend_factor(g_c3d.blend_dst_a));
	}

	switch (g_c3d.cull) {
	case GPU_CULL_NONE: glDisable(GL_CULL_FACE); break;
	case GPU_CULL_FRONT_CCW: glEnable(GL_CULL_FACE); glFrontFace(GL_CCW); glCullFace(GL_FRONT); break;
	default: glEnable(GL_CULL_FACE); glFrontFace(GL_CCW); glCullFace(GL_BACK); break;
	}
}

static void bind_vertex_input(void)
{
	static const GLenum TYPE[4] = { GL_BYTE, GL_UNSIGNED_BYTE, GL_SHORT, GL_FLOAT };
	static const int SIZE[4] = { 1, 1, 2, 4 };
	const C3D_AttrInfo* ai = &g_c3d.attr;
	const C3D_BufInfo* bi = &g_c3d.buf;
	u16 loaded = 0;

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	for (int b = 0; b < bi->bufCount; b++) {
		const u8* base = bi->ptr[b];
		int off = 0;
		for (int k = 0; k < bi->attrCount[b]; k++) {
			int a = (int)((bi->permutation[b] >> (4 * k)) & 0xF);
			if (a >= 0xC) {   // padding of 4, 8, 12 or 16 bytes
				off += (a - 0xB) * 4;
				continue;
			}
			if (a >= ai->loaderCount)
				continue;
			int fmt = ai->loader_fmt[a] & 3, cnt = ai->loader_count[a], reg = ai->loader_reg[a];
			int esz = SIZE[fmt];
			off = (off + esz - 1) / esz * esz;   // each attribute aligned to its element size
			if (reg < 5) {
				glEnableVertexAttribArray(reg);
				glVertexAttribPointer(reg, cnt, TYPE[fmt], GL_FALSE, bi->stride[b], base + off);
				loaded |= (u16)(1u << reg);
			}
			off += esz * cnt;
		}
	}
	for (int r = 0; r < 5; r++) {
		if (loaded & (1u << r))
			continue;
		glDisableVertexAttribArray(r);
		const C3D_FVec* f = &g_c3d.fixed[r];
		glVertexAttrib4f(r, f->x, f->y, f->z, f->w);
	}
}

static bool prepare(void)
{
	if (!g_c3d.target || !g_c3d.program)
		return false;
	Program* p = program_for_state();
	if (p->failed)
		return false;
	glUseProgram(p->prog);

	// uniform registers, in GL's (x, y, z, w) order
	static float regs[C3D_FVUNIF_COUNT][4];
	for (int i = 0; i < C3D_FVUNIF_COUNT; i++) {
		const C3D_FVec* v = &C3D_FVUnif[GPU_VERTEX_SHADER][i];
		regs[i][0] = v->x; regs[i][1] = v->y; regs[i][2] = v->z; regs[i][3] = v->w;
	}
	glUniform4fv(p->u_c, C3D_FVUNIF_COUNT, &regs[0][0]);
	float depth[2] = { 2.0f * g_c3d.z_scale, 2.0f * g_c3d.z_offset - 1.0f };
	glUniform2fv(p->u_depth, 1, depth);

	// texture units 0-2, and the LUT atlas on unit 3
	for (int u = 0; u < 3; u++) {
		glActiveTexture(GL_TEXTURE0 + u);
		C3D_Tex* t = g_c3d.tex[u];
		if (t) {
			c3d_tex_upload_if_dirty(t);
			glBindTexture(GL_TEXTURE_2D, t->gl_tex);
		} else {
			glBindTexture(GL_TEXTURE_2D, 0);
		}
		glUniform1i(p->u_tex[u], u);
	}
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, c3d_lut_texture());
	glUniform1i(p->u_lut, 3);
	glActiveTexture(GL_TEXTURE0);

	set_fragment_uniforms(p);
	apply_fixed_function();
	bind_vertex_input();
	return true;
}

void c3d_draw_prepare(void) { prepare(); }

static GLenum gl_prim(GPU_Primitive_t p)
{
	switch (p) {
	case GPU_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
	case GPU_TRIANGLE_FAN: return GL_TRIANGLE_FAN;
	default: return GL_TRIANGLES;
	}
}

void C3D_DrawArrays(GPU_Primitive_t primitive, int first, int size)
{
	if (size <= 0 || !prepare())
		return;
	glDrawArrays(gl_prim(primitive), first, size);
}

void C3D_DrawElements(GPU_Primitive_t primitive, int count, int type, const void* indices)
{
	if (count <= 0 || !prepare())
		return;
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glDrawElements(gl_prim(primitive), count, type == C3D_UNSIGNED_SHORT ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE, indices);
}

void C3D_ImmDrawBegin(GPU_Primitive_t primitive) { (void)primitive; vita_log("C3D_ImmDrawBegin: not implemented"); }
void C3D_ImmSendAttrib(float x, float y, float z, float w) { (void)x; (void)y; (void)z; (void)w; }
void C3D_ImmDrawEnd(void) {}
