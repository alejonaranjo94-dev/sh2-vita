// sh2-vita: things the compat layer's files share. Not seen by the game code.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define VITA_DATA_DIR "ux0:data/sh2vita"
#define VITA_SCREEN_W 960
#define VITA_SCREEN_H 544

void vita_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void vita_fatal(const char* fmt, ...) __attribute__((format(printf, 1, 2), noreturn));

extern volatile bool g_vita_quit;

// gfx.c: called by C3D_FrameEnd once the 3DS top screen's image is ready, to put it on the Vita's
// screen (rotated back from the 3DS's portrait layout and scaled to 960x544).
void vita_present(void);
// gfx.c: the shader source being compiled (NULL, NULL after), logged if the compile fails
void vita_shader_compiling(const char* a, const char* b);

// gfx.c: how the Vita's one screen is laid out (touchui.c sets it as its page changes): the top
// screen alone, or the touch screen's list on the left with the top screen beside it.
enum { VITA_UI_FULL, VITA_UI_SPLIT };
void vita_ui_layout(int layout);
// gfx.c: a point on the Vita's screen (pixels) as a point on the 3DS touch screen (320 x 240);
// false when the list is not shown, or the point is off it (clamp: kept on its edge instead).
bool vita_touch_panel(int sx, int sy, bool clamp, uint16_t* px, uint16_t* py);
