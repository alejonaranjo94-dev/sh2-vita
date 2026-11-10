// sh2-vita: the screens. vitaGL start-up, the 3DS framebuffers the game writes by hand (the
// "game data not found" screen) and the picture the Vita shows each frame.
//
// The 3DS top screen is 400 x 240; on the Vita it is scaled to the full height (906 x 544) and
// centred. The bottom screen is not shown: its functions move to PC-style menus in phase 5.
#include <citro3d.h>

#include <stdlib.h>
#include <string.h>

#include <psp2/display.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>
#include <vitashark.h>

#include "vita_compat.h"

extern C3D_RenderTarget* g_c3d_top_output;

static bool s_gl_ready;
static GLuint s_prog, s_fb_tex;
static GLint s_loc_pos, s_loc_uv, s_loc_tex;
static u8* s_fb[2];               // the 3DS framebuffers: top 240 x 400, bottom 240 x 320, 4 bytes a pixel
static bool s_fb_used;            // the game wrote the top framebuffer since the last present

static void shark_log(const char* msg, shark_log_level level, int line)
{
	vita_log("[SHARK %d] line %d: %s", (int)level, line, msg);
}

static GLuint compile(GLenum type, const char* src)
{
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
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

// Draws texture tex over the 3DS top screen's area. uv_of maps a corner of the screen (u right,
// v down, both 0..1) to the texture coordinates holding it; the 3DS's pictures are portrait and
// turned, and which way depends on where they come from (see the two callers).
static void draw_screen(GLuint tex, bool from_target)
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

	// 400 x 240 scaled to 544 high is 906.67 wide
	float half_w = (VITA_SCREEN_H * 400.0f / 240.0f) / VITA_SCREEN_W;
	float pos[8] = { -half_w, 1, half_w, 1, -half_w, -1, half_w, -1 };   // TL, TR, BL, BR
	float corners[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };     // (u, v) of each
	float uv[8];
	for (int i = 0; i < 4; i++) {
		float u = corners[i][0], v = corners[i][1];
		// A render target (GL's own rows, bottom first): the screen's left edge is the top row and
		// its top edge the right column (the Tilt matrices). A framebuffer the game wrote by hand
		// (3DS memory order): each memory row is one screen column, left first, top pixel last.
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

void vita_present(void)
{
	C3D_RenderTarget* top = g_c3d_top_output;
	if (top) {
		GLuint tex = top->gl_tex ? top->gl_tex->gl_tex : top->gl_color;
		draw_screen(tex, true);
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
	draw_screen(s_fb_tex, false);
	vglSwapBuffers(GL_FALSE);
}

void gspWaitForVBlank(void) { sceDisplayWaitVblankStart(); }

// ---- text console (the 3DS's debug text screen): its output goes to the log ------------------
static PrintConsole s_console;
PrintConsole* consoleInit(gfxScreen_t screen, PrintConsole* console) { (void)screen; return console ? console : &s_console; }
PrintConsole* consoleSelect(PrintConsole* console) { return console; }
void consoleClear(void) {}
