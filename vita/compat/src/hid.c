// sh2-vita: the 3DS input API (hid) on the Vita's buttons, sticks and front touch screen.
//
// The Vita's buttons stand for the 3DS's by their place and meaning: Cross = A (take), Circle = B
// (back), Square = Y, Triangle = X, the shoulders L and R, the left stick the Circle Pad, the right
// stick the C-Stick. What each does in play is controls.c's Vita layout (Cross acts, Square runs,
// Circle opens the items, Triangle the map ...). The front touch screen taps the menu list where it
// is drawn on the Vita's screen (gfx.c vita_touch_panel); elsewhere it does nothing.
#include <3ds.h>

#include <stdlib.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/touch.h>

#include "vita_compat.h"

static u32 s_held, s_down, s_up;
static circlePosition s_cpad, s_cstick;
static touchPosition s_touch;
static bool s_init, s_touching;   // s_touching: a finger came down on the list and is still down

static void hid_init(void)
{
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
	s_init = true;
}

// The 3DS circle pad reads about -156..156 with up positive; the Vita's sticks read 0..255 with
// 128 at rest and down positive.
static s16 axis(int v, bool invert)
{
	int d = v - 128;
	if (abs(d) < 12)   // dead zone, the Vita's sticks rest a little off centre
		d = 0;
	int out = d * 156 / 127;
	if (out > 156) out = 156;
	if (out < -156) out = -156;
	return (s16)(invert ? -out : out);
}

void hidScanInput(void)
{
	if (!s_init)
		hid_init();

	SceCtrlData pad;
	memset(&pad, 0, sizeof pad);
	sceCtrlPeekBufferPositive(0, &pad, 1);

	u32 k = 0;
	if (pad.buttons & SCE_CTRL_CROSS) k |= KEY_A;
	if (pad.buttons & SCE_CTRL_CIRCLE) k |= KEY_B;
	if (pad.buttons & SCE_CTRL_SQUARE) k |= KEY_Y;
	if (pad.buttons & SCE_CTRL_TRIANGLE) k |= KEY_X;
	if (pad.buttons & SCE_CTRL_LTRIGGER) k |= KEY_L;
	if (pad.buttons & SCE_CTRL_RTRIGGER) k |= KEY_R;
	if (pad.buttons & SCE_CTRL_START) k |= KEY_START;
	if (pad.buttons & SCE_CTRL_SELECT) k |= KEY_SELECT;
	if (pad.buttons & SCE_CTRL_UP) k |= KEY_DUP;
	if (pad.buttons & SCE_CTRL_DOWN) k |= KEY_DDOWN;
	if (pad.buttons & SCE_CTRL_LEFT) k |= KEY_DLEFT;
	if (pad.buttons & SCE_CTRL_RIGHT) k |= KEY_DRIGHT;

	s_cpad.dx = axis(pad.lx, false);
	s_cpad.dy = axis(pad.ly, true);
	s_cstick.dx = axis(pad.rx, false);
	s_cstick.dy = axis(pad.ry, true);
	if (s_cpad.dx > 40) k |= KEY_CPAD_RIGHT;
	if (s_cpad.dx < -40) k |= KEY_CPAD_LEFT;
	if (s_cpad.dy > 40) k |= KEY_CPAD_UP;
	if (s_cpad.dy < -40) k |= KEY_CPAD_DOWN;
	if (s_cstick.dx > 40) k |= KEY_CSTICK_RIGHT;
	if (s_cstick.dx < -40) k |= KEY_CSTICK_LEFT;
	if (s_cstick.dy > 40) k |= KEY_CSTICK_UP;
	if (s_cstick.dy < -40) k |= KEY_CSTICK_DOWN;

	// The front touch screen (1920 x 1088) on the list as the 3DS touch screen: a finger that came
	// down on it stays on it (clamped to its edge) until lifted, as a drag on the 3DS would.
	SceTouchData touch;
	memset(&touch, 0, sizeof touch);
	sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
	bool on = false;
	if (touch.reportNum > 0) {
		u16 px, py;
		int sx = touch.report[0].x * VITA_SCREEN_W / 1920, sy = touch.report[0].y * VITA_SCREEN_H / 1088;
		if (vita_touch_panel(sx, sy, s_touching, &px, &py)) {
			on = true;
			s_touch.px = px, s_touch.py = py;
		}
	}
	s_touching = on;
	if (on)
		k |= KEY_TOUCH;

	// The rear touch pad's upper outer corners, under the index fingers' reach, are the PS2's L2 and
	// R2 (ZL and ZR: the side steps by default, both together a quick turn). The middle and the
	// lower half, where the hands rest, do nothing.
	SceTouchData rear;
	memset(&rear, 0, sizeof rear);
	sceTouchPeek(SCE_TOUCH_PORT_BACK, &rear, 1);
	for (int i = 0; i < rear.reportNum && i < 4; i++) {
		int x = rear.report[i].x, y = rear.report[i].y;   // 0..1919, about 108..889
		if (y > 500)
			continue;
		if (x < 640) k |= KEY_ZL;
		else if (x >= 1280) k |= KEY_ZR;
	}

	s_down = k & ~s_held;
	s_up = s_held & ~k;
	s_held = k;
}

u32 hidKeysDown(void) { return s_down; }
u32 hidKeysHeld(void) { return s_held; }
u32 hidKeysUp(void) { return s_up; }
void hidTouchRead(touchPosition* pos) { *pos = s_touch; }
void hidCircleRead(circlePosition* pos) { *pos = s_cpad; }
void hidCstickRead(circlePosition* pos) { *pos = s_cstick; }
