// sh2-vita: the 3DS input API (hid) on the Vita's buttons, sticks and front touch screen.
//
// For now the buttons are mapped by position to the 3DS's (Cross = A, Circle = B, ...), so the
// game's own controls keep working while the port comes up. The PC-style control scheme (map and
// inventory on their own buttons, as on the PC) replaces this in phase 5.
#include <3ds.h>

#include <stdlib.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/touch.h>

static u32 s_held, s_down, s_up;
static circlePosition s_cpad, s_cstick;
static touchPosition s_touch;
static bool s_init;

static void hid_init(void)
{
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
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

	// The front touch screen stands in for the 3DS's touch screen (320x240) until phase 5.
	SceTouchData touch;
	memset(&touch, 0, sizeof touch);
	sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
	if (touch.reportNum > 0) {
		k |= KEY_TOUCH;
		s_touch.px = (u16)(touch.report[0].x * 320 / 1920);
		s_touch.py = (u16)(touch.report[0].y * 240 / 1088);
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
