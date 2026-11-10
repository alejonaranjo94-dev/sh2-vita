// sh2-vita: citro3d's state setters. They only record the 3DS GPU state in g_c3d; c3d_shader.c
// turns it into GL state and a generated fragment shader when something is drawn.
#include "c3d_internal.h"

#include <stdlib.h>
#include <string.h>

C3D_State g_c3d;
C3D_FVec C3D_FVUnif[2][C3D_FVUNIF_COUNT];
bool C3D_FVUnifDirty[2][C3D_FVUNIF_COUNT];

void c3d_state_reset(void)
{
	memset(&g_c3d, 0, sizeof g_c3d);
	for (int i = 0; i < 6; i++)
		C3D_TexEnvInit(&g_c3d.env[i]);
	g_c3d.depth_test = true;
	g_c3d.depth_func = GPU_GREATER;
	g_c3d.write_mask = GPU_WRITE_ALL;
	g_c3d.z_scale = -1.0f;
	g_c3d.z_offset = 0.0f;
	g_c3d.alpha_func = GPU_ALWAYS;
	g_c3d.stencil_func = GPU_ALWAYS;
	g_c3d.blend_eq_rgb = g_c3d.blend_eq_a = GPU_BLEND_ADD;
	g_c3d.blend_src_rgb = g_c3d.blend_src_a = GPU_SRC_ALPHA;
	g_c3d.blend_dst_rgb = g_c3d.blend_dst_a = GPU_ONE_MINUS_SRC_ALPHA;
	g_c3d.cull = GPU_CULL_BACK_CCW;
	g_c3d.frame_rate = 60.0f;
	for (int i = 0; i < 16; i++)
		g_c3d.fixed[i] = FVec4_New(0, 0, 0, 1);
}

// ---- texture combiners ----------------------------------------------------------------------
C3D_TexEnv* C3D_GetTexEnv(int id) { return &g_c3d.env[id]; }
void C3D_SetTexEnv(int id, C3D_TexEnv* env) { g_c3d.env[id] = *env; }
void C3D_DirtyTexEnv(C3D_TexEnv* env) { (void)env; }

void C3D_TexEnvBufUpdate(int mode, int mask)
{
	// mask bit i: the output of stage i goes to the combiner buffer (stages 0..3)
	if (mode & C3D_RGB) g_c3d.env_buf_rgb = (u8)(mask & 0xF);
	if (mode & C3D_Alpha) g_c3d.env_buf_alpha = (u8)(mask & 0xF);
}

void C3D_TexEnvBufColor(u32 color) { g_c3d.env_buf_color = color; }

// ---- fragment operations -------------------------------------------------------------------
void C3D_DepthMap(bool bIsZBuffer, float zScale, float zOffset)
{
	(void)bIsZBuffer;
	g_c3d.z_scale = zScale;
	g_c3d.z_offset = zOffset;
}

void C3D_CullFace(GPU_CULLMODE mode) { g_c3d.cull = mode; }

void C3D_StencilTest(bool enable, GPU_TESTFUNC function, int ref, int inputMask, int writeMask)
{
	g_c3d.stencil_test = enable;
	g_c3d.stencil_func = function;
	g_c3d.stencil_ref = ref;
	g_c3d.stencil_in_mask = inputMask;
	g_c3d.stencil_write_mask = writeMask;
}

void C3D_StencilOp(GPU_STENCILOP sfail, GPU_STENCILOP dfail, GPU_STENCILOP pass)
{
	g_c3d.stencil_fail = sfail;
	g_c3d.stencil_zfail = dfail;
	g_c3d.stencil_pass = pass;
}

void C3D_BlendingColor(u32 color) { g_c3d.blend_color = color; }
void C3D_EarlyDepthTest(bool enable, GPU_EARLYDEPTHFUNC function, u32 ref) { (void)enable; (void)function; (void)ref; }

void C3D_DepthTest(bool enable, GPU_TESTFUNC function, GPU_WRITEMASK writemask)
{
	g_c3d.depth_test = enable;
	g_c3d.depth_func = function;
	g_c3d.write_mask = writemask;
}

void C3D_AlphaTest(bool enable, GPU_TESTFUNC function, int ref)
{
	g_c3d.alpha_test = enable;
	g_c3d.alpha_func = function;
	g_c3d.alpha_ref = ref;
}

void C3D_AlphaBlend(GPU_BLENDEQUATION colorEq, GPU_BLENDEQUATION alphaEq, GPU_BLENDFACTOR srcClr, GPU_BLENDFACTOR dstClr,
                    GPU_BLENDFACTOR srcAlpha, GPU_BLENDFACTOR dstAlpha)
{
	g_c3d.logic_op = false;
	g_c3d.blend_eq_rgb = colorEq;
	g_c3d.blend_eq_a = alphaEq;
	g_c3d.blend_src_rgb = srcClr;
	g_c3d.blend_dst_rgb = dstClr;
	g_c3d.blend_src_a = srcAlpha;
	g_c3d.blend_dst_a = dstAlpha;
}

void C3D_ColorLogicOp(GPU_LOGICOP op)
{
	(void)op;
	g_c3d.logic_op = true;
}

void C3D_FragOpMode(int mode) { (void)mode; }
void C3D_FragOpShadow(float scale, float bias) { (void)scale; (void)bias; }

// ---- fog ------------------------------------------------------------------------------------
static float bits_to_float(u32 b) { float f; memcpy(&f, &b, 4); return f; }
static u32 float_to_bits(float f) { u32 b; memcpy(&b, &f, 4); return b; }

void FogLut_FromArray(C3D_FogLut* lut, const float data[256])
{
	// data[0..127]: the factor at depth i/128; data[128..255]: the step to the next one (unused)
	for (int i = 0; i < 128; i++)
		lut->data[i] = float_to_bits(data[i]);
}

void FogLut_Exp(C3D_FogLut* lut, float density, float gradient, float near, float far)
{
	float data[256];
	for (int i = 0; i <= 128; i++) {
		float x = FogLut_CalcZ(i / 128.0f, near, far);
		float v = expf(-powf(density * x, gradient));
		if (i < 128) data[i] = v;
		if (i > 0) data[i + 127] = v - data[i - 1];
	}
	FogLut_FromArray(lut, data);
}

void C3D_FogGasMode(GPU_FOGMODE fogMode, GPU_GASMODE gasMode, bool zFlip)
{
	(void)gasMode;
	g_c3d.fog_mode = fogMode;
	g_c3d.fog_zflip = zFlip;
}

void C3D_FogColor(u32 color) { g_c3d.fog_color = color; }
void C3D_FogLutBind(C3D_FogLut* lut) { g_c3d.fog_lut = lut; }

float c3d_fog_value(const C3D_FogLut* lut, int i) { return bits_to_float(lut->data[i]); }

// ---- procedural texture ---------------------------------------------------------------------
void C3D_ProcTexInit(C3D_ProcTex* pt, int offset, int length)
{
	memset(pt, 0, sizeof *pt);
	pt->offset = offset;
	pt->width = length;
	pt->uClamp = pt->vClamp = GPU_PT_CLAMP_TO_EDGE;
	pt->rgbFunc = pt->alphaFunc = GPU_PT_U;
	pt->minFilter = GPU_PT_LINEAR;
}

void C3D_ProcTexBind(int texCoordId, C3D_ProcTex* pt)
{
	g_c3d.proctex = pt;
	g_c3d.proctex_coord = texCoordId;
}

void C3D_ProcTexLutBind(GPU_PROCTEX_LUTID id, C3D_ProcTexLut* lut)
{
	if (id >= 0 && id < 6)
		g_c3d.pt_lut[id] = lut;
}

void ProcTexLut_FromArray(C3D_ProcTexLut* lut, const float in[129])
{
	for (int i = 0; i < 128; i++)
		(*lut)[i] = float_to_bits(in[i]);
}

void C3D_ProcTexColorLutBind(C3D_ProcTexColorLut* lut) { g_c3d.pt_color = lut; }

void ProcTexColorLut_Write(C3D_ProcTexColorLut* out, const u32* in, int offset, int length)
{
	if (offset < 0) offset = 0;
	if (offset + length > 256) length = 256 - offset;
	memcpy(&out->color[offset], in, length * sizeof(u32));
}

// ---- lighting LUTs --------------------------------------------------------------------------
void LightLut_FromArray(C3D_LightLut* lut, float* data)
{
	// citro3d's layout: data[0..255] values, data[256..511] differences; values only here
	for (int i = 0; i < 256; i++)
		lut->data[i] = float_to_bits(data[i]);
	lut->is_signed = 0;
}

void LightLut_FromFunc(C3D_LightLut* lut, C3D_LightLutFunc func, float param, bool negative)
{
	for (int i = 0; i < 256; i++) {
		float x;
		if (negative)
			x = (i < 128 ? i : i - 256) / 128.0f;
		else
			x = i / 256.0f;
		lut->data[i] = float_to_bits(func(x, param));
	}
	lut->is_signed = negative;
}

void LightLutDA_Create(C3D_LightLutDA* lut, C3D_LightLutFuncDA func, float from, float to, float arg0, float arg1)
{
	for (int i = 0; i < 256; i++) {
		float d = from + (to - from) * i / 256.0f;
		lut->lut.data[i] = float_to_bits(func(d, arg0, arg1));
	}
	lut->lut.is_signed = 0;
	lut->scale = 1.0f / (to - from);
	lut->bias = -from * lut->scale;
}

float c3d_light_lut_value(const C3D_LightLut* lut, int i) { return bits_to_float(lut->data[i]); }

// ---- lighting environment -------------------------------------------------------------------
static void env_changed(C3D_LightEnv* env) { if (env) env->serial++; }

void C3D_LightEnvInit(C3D_LightEnv* env)
{
	memset(env, 0, sizeof *env);
	for (int i = 0; i < 3; i++) {
		env->material.diffuse[i] = 1.0f;
	}
	env->serial = 1;
}

void C3D_LightEnvBind(C3D_LightEnv* env) { g_c3d.light_env = env; }

void C3D_LightEnvMaterial(C3D_LightEnv* env, const C3D_Material* mtl)
{
	env->material = *mtl;
	env_changed(env);
}

void C3D_LightEnvAmbient(C3D_LightEnv* env, float r, float g, float b)
{
	env->ambient[0] = r; env->ambient[1] = g; env->ambient[2] = b;
	env_changed(env);
}

void C3D_LightEnvLut(C3D_LightEnv* env, GPU_LIGHTLUTID lutId, GPU_LIGHTLUTINPUT input, bool negative, C3D_LightLut* lut)
{
	if (lutId < 0 || lutId >= 6)
		return;
	env->luts[lutId] = lut;
	env->lut_input[lutId] = (u8)input;
	env->lut_abs[lutId] = !negative;
	env_changed(env);
}

void C3D_LightEnvFresnel(C3D_LightEnv* env, GPU_FRESNELSEL selector) { env->fresnel = selector; env_changed(env); }
void C3D_LightEnvBumpMode(C3D_LightEnv* env, GPU_BUMPMODE mode) { (void)env; (void)mode; }
void C3D_LightEnvBumpSel(C3D_LightEnv* env, int texUnit) { (void)env; (void)texUnit; }
void C3D_LightEnvShadowMode(C3D_LightEnv* env, u32 mode) { (void)env; (void)mode; }
void C3D_LightEnvClampHighlights(C3D_LightEnv* env, bool clamp) { (void)env; (void)clamp; }

int C3D_LightInit(C3D_Light* light, C3D_LightEnv* env)
{
	memset(light, 0, sizeof *light);
	light->parent = env;
	for (int i = 0; i < GPU_LIGHTS; i++) {
		if (!env->lights[i]) {
			env->lights[i] = light;
			light->id = i;
			light->enabled = 1;
			for (int c = 0; c < 3; c++)
				light->diffuse[c] = light->specular0[c] = light->specular1[c] = 1.0f;
			light->position[2] = 1.0f;
			light->directional = 1;
			env_changed(env);
			return i;
		}
	}
	return -1;
}

#define LIGHT_SET(field, a, b, c) do { light->field[0] = (a); light->field[1] = (b); light->field[2] = (c); env_changed(light->parent); } while (0)

void C3D_LightEnable(C3D_Light* light, bool enable) { light->enabled = enable; env_changed(light->parent); }
void C3D_LightTwoSideDiffuse(C3D_Light* light, bool enable) { (void)light; (void)enable; }
void C3D_LightGeoFactor(C3D_Light* light, int id, bool enable) { (void)light; (void)id; (void)enable; }
void C3D_LightAmbient(C3D_Light* light, float r, float g, float b) { LIGHT_SET(color, r, g, b); }
void C3D_LightDiffuse(C3D_Light* light, float r, float g, float b) { LIGHT_SET(diffuse, r, g, b); }
void C3D_LightSpecular0(C3D_Light* light, float r, float g, float b) { LIGHT_SET(specular0, r, g, b); }
void C3D_LightSpecular1(C3D_Light* light, float r, float g, float b) { LIGHT_SET(specular1, r, g, b); }

void C3D_LightPosition(C3D_Light* light, C3D_FVec* pos)
{
	// view space; w == 0 is a direction (towards the light)
	light->position[0] = pos->x;
	light->position[1] = pos->y;
	light->position[2] = pos->z;
	light->directional = pos->w == 0.0f;
	env_changed(light->parent);
}

void C3D_LightShadowEnable(C3D_Light* light, bool enable) { (void)light; (void)enable; }
void C3D_LightSpotEnable(C3D_Light* light, bool enable) { light->spot_enabled = enable; env_changed(light->parent); }

void C3D_LightSpotDir(C3D_Light* light, float x, float y, float z)
{
	// stored as citro3d does: the normalised opposite, so the spot input is dot(L, spot_dir)
	C3D_FVec d = FVec3_Normalize(FVec3_New(-x, -y, -z));
	light->spot_dir[0] = d.x; light->spot_dir[1] = d.y; light->spot_dir[2] = d.z;
	env_changed(light->parent);
}

void C3D_LightSpotLut(C3D_Light* light, C3D_LightLut* lut) { light->spot_lut = lut; env_changed(light->parent); }
void C3D_LightDistAttnEnable(C3D_Light* light, bool enable) { light->da_enabled = enable; env_changed(light->parent); }
void C3D_LightDistAttn(C3D_Light* light, C3D_LightLutDA* lut) { light->da_lut = lut; env_changed(light->parent); }

// ---- vertex input ---------------------------------------------------------------------------
void AttrInfo_Init(C3D_AttrInfo* info) { memset(info, 0, sizeof *info); }

int AttrInfo_AddLoader(C3D_AttrInfo* info, int regId, GPU_FORMATS format, int count)
{
	if (info->loaderCount >= 12 || regId < 0 || regId > 15)
		return -1;
	int i = info->loaderCount++;
	info->loader_reg[i] = (u8)regId;
	info->loader_fmt[i] = (u8)format;
	info->loader_count[i] = (u8)count;
	info->attrCount = info->loaderCount;
	return i;
}

int AttrInfo_AddFixed(C3D_AttrInfo* info, int regId)
{
	if (regId < 0 || regId > 15)
		return -1;
	info->fixed_mask |= (u16)(1u << regId);
	return regId;
}

C3D_AttrInfo* C3D_GetAttrInfo(void) { return &g_c3d.attr; }
void C3D_SetAttrInfo(C3D_AttrInfo* info) { if (info != &g_c3d.attr) g_c3d.attr = *info; }

C3D_FVec* C3D_FixedAttribGetWritePtr(int id) { return &g_c3d.fixed[id & 15]; }

void BufInfo_Init(C3D_BufInfo* info) { memset(info, 0, sizeof *info); }

int BufInfo_Add(C3D_BufInfo* info, const void* data, ptrdiff_t stride, int attribCount, u64 permutation)
{
	if (info->bufCount >= 12)
		return -1;
	int i = info->bufCount++;
	info->ptr[i] = data;
	info->stride[i] = (int)stride;
	info->attrCount[i] = attribCount;
	info->permutation[i] = permutation;
	return i;
}

C3D_BufInfo* C3D_GetBufInfo(void) { return &g_c3d.buf; }
void C3D_SetBufInfo(C3D_BufInfo* info) { if (info != &g_c3d.buf) g_c3d.buf = *info; }

// ---- viewport and scissor (in the target's own, portrait, coordinates) --------------------
void C3D_SetViewport(u32 x, u32 y, u32 w, u32 h)
{
	g_c3d.vp_x = (int)x; g_c3d.vp_y = (int)y; g_c3d.vp_w = (int)w; g_c3d.vp_h = (int)h;
}

void C3D_SetScissor(int mode, u32 left, u32 top, u32 right, u32 bottom)
{
	g_c3d.scissor_mode = mode;
	g_c3d.sc_l = left; g_c3d.sc_t = top; g_c3d.sc_r = right; g_c3d.sc_b = bottom;
}
