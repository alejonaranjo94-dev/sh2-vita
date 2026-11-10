// sh2-vita: the Vita's version of port/source/profile.c (which sets up the 3DS heaps, clock and
// cores, and is left out of the Vita build). The Vita is treated as a New 3DS: high quality, the
// PC's texture sizes, all controls. The frame rate starts at 30 until the port is measured on the
// console (port/PERFORMANCE.md's budget was set for the 3DS GPU).
#include "profile.h"
#include <citro3d.h>
#include <string.h>
#if __has_include("quality.h")
#include "quality.h"
#endif

static struct {
	int hw, quality, layout, fps;   // CHOICE_*
	int tex_half, tex_full;
	char status[80];
} P;

static int clamp_choice(int v) { return v < 0 || v > 2 ? CHOICE_AUTO : v; }

bool profile_option(const char* key, int val)
{
	if (!strcmp(key, "hwprofile")) P.hw = clamp_choice(val);
	else if (!strcmp(key, "quality")) P.quality = clamp_choice(val);
	else if (!strcmp(key, "ctrllayout")) P.layout = clamp_choice(val);
	else if (!strcmp(key, "framerate")) P.fps = clamp_choice(val);
	else if (!strcmp(key, "helpercore")) {}
	else if (!strcmp(key, "texhalf")) P.tex_half = clamp_choice(val);
	else if (!strcmp(key, "texfull")) P.tex_full = clamp_choice(val);
	else return false;
	return true;
}

void profile_init(const char* options_path)
{
	FILE* f = fopen(options_path, "r");
	if (f) {
		char key[32];
		int val;
		while (fscanf(f, "%31s %d", key, &val) == 2)
			profile_option(key, val);
		fclose(f);
	}
	profile_apply_quality();
}

bool profile_console_new(void) { return true; }

HwProfile profile_get(void) { return P.hw == CHOICE_LOW_OLD ? PROFILE_OLD3DS : PROFILE_NEW3DS; }

QualityLevel profile_quality(void)
{
	if (P.quality == CHOICE_LOW_OLD) return QUALITY_LOW;
	return QUALITY_HIGH;
}

ControlsLayout controls_layout(void) { return P.layout == CHOICE_LOW_OLD ? CONTROLS_LAYOUT_OLD : CONTROLS_LAYOUT_NEW; }

bool hidShouldUseIrrst(void) { return true; }

// The Vita's cores 1 and 2 are free for helpers (core 0 runs the game, the mixer sits on core 1).
int profile_helper_core(void) { return 2; }
int profile_music_core(void) { return 2; }

static bool low(void) { return profile_quality() == QUALITY_LOW; }

bool profile_tex_half(void) { return P.tex_half == CHOICE_LOW_OLD; }
u32 profile_texcache_budget(void) { return 48u * 1024 * 1024; }
static bool tex_full(void) { return P.tex_full != CHOICE_HIGH_NEW; }
int profile_tex_max_map(void) { return tex_full() ? 512 : 256; }
int profile_tex_max_chr(void) { return tex_full() ? 1024 : 512; }
int profile_max_neighbours(void) { return low() ? 2 : 4; }
float profile_stream_dist(void) { return low() ? 5000.0f : 7000.0f; }
int profile_fog_sprites(void) { return low() ? 72 : 144; }
bool profile_shadows(void) { return !low(); }
int profile_chr_point_lights(void) { return low() ? 2 : 4; }
int profile_movie_scale(void) { return 1; }

int profile_frame_rate(void) { return P.fps == CHOICE_HIGH_NEW ? 60 : 30; }
void profile_apply_frame_rate(void) { C3D_FrameRate((float)profile_frame_rate()); }

void profile_apply_quality(void)
{
#if __has_include("quality.h")
	Quality q = *quality();
	q.fog_sprites = profile_fog_sprites();
	q.stencil_shadows = q.drop_shadows = profile_shadows();
	q.tile_distance = profile_stream_dist();
	q.frame_cap = profile_frame_rate();
	q.supersample = 0;   // the Vita draws at its own resolution; the 3DS's supersampling is not needed
	quality_set(&q);
#endif
}

void profile_set_choices(int hw, int quality, int layout, int fps)
{
	P.hw = clamp_choice(hw), P.quality = clamp_choice(quality);
	P.layout = clamp_choice(layout), P.fps = clamp_choice(fps);
	profile_apply_frame_rate();
	profile_apply_quality();
}

const char* profile_status(void)
{
	snprintf(P.status, sizeof P.status, "PS Vita, %s quality, %d fps", low() ? "low" : "high", profile_frame_rate());
	return P.status;
}
