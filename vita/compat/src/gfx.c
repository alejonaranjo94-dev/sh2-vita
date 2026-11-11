// sh2-vita: the screens. vitaGL start-up, the 3DS framebuffers the game writes by hand (the
// "game data not found" screen) and the picture the Vita shows each frame.
//
// One screen, as on the PC. In play, scenes, movies and the map the 3DS top screen fills the
// Vita's height (906 x 544, centred) and the touch screen's page is not shown: the PC has no HUD,
// and every button it offered is on a Vita button. When a menu is up (title, pause, items, options,
// save files, questions, puzzles) its list is drawn on the left and what the top screen shows
// (the item, the setting explained, the save file) on the right, on one black ground; the front
// touch screen taps the list (vita_touch_panel). touchui.c says which of the two (vita_ui_layout).
#include <citro3d.h>

#include <stdlib.h>
#include <string.h>

#include <psp2/display.h>
#include <psp2/power.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>
#include <vitashark.h>

#include "vita_compat.h"

extern C3D_RenderTarget* g_c3d_top_output;
extern C3D_RenderTarget* g_c3d_bottom_output;

static bool s_gl_ready;
static GLuint s_prog, s_fb_tex;
static GLint s_loc_pos, s_loc_uv, s_loc_tex;
static u8* s_fb[2];               // the 3DS framebuffers: top 240 x 400, bottom 240 x 320, 4 bytes a pixel
static bool s_fb_used;            // the game wrote the top framebuffer since the last present
static int s_layout = VITA_UI_FULL;

// The split layout, in Vita pixels: the list (320 x 240 at 1.4) on the left, the top screen
// (400 x 240 at 1.28) on the right, both centred on the height.
#define LIST_X 0.0f
#define LIST_W 448.0f
#define LIST_H 336.0f
#define LIST_Y ((VITA_SCREEN_H - LIST_H) * 0.5f)
#define SUBJ_X LIST_W
#define SUBJ_W (VITA_SCREEN_W - LIST_W)
#define SUBJ_H (SUBJ_W * 240.0f / 400.0f)
#define SUBJ_Y ((VITA_SCREEN_H - SUBJ_H) * 0.5f)

void vita_ui_layout(int layout) { s_layout = layout; }

bool vita_touch_panel(int sx, int sy, bool clamp, u16* px, u16* py)
{
	if (s_layout != VITA_UI_SPLIT)
		return false;
	float u = (sx - LIST_X) / LIST_W, v = (sy - LIST_Y) / LIST_H;
	bool inside = u >= 0.0f && u < 1.0f && v >= 0.0f && v < 1.0f;
	if (!inside && !clamp)
		return false;
	u = u < 0.0f ? 0.0f : u > 0.999f ? 0.999f : u;
	v = v < 0.0f ? 0.0f : v > 0.999f ? 0.999f : v;
	*px = (u16)(u * 320.0f), *py = (u16)(v * 240.0f);
	return true;
}

// The shader being compiled (set by vita_shader_compiling), so a compile error can log its source:
// vitaGL can hang after a failed compile, before the caller sees the error.
static const char* s_src[2];
static bool s_src_dumped;

void vita_shader_compiling(const char* a, const char* b)
{
	s_src[0] = a, s_src[1] = b, s_src_dumped = false;
}

static void dump_source(void)
{
	int n = 1;
	for (int i = 0; i < 2; i++) {
		const char* p = s_src[i];
		while (p && *p) {
			const char* e = strchr(p, '\n');
			int len = e ? (int)(e - p) : (int)strlen(p);
			vita_log("%4d| %.*s", n++, len, p);
			p = e ? e + 1 : p + len;
		}
	}
}

static void shark_log(const char* msg, shark_log_level level, int line)
{
	vita_log("[SHARK %d] line %d: %s", (int)level, line, msg);
	if ((int)level >= 2 && !s_src_dumped && (s_src[0] || s_src[1])) {
		s_src_dumped = true;
		vita_log("source of the shader that failed:");
		dump_source();
	}
}

static GLuint compile(GLenum type, const char* src)
{
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, NULL);
	vita_shader_compiling(src, NULL);
	glCompileShader(s);
	vita_shader_compiling(NULL, NULL);
	return s;
}

static const char* PRESENT_VS =
	"attribute vec2 a_pos;\n"
	"attribute vec2 a_uv;\n"
	"varying vec2 v_uv;\n"
	"void main() { v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

static const char* PRESENT_FS =
	"precision mediump float;\n"
	"uniform sampler2D u_tex;\n"
	"varying vec2 v_uv;\n"
	"void main() { gl_FragColor = vec4(texture2D(u_tex, v_uv).rgb, 1.0); }\n";

void vita_gl_init(void)
{
	if (s_gl_ready)
		return;
	s_gl_ready = true;

	sceIoMkdir(VITA_DATA_DIR, 0777);
	vita_log("sh2vita starting");

	// The Vita starts apps at 333 MHz; the game needs the full clocks (as SilentVitaHill and other ports)
	scePowerSetArmClockFrequency(444);
	scePowerSetBusClockFrequency(222);
	scePowerSetGpuClockFrequency(222);
	scePowerSetGpuXbarClockFrequency(166);
	vita_log("clocks: cpu %d bus %d gpu %d xbar %d", scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
	         scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());

	vglInitExtended(0, VITA_SCREEN_W, VITA_SCREEN_H, 16 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
	vglWaitVblankStart(GL_FALSE);   // the game paces itself (C3D_FrameEnd)
	shark_install_log_cb(shark_log);

	s_prog = glCreateProgram();
	glAttachShader(s_prog, compile(GL_VERTEX_SHADER, PRESENT_VS));
	glAttachShader(s_prog, compile(GL_FRAGMENT_SHADER, PRESENT_FS));
	glBindAttribLocation(s_prog, 0, "a_pos");
	glBindAttribLocation(s_prog, 1, "a_uv");
	glLinkProgram(s_prog);
	s_loc_pos = 0;
	s_loc_uv = 1;
	s_loc_tex = glGetUniformLocation(s_prog, "u_tex");

	glGenTextures(1, &s_fb_tex);
	glBindTexture(GL_TEXTURE_2D, s_fb_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	vita_log("vitaGL ready");
}

static void begin_screen(void)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, VITA_SCREEN_W, VITA_SCREEN_H);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
}

// Draws a 3DS screen's texture into the rectangle (x, y, w, h) of the Vita's screen (pixels, y
// down). The 3DS's pictures are portrait and turned, and which way depends on where they come from:
// a render target (GL's own rows, bottom first) has the screen's left edge as its top row and its
// top edge as the right column (the Tilt matrices); a framebuffer the game wrote by hand (3DS
// memory order) has each memory row as one screen column, left first, top pixel last.
static void blit(GLuint tex, float x, float y, float w, float h, bool from_target)
{
	float l = x / (VITA_SCREEN_W * 0.5f) - 1.0f, r = (x + w) / (VITA_SCREEN_W * 0.5f) - 1.0f;
	float t = 1.0f - y / (VITA_SCREEN_H * 0.5f), b = 1.0f - (y + h) / (VITA_SCREEN_H * 0.5f);
	float pos[8] = { l, t, r, t, l, b, r, b };                         // TL, TR, BL, BR
	float corners[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };   // (u, v) of each
	float uv[8];
	for (int i = 0; i < 4; i++) {
		float u = corners[i][0], v = corners[i][1];
		uv[i * 2] = 1.0f - v;
		uv[i * 2 + 1] = from_target ? 1.0f - u : u;
	}

	glUseProgram(s_prog);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex);
	glUniform1i(s_loc_tex, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glEnableVertexAttribArray(s_loc_pos);
	glEnableVertexAttribArray(s_loc_uv);
	glVertexAttribPointer(s_loc_pos, 2, GL_FLOAT, GL_FALSE, 0, pos);
	glVertexAttribPointer(s_loc_uv, 2, GL_FLOAT, GL_FALSE, 0, uv);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisableVertexAttribArray(s_loc_pos);
	glDisableVertexAttribArray(s_loc_uv);
}

// The top screen alone, scaled to the full height: 400 x 240 at 544 high is 906.67 wide.
static void draw_full(GLuint tex, bool from_target)
{
	float w = VITA_SCREEN_H * 400.0f / 240.0f;
	blit(tex, (VITA_SCREEN_W - w) * 0.5f, 0.0f, w, VITA_SCREEN_H, from_target);
}

static GLuint target_tex(C3D_RenderTarget* rt) { return rt->gl_tex ? rt->gl_tex->gl_tex : rt->gl_color; }

void vita_present(void)
{
	C3D_RenderTarget* top = g_c3d_top_output;
	C3D_RenderTarget* bottom = g_c3d_bottom_output;
	begin_screen();
	if (s_layout == VITA_UI_SPLIT && bottom) {
		blit(target_tex(bottom), LIST_X, LIST_Y, LIST_W, LIST_H, true);
		if (top)
			blit(target_tex(top), SUBJ_X, SUBJ_Y, SUBJ_W, SUBJ_H, true);
	} else if (top) {
		draw_full(target_tex(top), true);
	}
	vglSwapBuffers(GL_FALSE);
}

// ---- the framebuffers the game writes itself (gfx*) --------------------------------------------
void gfxInitDefault(void) { vita_gl_init(); }
void gfxExit(void) {}
void gfxSet3D(bool enable) { (void)enable; }

u8* gfxGetFramebuffer(gfxScreen_t screen, gfx3dSide_t side, u16* width, u16* height)
{
	(void)side;
	int i = screen == GFX_TOP ? 0 : 1;
	int rows = i == 0 ? 400 : 320;
	if (!s_fb[i])
		s_fb[i] = calloc(240 * rows, 4);
	if (width) *width = 240;
	if (height) *height = (u16)rows;
	if (i == 0)
		s_fb_used = true;
	return s_fb[i];
}

void gfxFlushBuffers(void) {}

void gfxSwapBuffers(void)
{
	vita_gl_init();
	if (!s_fb_used || !s_fb[0])
		return;
	// BGR, 240 bytes-triplets per memory row, 400 rows -> an RGB texture of the same shape
	static u8 rgb[240 * 400 * 3];
	const u8* src = s_fb[0];
	for (int i = 0; i < 240 * 400; i++) {
		rgb[i * 3] = src[i * 3 + 2];
		rgb[i * 3 + 1] = src[i * 3 + 1];
		rgb[i * 3 + 2] = src[i * 3];
	}
	glBindTexture(GL_TEXTURE_2D, s_fb_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 240, 400, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
	begin_screen();
	draw_full(s_fb_tex, false);
	vglSwapBuffers(GL_FALSE);
}

void gspWaitForVBlank(void) { sceDisplayWaitVblankStart(); }

// ---- text console (the 3DS's debug text screen): its output goes to the log ------------------
static PrintConsole s_console;
PrintConsole* consoleInit(gfxScreen_t screen, PrintConsole* console) { (void)screen; return console ? console : &s_console; }
PrintConsole* consoleSelect(PrintConsole* console) { return console; }
void consoleClear(void) {}
