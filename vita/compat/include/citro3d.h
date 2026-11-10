// sh2-vita: stand-in for citro3d, the 3DS's GPU library, on vitaGL.
//
// Same names, types and memory layouts as citro3d where the game depends on them (C3D_FVec's
// reversed w,z,y,x order, C3D_Mtx rows, C3D_Tex's public fields). The fixed-function parts of the
// 3DS GPU (texture combiners, fragment lighting, fog, procedural textures) are recorded as state and
// turned into GLSL by vita/compat/src/c3d_shadergen.c when something is drawn.
#pragma once

#include <3ds.h>
#include <math.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ======================================================================================== maths
typedef union {
	struct { float w, z, y, x; };
	struct { float r, k, j, i; };
	float c[4];
} C3D_FVec;
typedef C3D_FVec C3D_FQuat;

typedef union {
	C3D_FVec r[4];
	float m[4 * 4];
} C3D_Mtx;

#define C3D_AngleFromDegrees(_angle) ((_angle) * (float)M_TAU / 360.0f)
#ifndef M_TAU
#define M_TAU (2 * M_PI)
#endif

static inline C3D_FVec FVec4_New(float x, float y, float z, float w) { C3D_FVec v = { .x = x, .y = y, .z = z, .w = w }; return v; }
static inline C3D_FVec FVec3_New(float x, float y, float z) { return FVec4_New(x, y, z, 0.0f); }
static inline float FVec3_Dot(C3D_FVec a, C3D_FVec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float FVec4_Dot(C3D_FVec a, C3D_FVec b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
static inline float FVec3_Magnitude(C3D_FVec v) { return sqrtf(FVec3_Dot(v, v)); }
static inline C3D_FVec FVec3_Scale(C3D_FVec v, float s) { return FVec3_New(v.x * s, v.y * s, v.z * s); }
static inline C3D_FVec FVec4_Scale(C3D_FVec v, float s) { return FVec4_New(v.x * s, v.y * s, v.z * s, v.w * s); }
static inline C3D_FVec FVec3_Normalize(C3D_FVec v) { float m = FVec3_Magnitude(v); return m > 0 ? FVec3_Scale(v, 1.0f / m) : v; }
static inline C3D_FVec FVec3_Add(C3D_FVec a, C3D_FVec b) { return FVec3_New(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline C3D_FVec FVec3_Subtract(C3D_FVec a, C3D_FVec b) { return FVec3_New(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline C3D_FVec FVec3_Negate(C3D_FVec v) { return FVec3_New(-v.x, -v.y, -v.z); }
static inline C3D_FVec FVec4_Add(C3D_FVec a, C3D_FVec b) { return FVec4_New(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
static inline C3D_FVec FVec4_Subtract(C3D_FVec a, C3D_FVec b) { return FVec4_New(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }
static inline C3D_FVec FVec4_Negate(C3D_FVec v) { return FVec4_New(-v.x, -v.y, -v.z, -v.w); }
static inline C3D_FVec FVec3_Cross(C3D_FVec a, C3D_FVec b)
{
	return FVec3_New(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float FVec3_Distance(C3D_FVec a, C3D_FVec b) { return FVec3_Magnitude(FVec3_Subtract(a, b)); }
static inline C3D_FVec FVec4_PerspDivide(C3D_FVec v) { return FVec4_New(v.x / v.w, v.y / v.w, v.z / v.w, 1.0f); }

static inline void Mtx_Zeros(C3D_Mtx* out) { memset(out, 0, sizeof *out); }
static inline void Mtx_Copy(C3D_Mtx* out, const C3D_Mtx* in) { *out = *in; }
static inline void Mtx_Diagonal(C3D_Mtx* out, float x, float y, float z, float w)
{
	Mtx_Zeros(out);
	out->r[0].x = x; out->r[1].y = y; out->r[2].z = z; out->r[3].w = w;
}
static inline void Mtx_Identity(C3D_Mtx* out) { Mtx_Diagonal(out, 1.0f, 1.0f, 1.0f, 1.0f); }

void Mtx_Transpose(C3D_Mtx* out);
void Mtx_Add(C3D_Mtx* out, const C3D_Mtx* lhs, const C3D_Mtx* rhs);
void Mtx_Subtract(C3D_Mtx* out, const C3D_Mtx* lhs, const C3D_Mtx* rhs);
void Mtx_Multiply(C3D_Mtx* out, const C3D_Mtx* a, const C3D_Mtx* b);
float Mtx_Inverse(C3D_Mtx* out);
C3D_FVec Mtx_MultiplyFVec3(const C3D_Mtx* mtx, C3D_FVec v);
C3D_FVec Mtx_MultiplyFVec4(const C3D_Mtx* mtx, C3D_FVec v);
void Mtx_Translate(C3D_Mtx* mtx, float x, float y, float z, bool bRightSide);
void Mtx_Scale(C3D_Mtx* mtx, float x, float y, float z);
void Mtx_Rotate(C3D_Mtx* mtx, C3D_FVec axis, float angle, bool bRightSide);
void Mtx_RotateX(C3D_Mtx* mtx, float angle, bool bRightSide);
void Mtx_RotateY(C3D_Mtx* mtx, float angle, bool bRightSide);
void Mtx_RotateZ(C3D_Mtx* mtx, float angle, bool bRightSide);
void Mtx_Ortho(C3D_Mtx* mtx, float left, float right, float bottom, float top, float near, float far, bool isLeftHanded);
void Mtx_Persp(C3D_Mtx* mtx, float fovy, float aspect, float near, float far, bool isLeftHanded);
void Mtx_PerspStereo(C3D_Mtx* mtx, float fovy, float aspect, float near, float far, float iod, float screen, bool isLeftHanded);
void Mtx_OrthoTilt(C3D_Mtx* mtx, float left, float right, float bottom, float top, float near, float far, bool isLeftHanded);
void Mtx_PerspTilt(C3D_Mtx* mtx, float fovy, float aspect, float near, float far, bool isLeftHanded);
void Mtx_PerspStereoTilt(C3D_Mtx* mtx, float fovy, float aspect, float near, float far, float iod, float screen, bool isLeftHanded);
void Mtx_LookAt(C3D_Mtx* out, C3D_FVec cameraPosition, C3D_FVec cameraTarget, C3D_FVec cameraUpVector, bool isLeftHanded);

#define C3D_AspectRatioTop (400.0f / 240.0f)
#define C3D_AspectRatioBot (320.0f / 240.0f)

// ===================================================================================== GPU enums
typedef enum { GPU_NEAREST = 0, GPU_LINEAR = 1 } GPU_TEXTURE_FILTER_PARAM;
typedef enum { GPU_CLAMP_TO_EDGE = 0, GPU_CLAMP_TO_BORDER = 1, GPU_REPEAT = 2, GPU_MIRRORED_REPEAT = 3 } GPU_TEXTURE_WRAP_PARAM;
typedef enum { GPU_TEX_2D = 0, GPU_TEX_CUBE_MAP = 1 } GPU_TEXTURE_MODE_PARAM;
typedef enum { GPU_TEXFACE_2D = 0 } GPU_TEXFACE;
typedef enum {
	GPU_RGBA8 = 0x0, GPU_RGB8 = 0x1, GPU_RGBA5551 = 0x2, GPU_RGB565 = 0x3, GPU_RGBA4 = 0x4,
	GPU_LA8 = 0x5, GPU_HILO8 = 0x6, GPU_L8 = 0x7, GPU_A8 = 0x8, GPU_LA4 = 0x9, GPU_L4 = 0xA,
	GPU_A4 = 0xB, GPU_ETC1 = 0xC, GPU_ETC1A4 = 0xD,
} GPU_TEXCOLOR;
typedef enum { GPU_RB_RGBA8 = 0, GPU_RB_RGB8 = 1, GPU_RB_RGBA5551 = 2, GPU_RB_RGB565 = 3, GPU_RB_RGBA4 = 4 } GPU_COLORBUF;
typedef enum { GPU_RB_DEPTH16 = 0, GPU_RB_DEPTH24 = 2, GPU_RB_DEPTH24_STENCIL8 = 3 } GPU_DEPTHBUF;

typedef enum {
	GPU_NEVER = 0, GPU_ALWAYS = 1, GPU_EQUAL = 2, GPU_NOTEQUAL = 3,
	GPU_LESS = 4, GPU_LEQUAL = 5, GPU_GREATER = 6, GPU_GEQUAL = 7,
} GPU_TESTFUNC;
typedef enum { GPU_EARLYDEPTH_GEQUAL = 0, GPU_EARLYDEPTH_GREATER = 1, GPU_EARLYDEPTH_LEQUAL = 2, GPU_EARLYDEPTH_LESS = 3 } GPU_EARLYDEPTHFUNC;
typedef enum {
	GPU_WRITE_RED = 0x01, GPU_WRITE_GREEN = 0x02, GPU_WRITE_BLUE = 0x04, GPU_WRITE_ALPHA = 0x08,
	GPU_WRITE_DEPTH = 0x10, GPU_WRITE_COLOR = 0x0F, GPU_WRITE_ALL = 0x1F,
} GPU_WRITEMASK;
typedef enum {
	GPU_STENCIL_KEEP = 0, GPU_STENCIL_ZERO = 1, GPU_STENCIL_REPLACE = 2, GPU_STENCIL_INCR = 3,
	GPU_STENCIL_DECR = 4, GPU_STENCIL_INVERT = 5, GPU_STENCIL_INCR_WRAP = 6, GPU_STENCIL_DECR_WRAP = 7,
} GPU_STENCILOP;
typedef enum {
	GPU_BLEND_ADD = 0, GPU_BLEND_SUBTRACT = 1, GPU_BLEND_REVERSE_SUBTRACT = 2, GPU_BLEND_MIN = 3, GPU_BLEND_MAX = 4,
} GPU_BLENDEQUATION;
typedef enum {
	GPU_ZERO = 0, GPU_ONE = 1, GPU_SRC_COLOR = 2, GPU_ONE_MINUS_SRC_COLOR = 3, GPU_DST_COLOR = 4,
	GPU_ONE_MINUS_DST_COLOR = 5, GPU_SRC_ALPHA = 6, GPU_ONE_MINUS_SRC_ALPHA = 7, GPU_DST_ALPHA = 8,
	GPU_ONE_MINUS_DST_ALPHA = 9, GPU_CONSTANT_COLOR = 10, GPU_ONE_MINUS_CONSTANT_COLOR = 11,
	GPU_CONSTANT_ALPHA = 12, GPU_ONE_MINUS_CONSTANT_ALPHA = 13, GPU_SRC_ALPHA_SATURATE = 14,
} GPU_BLENDFACTOR;
typedef enum { GPU_LOGICOP_CLEAR = 0, GPU_LOGICOP_COPY = 3 } GPU_LOGICOP;
typedef enum { GPU_CULL_NONE = 0, GPU_CULL_FRONT_CCW = 1, GPU_CULL_BACK_CCW = 2 } GPU_CULLMODE;
typedef enum { GPU_BYTE = 0, GPU_UNSIGNED_BYTE = 1, GPU_SHORT = 2, GPU_FLOAT = 3 } GPU_FORMATS;
typedef enum { GPU_VERTEX_SHADER = 0, GPU_GEOMETRY_SHADER = 1 } GPU_SHADER_TYPE;
typedef enum { GPU_TRIANGLES = 0x0000, GPU_TRIANGLE_STRIP = 0x0100, GPU_TRIANGLE_FAN = 0x0200, GPU_GEOMETRY_PRIM = 0x0300 } GPU_Primitive_t;

typedef enum {
	GPU_PRIMARY_COLOR = 0x00, GPU_FRAGMENT_PRIMARY_COLOR = 0x01, GPU_FRAGMENT_SECONDARY_COLOR = 0x02,
	GPU_TEXTURE0 = 0x03, GPU_TEXTURE1 = 0x04, GPU_TEXTURE2 = 0x05, GPU_TEXTURE3 = 0x06,
	GPU_PREVIOUS_BUFFER = 0x0D, GPU_CONSTANT = 0x0E, GPU_PREVIOUS = 0x0F,
} GPU_TEVSRC;
typedef enum {
	GPU_TEVOP_RGB_SRC_COLOR = 0x00, GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR = 0x01,
	GPU_TEVOP_RGB_SRC_ALPHA = 0x02, GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA = 0x03,
	GPU_TEVOP_RGB_SRC_R = 0x04, GPU_TEVOP_RGB_ONE_MINUS_SRC_R = 0x05,
	GPU_TEVOP_RGB_SRC_G = 0x08, GPU_TEVOP_RGB_ONE_MINUS_SRC_G = 0x09,
	GPU_TEVOP_RGB_SRC_B = 0x0C, GPU_TEVOP_RGB_ONE_MINUS_SRC_B = 0x0D,
} GPU_TEVOP_RGB;
typedef enum {
	GPU_TEVOP_A_SRC_ALPHA = 0x00, GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA = 0x01,
	GPU_TEVOP_A_SRC_R = 0x02, GPU_TEVOP_A_ONE_MINUS_SRC_R = 0x03,
	GPU_TEVOP_A_SRC_G = 0x04, GPU_TEVOP_A_ONE_MINUS_SRC_G = 0x05,
	GPU_TEVOP_A_SRC_B = 0x06, GPU_TEVOP_A_ONE_MINUS_SRC_B = 0x07,
} GPU_TEVOP_A;
typedef enum {
	GPU_REPLACE = 0x00, GPU_MODULATE = 0x01, GPU_ADD = 0x02, GPU_ADD_SIGNED = 0x03,
	GPU_INTERPOLATE = 0x04, GPU_SUBTRACT = 0x05, GPU_DOT3_RGB = 0x06, GPU_DOT3_RGBA = 0x07,
	GPU_MULTIPLY_ADD = 0x08, GPU_ADD_MULTIPLY = 0x09,
} GPU_COMBINEFUNC;
typedef enum { GPU_TEVSCALE_1 = 0, GPU_TEVSCALE_2 = 1, GPU_TEVSCALE_4 = 2 } GPU_TEVSCALE;

typedef enum { GPU_NO_FOG = 0, GPU_FOG = 5, GPU_GAS = 7 } GPU_FOGMODE;
typedef enum { GPU_PLAIN_DENSITY = 0, GPU_DEPTH_DENSITY = 1 } GPU_GASMODE;

typedef enum {
	GPU_LUT_D0 = 0, GPU_LUT_D1 = 1, GPU_LUT_SP = 2, GPU_LUT_FR = 3,
	GPU_LUT_RB = 4, GPU_LUT_RG = 5, GPU_LUT_RR = 6, GPU_LUT_DA = 7,
} GPU_LIGHTLUTID;
typedef enum {
	GPU_LUTINPUT_NH = 0, GPU_LUTINPUT_VH = 1, GPU_LUTINPUT_NV = 2,
	GPU_LUTINPUT_LN = 3, GPU_LUTINPUT_SP = 4, GPU_LUTINPUT_CP = 5,
} GPU_LIGHTLUTINPUT;
typedef enum { GPU_LUTSCALER_1x = 0, GPU_LUTSCALER_2x = 1, GPU_LUTSCALER_4x = 2, GPU_LUTSCALER_8x = 3, GPU_LUTSCALER_0_25x = 6, GPU_LUTSCALER_0_5x = 7 } GPU_LIGHTLUTSCALER;
typedef enum { GPU_LUTSELECT_COMMON = 0, GPU_LUTSELECT_SP = 1, GPU_LUTSELECT_DA = 2 } GPU_LIGHTLUTSELECT;
typedef enum { GPU_NO_FRESNEL = 0, GPU_PRI_ALPHA_FRESNEL = 1, GPU_SEC_ALPHA_FRESNEL = 2, GPU_PRI_SEC_ALPHA_FRESNEL = 3 } GPU_FRESNELSEL;
typedef enum { GPU_BUMP_NOT_USED = 0, GPU_BUMP_AS_BUMP = 1, GPU_BUMP_AS_TANG = 2 } GPU_BUMPMODE;
#define GPU_LIGHTS 8

typedef enum { GPU_PT_CLAMP_TO_ZERO = 0, GPU_PT_CLAMP_TO_EDGE = 1, GPU_PT_REPEAT = 2, GPU_PT_MIRRORED_REPEAT = 3, GPU_PT_PULSE = 4 } GPU_PROCTEX_CLAMP;
typedef enum { GPU_PT_U = 0, GPU_PT_U2 = 1, GPU_PT_V = 2, GPU_PT_V2 = 3, GPU_PT_ADD = 4, GPU_PT_ADD2 = 5, GPU_PT_SQRT2 = 6, GPU_PT_MIN = 7, GPU_PT_MAX = 8, GPU_PT_RMAX = 9 } GPU_PROCTEX_MAPFUNC;
typedef enum { GPU_PT_NEAREST = 0, GPU_PT_LINEAR = 1, GPU_PT_NEAREST_MIP_NEAREST = 2, GPU_PT_LINEAR_MIP_NEAREST = 3, GPU_PT_NEAREST_MIP_LINEAR = 4, GPU_PT_LINEAR_MIP_LINEAR = 5 } GPU_PROCTEX_FILTER;
typedef enum { GPU_LUT_NOISE = 0, GPU_LUT_RGBMAP = 2, GPU_LUT_ALPHAMAP = 3, GPU_LUT_COLOR = 4, GPU_LUT_COLORDIF = 5 } GPU_PROCTEX_LUTID;

// ======================================================================================== frame
#define C3D_DEFAULT_CMDBUF_SIZE 0x40000
enum { C3D_UNSIGNED_BYTE = 0, C3D_UNSIGNED_SHORT = 1 };
enum { C3D_FRAME_SYNCDRAW = BIT(0), C3D_FRAME_NONBLOCK = BIT(1) };
typedef enum { C3D_CLEAR_COLOR = BIT(0), C3D_CLEAR_DEPTH = BIT(1), C3D_CLEAR_ALL = C3D_CLEAR_COLOR | C3D_CLEAR_DEPTH } C3D_ClearBits;

bool C3D_Init(size_t cmdBufSize);
void C3D_Fini(void);
float C3D_GetCmdBufUsage(void);
float C3D_FrameRate(float fps);
bool C3D_FrameSync(void);
bool C3D_FrameBegin(u8 flags);
void C3D_FrameEnd(u8 flags);
float C3D_GetDrawingTime(void);
float C3D_GetProcessingTime(void);
// citro3d internal the game calls (gpusync.h) to wait until the GPU has finished
void C3Di_RenderQueueWaitDone(void);
void C3D_SetViewport(u32 x, u32 y, u32 w, u32 h);
void C3D_SetScissor(int mode, u32 left, u32 top, u32 right, u32 bottom);
enum { GPU_SCISSOR_DISABLE = 0, GPU_SCISSOR_INVERT = 1, GPU_SCISSOR_NORMAL = 3 };

// ===================================================================================== textures
typedef struct {
	void* data;                  // the image in the 3DS's tiled layout (CPU side; uploaded on flush)
	GPU_TEXCOLOR fmt : 4;
	size_t size : 28;
	union {
		u32 dim;
		struct { u16 height, width; };
	};
	u32 param;
	u32 border;
	union {
		u32 lodParam;
		struct { u16 lodBias; u8 maxLevel, minLevel; };
	};
	// sh2-vita
	u32 gl_tex;                  // the GL texture name
	u8 gl_dirty;                 // data changed since the last upload
	u8 gl_rt;                    // a render target draws into it: GL owns the pixels
	u8 gl_keep;                  // keep data after upload (the game writes into it)
} C3D_Tex;

typedef struct { void* data[6]; } C3D_TexCube;

typedef struct {
	u16 width, height;
	u8 maxLevel : 4;
	GPU_TEXCOLOR format : 4;
	GPU_TEXTURE_MODE_PARAM type : 3;
	bool onVram : 1;
} C3D_TexInitParams;

bool C3D_TexInitWithParams(C3D_Tex* tex, C3D_TexCube* cube, C3D_TexInitParams p);
static inline bool C3D_TexInit(C3D_Tex* tex, u16 width, u16 height, GPU_TEXCOLOR format)
{
	C3D_TexInitParams p = { width, height, 0, format, GPU_TEX_2D, false };
	return C3D_TexInitWithParams(tex, NULL, p);
}
static inline bool C3D_TexInitMipmap(C3D_Tex* tex, u16 width, u16 height, GPU_TEXCOLOR format)
{
	int lw = 0, lh = 0;
	while ((8 << lw) < width) lw++;
	while ((8 << lh) < height) lh++;
	C3D_TexInitParams p = { width, height, (u8)(lw < lh ? lw : lh), format, GPU_TEX_2D, false };
	return C3D_TexInitWithParams(tex, NULL, p);
}
static inline bool C3D_TexInitVRAM(C3D_Tex* tex, u16 width, u16 height, GPU_TEXCOLOR format)
{
	C3D_TexInitParams p = { width, height, 0, format, GPU_TEX_2D, true };
	return C3D_TexInitWithParams(tex, NULL, p);
}
void* C3D_TexGetImagePtr(C3D_Tex* tex, void* data, int level, u32* size);
void C3D_TexUpload(C3D_Tex* tex, const void* data);
void C3D_TexLoadImage(C3D_Tex* tex, const void* data, GPU_TEXFACE face, int level);
void C3D_TexGenerateMipmap(C3D_Tex* tex, GPU_TEXFACE face);
void C3D_TexBind(int unitId, C3D_Tex* tex);
void C3D_TexFlush(C3D_Tex* tex);
void C3D_TexDelete(C3D_Tex* tex);
void C3D_TexSetFilter(C3D_Tex* tex, GPU_TEXTURE_FILTER_PARAM magFilter, GPU_TEXTURE_FILTER_PARAM minFilter);
void C3D_TexSetFilterMipmap(C3D_Tex* tex, GPU_TEXTURE_FILTER_PARAM filter);
void C3D_TexSetWrap(C3D_Tex* tex, GPU_TEXTURE_WRAP_PARAM wrapS, GPU_TEXTURE_WRAP_PARAM wrapT);
static inline void C3D_TexSetLodBias(C3D_Tex* tex, float lodBias) { tex->lodBias = (u16)(lodBias * 256); }
u32 C3D_TexCalcTotalSize(u32 size, int maxLevel);

void C3D_SyncDisplayTransfer(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 flags);
void C3D_SyncTextureCopy(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 size, u32 flags);
#define GX_BUFFER_DIM(w, h) (((u32)(h) << 16) | ((u32)(w) & 0xFFFF))
#define GX_TRANSFER_FLIP_VERT(x) ((x) << 0)
#define GX_TRANSFER_OUT_TILED(x) ((x) << 1)
#define GX_TRANSFER_RAW_COPY(x) ((x) << 3)
#define GX_TRANSFER_IN_FORMAT(x) ((x) << 8)
#define GX_TRANSFER_OUT_FORMAT(x) ((x) << 12)
#define GX_TRANSFER_SCALING(x) ((x) << 24)
enum { GX_TRANSFER_FMT_RGBA8 = 0, GX_TRANSFER_FMT_RGB8 = 1, GX_TRANSFER_FMT_RGB565 = 2, GX_TRANSFER_FMT_RGB5A1 = 3, GX_TRANSFER_FMT_RGBA4 = 4 };
enum { GX_TRANSFER_SCALE_NO = 0, GX_TRANSFER_SCALE_X = 1, GX_TRANSFER_SCALE_XY = 2 };

// =============================================================================== render targets
typedef struct C3D_FrameBuf_s {
	void* colorBuf;
	void* depthBuf;
	u16 width, height;
	GPU_COLORBUF colorFmt;
	GPU_DEPTHBUF depthFmt;
	bool block32;
	u8 colorMask : 4;
	u8 depthMask : 4;
} C3D_FrameBuf;

typedef int C3D_DEPTHTYPE;   // a GPU_DEPTHBUF, or -1 for no depth buffer
#define C3D_DEPTHTYPE_OK(_x) ((_x) >= 0)

typedef struct C3D_RenderTarget_tag C3D_RenderTarget;
struct C3D_RenderTarget_tag {
	C3D_RenderTarget *next, *prev;
	C3D_FrameBuf frameBuf;
	bool used;
	bool ownsColor, ownsDepth;
	bool linked;
	gfxScreen_t screen;
	gfx3dSide_t side;
	u32 transferFlags;
	// sh2-vita
	u32 gl_fbo, gl_color, gl_depth;
	C3D_Tex* gl_tex;             // set when made from a texture
};

C3D_RenderTarget* C3D_RenderTargetCreate(int width, int height, GPU_COLORBUF colorFmt, C3D_DEPTHTYPE depthFmt);
C3D_RenderTarget* C3D_RenderTargetCreateFromTex(C3D_Tex* tex, GPU_TEXFACE face, int level, C3D_DEPTHTYPE depthFmt);
void C3D_RenderTargetDelete(C3D_RenderTarget* target);
void C3D_RenderTargetSetOutput(C3D_RenderTarget* target, gfxScreen_t screen, gfx3dSide_t side, u32 transferFlags);
void C3D_RenderTargetClear(C3D_RenderTarget* target, C3D_ClearBits clearBits, u32 clearColor, u32 clearDepth);
bool C3D_FrameDrawOn(C3D_RenderTarget* target);
void C3D_FrameSplit(u8 flags);
static inline void C3D_RenderTargetDetachOutput(C3D_RenderTarget* target) { C3D_RenderTargetSetOutput(NULL, target->screen, target->side, 0); }

// ===================================================================================== shaders
// The game embeds its compiled .pica shaders (world_shbin, ...). vita/compat/src/c3d_shader.c
// recognises each by its name tag (vita/shaders/*_shbin.h) and links the matching GLSL program.
typedef struct { u32 id; } DVLE_s;
typedef struct { u32 numDVLE; DVLE_s* DVLE; u32 shbin_id; } DVLB_s;
typedef struct { DVLE_s* dvle; u32 id; } shaderInstance_s;
typedef struct {
	shaderInstance_s* vertexShader;
	shaderInstance_s* geometryShader;
	u32 gl_program;              // sh2-vita: the linked GLSL program (vertex part; fragment is generated)
	u32 shbin_id;
} shaderProgram_s;

DVLB_s* DVLB_ParseFile(u32* shbinData, u32 shbinSize);
void DVLB_Free(DVLB_s* dvlb);
s8 shaderInstanceGetUniformLocation(shaderInstance_s* si, const char* name);
Result shaderProgramInit(shaderProgram_s* sp);
Result shaderProgramFree(shaderProgram_s* sp);
Result shaderProgramSetVsh(shaderProgram_s* sp, DVLE_s* dvle);
void C3D_BindProgram(shaderProgram_s* program);

// The uniform register file: 96 vectors per shader stage, as on the 3DS.
#define C3D_FVUNIF_COUNT 96
extern C3D_FVec C3D_FVUnif[2][C3D_FVUNIF_COUNT];
extern bool C3D_FVUnifDirty[2][C3D_FVUNIF_COUNT];
static inline C3D_FVec* C3D_FVUnifWritePtr(GPU_SHADER_TYPE type, int id, int size)
{
	for (int i = 0; i < size; i++)
		C3D_FVUnifDirty[type][id + i] = true;
	return &C3D_FVUnif[type][id];
}
static inline void C3D_FVUnifMtxNx4(GPU_SHADER_TYPE type, int id, const C3D_Mtx* mtx, int num)
{
	C3D_FVec* p = C3D_FVUnifWritePtr(type, id, num);
	for (int i = 0; i < num; i++)
		p[i] = mtx->r[i];
}
static inline void C3D_FVUnifMtx4x4(GPU_SHADER_TYPE type, int id, const C3D_Mtx* mtx) { C3D_FVUnifMtxNx4(type, id, mtx, 4); }
static inline void C3D_FVUnifMtx3x4(GPU_SHADER_TYPE type, int id, const C3D_Mtx* mtx) { C3D_FVUnifMtxNx4(type, id, mtx, 3); }
static inline void C3D_FVUnifMtx2x4(GPU_SHADER_TYPE type, int id, const C3D_Mtx* mtx) { C3D_FVUnifMtxNx4(type, id, mtx, 2); }
static inline void C3D_FVUnifSet(GPU_SHADER_TYPE type, int id, float x, float y, float z, float w)
{
	C3D_FVec* p = C3D_FVUnifWritePtr(type, id, 1);
	p->x = x; p->y = y; p->z = z; p->w = w;
}

// ============================================================================= vertex attributes
typedef struct {
	u32 flags[2];
	u64 permutation;
	int attrCount;
	// sh2-vita: the loaders in the order they were added (an attribute index is a loader index),
	// and the registers that take a fixed value instead
	u8 loader_reg[12], loader_fmt[12], loader_count[12];
	int loaderCount;
	u16 fixed_mask;
} C3D_AttrInfo;

void AttrInfo_Init(C3D_AttrInfo* info);
int AttrInfo_AddLoader(C3D_AttrInfo* info, int regId, GPU_FORMATS format, int count);
int AttrInfo_AddFixed(C3D_AttrInfo* info, int regId);
C3D_AttrInfo* C3D_GetAttrInfo(void);
void C3D_SetAttrInfo(C3D_AttrInfo* info);
C3D_FVec* C3D_FixedAttribGetWritePtr(int id);
static inline void C3D_FixedAttribSet(int id, float x, float y, float z, float w)
{
	C3D_FVec* p = C3D_FixedAttribGetWritePtr(id);
	p->x = x; p->y = y; p->z = z; p->w = w;
}

typedef struct {
	u32 base_paddr;
	int bufCount;
	struct { u32 offset; u32 flags[2]; } buffers[12];
	// sh2-vita: the CPU address and stride of each buffer, and which attributes it holds in order
	const void* ptr[12];
	int stride[12];
	int attrCount[12];
	u64 permutation[12];
} C3D_BufInfo;

void BufInfo_Init(C3D_BufInfo* info);
int BufInfo_Add(C3D_BufInfo* info, const void* data, ptrdiff_t stride, int attribCount, u64 permutation);
C3D_BufInfo* C3D_GetBufInfo(void);
void C3D_SetBufInfo(C3D_BufInfo* info);

void C3D_DrawArrays(GPU_Primitive_t primitive, int first, int size);
void C3D_DrawElements(GPU_Primitive_t primitive, int count, int type, const void* indices);
void C3D_ImmDrawBegin(GPU_Primitive_t primitive);
void C3D_ImmSendAttrib(float x, float y, float z, float w);
void C3D_ImmDrawEnd(void);

// =============================================================================== fixed function
void C3D_DepthMap(bool bIsZBuffer, float zScale, float zOffset);
void C3D_CullFace(GPU_CULLMODE mode);
void C3D_StencilTest(bool enable, GPU_TESTFUNC function, int ref, int inputMask, int writeMask);
void C3D_StencilOp(GPU_STENCILOP sfail, GPU_STENCILOP dfail, GPU_STENCILOP pass);
void C3D_BlendingColor(u32 color);
void C3D_EarlyDepthTest(bool enable, GPU_EARLYDEPTHFUNC function, u32 ref);
void C3D_DepthTest(bool enable, GPU_TESTFUNC function, GPU_WRITEMASK writemask);
void C3D_AlphaTest(bool enable, GPU_TESTFUNC function, int ref);
void C3D_AlphaBlend(GPU_BLENDEQUATION colorEq, GPU_BLENDEQUATION alphaEq, GPU_BLENDFACTOR srcClr, GPU_BLENDFACTOR dstClr,
                    GPU_BLENDFACTOR srcAlpha, GPU_BLENDFACTOR dstAlpha);
void C3D_ColorLogicOp(GPU_LOGICOP op);
void C3D_FragOpMode(int mode);
void C3D_FragOpShadow(float scale, float bias);

// ---- texture combiners (TexEnv) -------------------------------------------------------------
typedef struct {
	u16 srcRgb, srcAlpha;
	union {
		u32 opAll;
		struct { u32 opRgb : 12, opAlpha : 12; };
	};
	u16 funcRgb, funcAlpha;
	u32 color;
	u16 scaleRgb, scaleAlpha;
} C3D_TexEnv;

#define GPU_TEVSOURCES(a, b, c) (((a)) | ((b) << 4) | ((c) << 8))
#define GPU_TEVOPERANDS(a, b, c) (((a)) | ((b) << 4) | ((c) << 8))

typedef enum { C3D_RGB = BIT(0), C3D_Alpha = BIT(1), C3D_Both = C3D_RGB | C3D_Alpha } C3D_TexEnvMode;

C3D_TexEnv* C3D_GetTexEnv(int id);
void C3D_SetTexEnv(int id, C3D_TexEnv* env);
void C3D_DirtyTexEnv(C3D_TexEnv* env);
void C3D_TexEnvBufUpdate(int mode, int mask);
void C3D_TexEnvBufColor(u32 color);

static inline void C3D_TexEnvInit(C3D_TexEnv* env)
{
	env->srcRgb = GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0);
	env->srcAlpha = env->srcRgb;
	env->opAll = 0;
	env->funcRgb = GPU_REPLACE;
	env->funcAlpha = env->funcRgb;
	env->color = 0xFFFFFFFF;
	env->scaleRgb = GPU_TEVSCALE_1;
	env->scaleAlpha = GPU_TEVSCALE_1;
	C3D_DirtyTexEnv(env);
}

#ifdef __cplusplus
#define _C3D_DEFAULT(x) = x
#else
#define _C3D_DEFAULT(x)
#endif

static inline void C3D_TexEnvSrc(C3D_TexEnv* env, C3D_TexEnvMode mode, GPU_TEVSRC s1,
                                 GPU_TEVSRC s2 _C3D_DEFAULT(GPU_PRIMARY_COLOR), GPU_TEVSRC s3 _C3D_DEFAULT(GPU_PRIMARY_COLOR))
{
	int param = GPU_TEVSOURCES((int)s1, (int)s2, (int)s3);
	if ((int)mode & C3D_RGB) env->srcRgb = param;
	if ((int)mode & C3D_Alpha) env->srcAlpha = param;
	C3D_DirtyTexEnv(env);
}
static inline void C3D_TexEnvOpRgb(C3D_TexEnv* env, GPU_TEVOP_RGB o1, GPU_TEVOP_RGB o2 _C3D_DEFAULT(GPU_TEVOP_RGB_SRC_COLOR),
                                   GPU_TEVOP_RGB o3 _C3D_DEFAULT(GPU_TEVOP_RGB_SRC_COLOR))
{
	env->opRgb = GPU_TEVOPERANDS((int)o1, (int)o2, (int)o3);
	C3D_DirtyTexEnv(env);
}
static inline void C3D_TexEnvOpAlpha(C3D_TexEnv* env, GPU_TEVOP_A o1, GPU_TEVOP_A o2 _C3D_DEFAULT(GPU_TEVOP_A_SRC_ALPHA),
                                     GPU_TEVOP_A o3 _C3D_DEFAULT(GPU_TEVOP_A_SRC_ALPHA))
{
	env->opAlpha = GPU_TEVOPERANDS((int)o1, (int)o2, (int)o3);
	C3D_DirtyTexEnv(env);
}
static inline void C3D_TexEnvFunc(C3D_TexEnv* env, C3D_TexEnvMode mode, GPU_COMBINEFUNC param)
{
	if ((int)mode & C3D_RGB) env->funcRgb = param;
	if ((int)mode & C3D_Alpha) env->funcAlpha = param;
	C3D_DirtyTexEnv(env);
}
static inline void C3D_TexEnvColor(C3D_TexEnv* env, u32 color)
{
	env->color = color;
	C3D_DirtyTexEnv(env);
}
static inline void C3D_TexEnvScale(C3D_TexEnv* env, int mode, GPU_TEVSCALE param)
{
	if (mode & C3D_RGB) env->scaleRgb = param;
	if (mode & C3D_Alpha) env->scaleAlpha = param;
	C3D_DirtyTexEnv(env);
}

// ---- fog and gas ----------------------------------------------------------------------------
// sh2-vita: data holds the 128 fog factors as float bits (index = depth * 128)
typedef struct { u32 data[128]; } C3D_FogLut;
typedef struct { u32 diff[8]; u32 color[8]; } C3D_GasLut;

void FogLut_FromArray(C3D_FogLut* lut, const float data[256]);
void FogLut_Exp(C3D_FogLut* lut, float density, float gradient, float near, float far);
static inline float FogLut_CalcZ(float depth, float near, float far) { return far * near / (depth * (far - near) + near); }
void C3D_FogGasMode(GPU_FOGMODE fogMode, GPU_GASMODE gasMode, bool zFlip);
void C3D_FogColor(u32 color);
void C3D_FogLutBind(C3D_FogLut* lut);

// ---- procedural textures (texture unit 3) ---------------------------------------------------
typedef struct {
	u32 color;
	union { u32 proctex0; struct { u32 uClamp : 3, vClamp : 3, rgbFunc : 4, alphaFunc : 4; bool alphaSeparate : 1; bool enableNoise : 1; u32 uShift : 2, vShift : 2; u32 lodBiasLow : 8; }; };
	union { u32 proctex1; struct { u16 uNoiseAmpl; u16 uNoisePhase; }; };
	union { u32 proctex2; struct { u16 vNoiseAmpl; u16 vNoisePhase; }; };
	union { u32 proctex3; struct { u16 uNoiseFreq; u16 vNoiseFreq; }; };
	union { u32 proctex4; struct { u32 minFilter : 3; u32 unknown1 : 8; u32 width : 8; u32 lodBiasHigh : 8; }; };
	union { u32 proctex5; struct { u32 offset : 8; u32 unknown2 : 24; }; };
} C3D_ProcTex;
typedef u32 C3D_ProcTexLut[128];
typedef struct { u32 color[256]; u32 diff[256]; } C3D_ProcTexColorLut;

void C3D_ProcTexInit(C3D_ProcTex* pt, int offset, int length);
static inline void C3D_ProcTexClamp(C3D_ProcTex* pt, GPU_PROCTEX_CLAMP u, GPU_PROCTEX_CLAMP v) { pt->uClamp = u; pt->vClamp = v; }
static inline void C3D_ProcTexCombiner(C3D_ProcTex* pt, bool separate, GPU_PROCTEX_MAPFUNC rgb, GPU_PROCTEX_MAPFUNC alpha)
{
	pt->alphaSeparate = separate; pt->rgbFunc = rgb;
	if (separate) pt->alphaFunc = alpha;
}
static inline void C3D_ProcTexFilter(C3D_ProcTex* pt, GPU_PROCTEX_FILTER min) { pt->minFilter = min; }
void C3D_ProcTexBind(int texCoordId, C3D_ProcTex* pt);
void C3D_ProcTexLutBind(GPU_PROCTEX_LUTID id, C3D_ProcTexLut* lut);
void ProcTexLut_FromArray(C3D_ProcTexLut* lut, const float in[129]);
void C3D_ProcTexColorLutBind(C3D_ProcTexColorLut* lut);
void ProcTexColorLut_Write(C3D_ProcTexColorLut* out, const u32* in, int offset, int length);

// ---- fragment lighting ----------------------------------------------------------------------
// sh2-vita: data holds 256 floats (as bits). Signed tables cover -1..1 (entries 0..127 for 0..1,
// 128..255 for -1..0, as the 3DS indexes them); unsigned ones cover 0..1.
typedef struct { u32 data[256]; u8 is_signed; } C3D_LightLut;
typedef struct { C3D_LightLut lut; float bias, scale; } C3D_LightLutDA;
typedef float (*C3D_LightLutFunc)(float x, float param);
typedef float (*C3D_LightLutFuncDA)(float dist, float arg0, float arg1);

static inline float quadratic_dist_attn(float dist, float linear, float quad) { return 1.0f / (1.0f + linear * dist + quad * dist * dist); }
static inline float spot_step(float angle, float cutoff) { return angle >= cutoff ? 1.0f : 0.0f; }

void LightLut_FromArray(C3D_LightLut* lut, float* data);
void LightLut_FromFunc(C3D_LightLut* lut, C3D_LightLutFunc func, float param, bool negative);
void LightLutDA_Create(C3D_LightLutDA* lut, C3D_LightLutFuncDA func, float from, float to, float arg0, float arg1);
#define LightLut_Phong(lut, shininess) LightLut_FromFunc((lut), powf, (shininess), false)
#define LightLut_Spotlight(lut, angle) LightLut_FromFunc((lut), spot_step, cosf(angle), true)
#define LightLutDA_Quadratic(lut, from, to, linear, quad) LightLutDA_Create((lut), quadratic_dist_attn, (from), (to), (linear), (quad))

typedef struct {
	float ambient[3];
	float diffuse[3];
	float specular0[3];
	float specular1[3];
	float emission[3];
} C3D_Material;

typedef struct C3D_LightEnv_t C3D_LightEnv;

typedef struct {
	u8 enabled;
	u8 spot_enabled;
	u8 da_enabled;
	u8 directional;              // position.w == 0
	float diffuse[3], specular0[3], specular1[3], color[3];
	float position[3];
	float spot_dir[3];
	C3D_LightLut* spot_lut;
	C3D_LightLutDA* da_lut;
	C3D_LightEnv* parent;
	int id;
} C3D_Light;

struct C3D_LightEnv_t {
	C3D_Light* lights[GPU_LIGHTS];
	C3D_Material material;
	float ambient[3];
	C3D_LightLut* luts[6];       // D0, D1, (SP per light), FR, RB, RG, RR -> indexed by GPU_LIGHTLUTID < 6
	u8 lut_input[6];
	u8 lut_abs[6];
	GPU_FRESNELSEL fresnel;
	u32 serial;                  // bumped on change, so the shader generator can cache
};

void C3D_LightEnvInit(C3D_LightEnv* env);
void C3D_LightEnvBind(C3D_LightEnv* env);
void C3D_LightEnvMaterial(C3D_LightEnv* env, const C3D_Material* mtl);
void C3D_LightEnvAmbient(C3D_LightEnv* env, float r, float g, float b);
void C3D_LightEnvLut(C3D_LightEnv* env, GPU_LIGHTLUTID lutId, GPU_LIGHTLUTINPUT input, bool negative, C3D_LightLut* lut);
void C3D_LightEnvFresnel(C3D_LightEnv* env, GPU_FRESNELSEL selector);
void C3D_LightEnvBumpMode(C3D_LightEnv* env, GPU_BUMPMODE mode);
void C3D_LightEnvBumpSel(C3D_LightEnv* env, int texUnit);
void C3D_LightEnvShadowMode(C3D_LightEnv* env, u32 mode);
void C3D_LightEnvClampHighlights(C3D_LightEnv* env, bool clamp);

int C3D_LightInit(C3D_Light* light, C3D_LightEnv* env);
void C3D_LightEnable(C3D_Light* light, bool enable);
void C3D_LightTwoSideDiffuse(C3D_Light* light, bool enable);
void C3D_LightGeoFactor(C3D_Light* light, int id, bool enable);
void C3D_LightAmbient(C3D_Light* light, float r, float g, float b);
void C3D_LightDiffuse(C3D_Light* light, float r, float g, float b);
void C3D_LightSpecular0(C3D_Light* light, float r, float g, float b);
void C3D_LightSpecular1(C3D_Light* light, float r, float g, float b);
void C3D_LightPosition(C3D_Light* light, C3D_FVec* pos);
void C3D_LightShadowEnable(C3D_Light* light, bool enable);
void C3D_LightSpotEnable(C3D_Light* light, bool enable);
void C3D_LightSpotDir(C3D_Light* light, float x, float y, float z);
void C3D_LightSpotLut(C3D_Light* light, C3D_LightLut* lut);
void C3D_LightDistAttnEnable(C3D_Light* light, bool enable);
void C3D_LightDistAttn(C3D_Light* light, C3D_LightLutDA* lut);
static inline void C3D_LightColor(C3D_Light* light, float r, float g, float b)
{
	C3D_LightDiffuse(light, r, g, b);
	C3D_LightSpecular0(light, r, g, b);
	C3D_LightSpecular1(light, r, g, b);
}

#ifdef __cplusplus
}
#endif
