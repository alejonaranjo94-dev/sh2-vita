// sh2-vita: citro3d render targets and frames on vitaGL framebuffers.
//
// Every target keeps the 3DS's portrait size (the top screen is 240 x 400); the game's "Tilt"
// matrices draw into it as on the 3DS. The target set as the top screen's output is shown on the
// Vita's screen at the end of each frame (gfx.c, vita_present).
#include "c3d_internal.h"

#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

C3D_RenderTarget* g_c3d_top_output;
C3D_RenderTarget* g_c3d_bottom_output;

static u64 s_frame_start_us, s_last_swap_us;
static float s_draw_ms, s_proc_ms;

void c3d_state_reset(void);
void vita_gl_init(void);

bool C3D_Init(size_t cmdBufSize)
{
	(void)cmdBufSize;
	vita_gl_init();
	c3d_state_reset();
	c3d_shader_init();
	return true;
}

void C3D_Fini(void) {}
float C3D_GetCmdBufUsage(void) { return 0.0f; }

float C3D_FrameRate(float fps)
{
	float old = g_c3d.frame_rate;
	if (fps > 0)
		g_c3d.frame_rate = fps;
	return old;
}

bool C3D_FrameSync(void) { return true; }
float C3D_GetDrawingTime(void) { return s_draw_ms; }
float C3D_GetProcessingTime(void) { return s_proc_ms; }

bool C3D_FrameBegin(u8 flags)
{
	(void)flags;
	g_c3d.in_frame = true;
	s_frame_start_us = sceKernelGetProcessTimeWide();
	c3d_lut_frame_begin();
	return true;
}

void C3D_FrameEnd(u8 flags)
{
	(void)flags;
	g_c3d.in_frame = false;
	u64 t0 = sceKernelGetProcessTimeWide();
	s_proc_ms = (t0 - s_frame_start_us) / 1000.0f;

	// Pace to the frame rate the game asked for (30 or 60), sleeping rather than spinning.
	u64 period = (u64)(1000000.0f / (g_c3d.frame_rate > 0 ? g_c3d.frame_rate : 60.0f));
	u64 now = sceKernelGetProcessTimeWide();
	if (s_last_swap_us && now - s_last_swap_us < period - 1000)
		sceKernelDelayThread((SceUInt)(period - (now - s_last_swap_us) - 1000));

	vita_present();
	s_last_swap_us = sceKernelGetProcessTimeWide();
	s_draw_ms = (s_last_swap_us - t0) / 1000.0f;
	g_c3d.target = NULL;
}

void C3D_FrameSplit(u8 flags) { (void)flags; }

// ---- targets --------------------------------------------------------------------------------
static bool make_fbo(C3D_RenderTarget* rt, GLuint color_tex, int w, int h, C3D_DEPTHTYPE depthFmt)
{
	glGenFramebuffers(1, &rt->gl_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, rt->gl_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex, 0);
	if (C3D_DEPTHTYPE_OK(depthFmt)) {
		glGenRenderbuffers(1, &rt->gl_depth);
		glBindRenderbuffer(GL_RENDERBUFFER, rt->gl_depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rt->gl_depth);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rt->gl_depth);
	}
	GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (st != GL_FRAMEBUFFER_COMPLETE) {
		vita_log("render target %dx%d incomplete: 0x%X", w, h, st);
		return false;
	}
	rt->frameBuf.width = (u16)w;
	rt->frameBuf.height = (u16)h;
	rt->frameBuf.colorBuf = rt;   // a tag: C3D_SyncTextureCopy / DisplayTransfer recognise it
	rt->frameBuf.depthBuf = C3D_DEPTHTYPE_OK(depthFmt) ? rt : NULL;
	rt->frameBuf.colorMask = 0xF;
	rt->frameBuf.depthMask = 0x3;
	return true;
}

C3D_RenderTarget* C3D_RenderTargetCreate(int width, int height, GPU_COLORBUF colorFmt, C3D_DEPTHTYPE depthFmt)
{
	(void)colorFmt;
	C3D_RenderTarget* rt = calloc(1, sizeof *rt);
	if (!rt)
		return NULL;
	glGenTextures(1, &rt->gl_color);
	glBindTexture(GL_TEXTURE_2D, rt->gl_color);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	rt->ownsColor = true;
	rt->ownsDepth = C3D_DEPTHTYPE_OK(depthFmt);
	if (!make_fbo(rt, rt->gl_color, width, height, depthFmt)) {
		glDeleteTextures(1, &rt->gl_color);
		free(rt);
		return NULL;
	}
	return rt;
}

C3D_RenderTarget* C3D_RenderTargetCreateFromTex(C3D_Tex* tex, GPU_TEXFACE face, int level, C3D_DEPTHTYPE depthFmt)
{
	(void)face; (void)level;
	C3D_RenderTarget* rt = calloc(1, sizeof *rt);
	if (!rt)
		return NULL;
	// The texture's pixels now belong to GL: allocate its GL storage as a colour buffer.
	if (!tex->gl_tex)
		glGenTextures(1, &tex->gl_tex);
	glBindTexture(GL_TEXTURE_2D, tex->gl_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex->width, tex->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	tex->gl_rt = 1;
	tex->gl_dirty = 0;
	rt->gl_tex = tex;
	rt->ownsDepth = C3D_DEPTHTYPE_OK(depthFmt);
	if (!make_fbo(rt, tex->gl_tex, tex->width, tex->height, depthFmt)) {
		free(rt);
		return NULL;
	}
	return rt;
}

void C3D_RenderTargetDelete(C3D_RenderTarget* rt)
{
	if (!rt)
		return;
	if (g_c3d.target == rt)
		g_c3d.target = NULL;
	if (g_c3d_top_output == rt)
		g_c3d_top_output = NULL;
	if (g_c3d_bottom_output == rt)
		g_c3d_bottom_output = NULL;
	if (rt->gl_fbo)
		glDeleteFramebuffers(1, &rt->gl_fbo);
	if (rt->gl_depth)
		glDeleteRenderbuffers(1, &rt->gl_depth);
	if (rt->ownsColor && rt->gl_color)
		glDeleteTextures(1, &rt->gl_color);
	free(rt);
}

void C3D_RenderTargetSetOutput(C3D_RenderTarget* target, gfxScreen_t screen, gfx3dSide_t side, u32 transferFlags)
{
	if (side != GFX_LEFT)
		return;   // the right eye of the 3D effect has no screen on the Vita
	if (screen == GFX_TOP)
		g_c3d_top_output = target;
	else
		g_c3d_bottom_output = target;
	if (target) {
		target->screen = screen;
		target->side = side;
		target->transferFlags = transferFlags;
		target->linked = true;
	}
}

void c3d_target_bind(C3D_RenderTarget* rt)
{
	glBindFramebuffer(GL_FRAMEBUFFER, rt ? rt->gl_fbo : 0);
}

void C3D_RenderTargetClear(C3D_RenderTarget* target, C3D_ClearBits clearBits, u32 clearColor, u32 clearDepth)
{
	if (!target)
		return;
	c3d_target_bind(target);
	GLbitfield bits = 0;
	glDisable(GL_SCISSOR_TEST);
	if (clearBits & C3D_CLEAR_COLOR) {
		// 0xRRGGBBAA
		glClearColor(((clearColor >> 24) & 0xFF) / 255.0f, ((clearColor >> 16) & 0xFF) / 255.0f,
		             ((clearColor >> 8) & 0xFF) / 255.0f, (clearColor & 0xFF) / 255.0f);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		bits |= GL_COLOR_BUFFER_BIT;
	}
	if ((clearBits & C3D_CLEAR_DEPTH) && target->gl_depth) {
		glClearDepthf((clearDepth & 0xFFFFFF) / 16777215.0f);
		glClearStencil((clearDepth >> 24) & 0xFF);
		glDepthMask(GL_TRUE);
		glStencilMask(0xFF);
		bits |= GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
	}
	if (bits)
		glClear(bits);
	c3d_target_bind(g_c3d.target);
}

bool C3D_FrameDrawOn(C3D_RenderTarget* target)
{
	if (!target)
		return false;
	g_c3d.target = target;
	target->used = true;
	c3d_target_bind(target);
	C3D_SetViewport(0, 0, target->frameBuf.width, target->frameBuf.height);
	return true;
}

// ---- copies from a target ---------------------------------------------------------------------
static C3D_RenderTarget* target_of(const void* colorBuf)
{
	// colorBuf of a compat target points at the target itself (make_fbo)
	C3D_RenderTarget* rt = (C3D_RenderTarget*)colorBuf;
	return rt && rt->frameBuf.colorBuf == rt ? rt : NULL;
}

// The game's screen grabs (the game-over still): copies the target into the texture whose data is
// outadr. The texture then keeps GL's copy (gl_rt) and is sampled as a render target would be.
extern C3D_Tex* c3d_tex_find_by_data(const void* data);

void C3D_SyncTextureCopy(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 size, u32 flags)
{
	(void)indim; (void)outdim; (void)size; (void)flags;
	C3D_RenderTarget* rt = target_of(inadr);
	C3D_Tex* tex = c3d_tex_find_by_data(outadr);
	if (!rt || !tex) {
		vita_log("C3D_SyncTextureCopy: unsupported copy (%p -> %p)", (void*)inadr, (void*)outadr);
		return;
	}
	if (!tex->gl_tex)
		glGenTextures(1, &tex->gl_tex);
	glBindFramebuffer(GL_FRAMEBUFFER, rt->gl_fbo);
	glBindTexture(GL_TEXTURE_2D, tex->gl_tex);
	int w = rt->frameBuf.width < tex->width ? rt->frameBuf.width : tex->width;
	int h = rt->frameBuf.height < tex->height ? rt->frameBuf.height : tex->height;
	if (tex->gl_rt == 0) {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex->width, tex->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		tex->gl_rt = 1;
	}
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
	tex->gl_dirty = 0;
	c3d_target_bind(g_c3d.target);
}

// The game's frame dumps (debug only): the target's picture into buf as 240 x 400 BGR.
void C3D_SyncDisplayTransfer(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 flags)
{
	(void)indim; (void)flags;
	C3D_RenderTarget* rt = target_of(inadr);
	if (!rt)
		return;
	int w = outdim & 0xFFFF, h = outdim >> 16;
	u8* rgba = malloc((size_t)w * h * 4);
	if (!rgba)
		return;
	glBindFramebuffer(GL_FRAMEBUFFER, rt->gl_fbo);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	u8* out = (u8*)outadr;
	for (int i = 0; i < w * h; i++) {
		out[i * 3] = rgba[i * 4 + 2];
		out[i * 3 + 1] = rgba[i * 4 + 1];
		out[i * 3 + 2] = rgba[i * 4];
	}
	free(rgba);
	c3d_target_bind(g_c3d.target);
}
