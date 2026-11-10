// sh2-vita: things the compat layer's files share. Not seen by the game code.
#pragma once

#include <stdbool.h>

#define VITA_DATA_DIR "ux0:data/sh2vita"
#define VITA_SCREEN_W 960
#define VITA_SCREEN_H 544

void vita_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void vita_fatal(const char* fmt, ...) __attribute__((format(printf, 1, 2), noreturn));

extern volatile bool g_vita_quit;

// gfx.c: called by C3D_FrameEnd once the 3DS top screen's image is ready, to put it on the Vita's
// screen (rotated back from the 3DS's portrait layout and scaled to 960x544).
void vita_present(void);
