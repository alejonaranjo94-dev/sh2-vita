// sh2-vita: the 3DS DSP (ndsp) as a software mixer.
//
// 24 channels, each with a queue of wave buffers like the DSP's. A mixer thread resamples every
// playing channel to 48 kHz with linear interpolation, applies its left/right mix and writes the
// sum to the Vita's audio port. The game's own buffer bookkeeping (status, nsamples, sample
// position, sequence ids) behaves as on the 3DS.
#include <3ds.h>

#include <math.h>
#include <string.h>

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>

#include "vita_compat.h"

#define OUT_RATE 48000
#define GRAIN 512   // frames per write: about 10.7 ms

typedef struct {
	ndspWaveBuf* head;    // playing buffer; ->next is the queue
	double pos;           // frames into head
	float rate;
	float mix_l, mix_r;
	u16 format;
	u16 seq_next;
	u16 seq_playing;
	bool paused;
} Channel;

static Channel s_ch[NDSP_NUM_CHANNELS];
static SceUID s_lock = -1;
static SceUID s_thread = -1;
static int s_port = -1;
static int s_refs;
static volatile bool s_run;
static ndspOutputMode s_mode = NDSP_OUTPUT_STEREO;
static float s_master = 1.0f;

static void lock(void) { sceKernelLockMutex(s_lock, 1, NULL); }
static void unlock(void) { sceKernelUnlockMutex(s_lock, 1); }

static int channels_of(u16 format) { return (format & 3) == 2 ? 2 : 1; }
static bool is_pcm8(u16 format) { return ((format >> 2) & 3) == NDSP_ENCODING_PCM8; }

static inline float sample_at(const ndspWaveBuf* b, u16 format, u32 frame, int c)
{
	int nch = channels_of(format);
	u32 i = frame * nch + (nch == 2 ? c : 0);
	if (is_pcm8(format))
		return b->data_pcm8[i] * (1.0f / 128.0f);
	return b->data_pcm16[i] * (1.0f / 32768.0f);
}

// Moves the channel to its next buffer; returns false when the queue ran dry.
static bool advance_buffer(Channel* ch)
{
	ndspWaveBuf* done = ch->head;
	if (done->looping) {
		ch->pos = 0;
		return true;
	}
	done->status = NDSP_WBUF_DONE;
	ch->head = done->next;
	ch->pos = 0;
	if (ch->head) {
		ch->head->status = NDSP_WBUF_PLAYING;
		ch->seq_playing = ch->head->sequence_id;
		ch->pos = ch->head->offset;
	}
	return ch->head != NULL;
}

static void mix_channel(Channel* ch, float* out, int frames)
{
	float step = ch->rate / OUT_RATE;
	for (int f = 0; f < frames && ch->head; f++) {
		ndspWaveBuf* b = ch->head;
		u32 i0 = (u32)ch->pos;
		if (i0 >= b->nsamples) {
			if (!advance_buffer(ch))
				break;
			f--;
			continue;
		}
		u32 i1 = i0 + 1 < b->nsamples ? i0 + 1 : i0;
		float t = (float)(ch->pos - i0);
		float l = sample_at(b, ch->format, i0, 0) * (1 - t) + sample_at(b, ch->format, i1, 0) * t;
		float r = l;
		if (channels_of(ch->format) == 2)
			r = sample_at(b, ch->format, i0, 1) * (1 - t) + sample_at(b, ch->format, i1, 1) * t;
		out[f * 2] += l * ch->mix_l;
		out[f * 2 + 1] += r * ch->mix_r;
		ch->pos += step;
	}
}

static int mixer_thread(SceSize args, void* argp)
{
	(void)args; (void)argp;
	static float acc[GRAIN * 2];
	static s16 pcm[2][GRAIN * 2];
	int which = 0;
	while (s_run) {
		memset(acc, 0, sizeof acc);
		lock();
		for (int c = 0; c < NDSP_NUM_CHANNELS; c++)
			if (s_ch[c].head && !s_ch[c].paused)
				mix_channel(&s_ch[c], acc, GRAIN);
		ndspOutputMode mode = s_mode;
		float master = s_master;
		unlock();

		s16* o = pcm[which];
		for (int f = 0; f < GRAIN; f++) {
			float l = acc[f * 2] * master, r = acc[f * 2 + 1] * master;
			if (mode == NDSP_OUTPUT_MONO)
				l = r = (l + r) * 0.5f;
			l = l > 1 ? 1 : l < -1 ? -1 : l;
			r = r > 1 ? 1 : r < -1 ? -1 : r;
			o[f * 2] = (s16)(l * 32767);
			o[f * 2 + 1] = (s16)(r * 32767);
		}
		sceAudioOutOutput(s_port, o);   // blocks until the port can take the next grain
		which ^= 1;
	}
	return 0;
}

Result ndspInit(void)
{
	if (s_refs++ > 0)
		return 0;
	if (s_lock < 0)
		s_lock = sceKernelCreateMutex("sh2v_ndsp", SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 0, NULL);
	for (int c = 0; c < NDSP_NUM_CHANNELS; c++) {
		memset(&s_ch[c], 0, sizeof s_ch[c]);
		s_ch[c].mix_l = s_ch[c].mix_r = 1.0f;
		s_ch[c].rate = OUT_RATE;
		s_ch[c].format = NDSP_FORMAT_MONO_PCM16;
	}
	s_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, GRAIN, OUT_RATE, SCE_AUDIO_OUT_MODE_STEREO);
	if (s_port < 0) {
		vita_log("ndspInit: sceAudioOutOpenPort failed 0x%08X", s_port);
		s_refs = 0;
		return -1;
	}
	int vol[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };
	sceAudioOutSetVolume(s_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
	s_run = true;
	s_thread = sceKernelCreateThread("sh2v_mixer", mixer_thread, 0x40, 64 * 1024, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
	sceKernelStartThread(s_thread, 0, NULL);
	return 0;
}

void ndspExit(void)
{
	if (s_refs <= 0 || --s_refs > 0)
		return;
	s_run = false;
	sceKernelWaitThreadEnd(s_thread, NULL, NULL);
	sceKernelDeleteThread(s_thread);
	sceAudioOutReleasePort(s_port);
	s_port = -1;
}

void ndspSetOutputMode(ndspOutputMode mode) { s_mode = mode; }
void ndspSetMasterVol(float volume) { s_master = volume; }

static void clear_queue(Channel* ch)
{
	for (ndspWaveBuf* b = ch->head; b; b = b->next)
		b->status = NDSP_WBUF_DONE;
	ch->head = NULL;
	ch->pos = 0;
}

void ndspChnReset(int id)
{
	lock();
	Channel* ch = &s_ch[id];
	clear_queue(ch);
	ch->paused = false;
	ch->mix_l = ch->mix_r = 1.0f;
	ch->rate = OUT_RATE;
	ch->seq_next = ch->seq_playing = 0;
	unlock();
}

void ndspChnWaveBufClear(int id)
{
	lock();
	clear_queue(&s_ch[id]);
	unlock();
}

void ndspChnSetPaused(int id, bool paused) { s_ch[id].paused = paused; }
bool ndspChnIsPaused(int id) { return s_ch[id].paused; }
bool ndspChnIsPlaying(int id) { return s_ch[id].head != NULL; }
u16 ndspChnGetWaveBufSeq(int id) { return s_ch[id].head ? s_ch[id].seq_playing : 0; }

u32 ndspChnGetSamplePos(int id)
{
	lock();
	u32 pos = s_ch[id].head ? (u32)s_ch[id].pos : 0;
	unlock();
	return pos;
}

void ndspChnSetFormat(int id, u16 format) { s_ch[id].format = format; }
void ndspChnSetInterp(int id, ndspInterpType type) { (void)id; (void)type; }
void ndspChnSetRate(int id, float rate) { s_ch[id].rate = rate; }

void ndspChnSetMix(int id, float mix[12])
{
	lock();
	s_ch[id].mix_l = mix[0];
	s_ch[id].mix_r = mix[1];
	unlock();
}

void ndspChnWaveBufAdd(int id, ndspWaveBuf* buf)
{
	if (!buf || buf->nsamples == 0)
		return;
	lock();
	Channel* ch = &s_ch[id];
	buf->next = NULL;
	buf->sequence_id = ++ch->seq_next;
	if (!ch->head) {
		ch->head = buf;
		buf->status = NDSP_WBUF_PLAYING;
		ch->seq_playing = buf->sequence_id;
		ch->pos = buf->offset;
	} else {
		ndspWaveBuf* tail = ch->head;
		while (tail->next)
			tail = tail->next;
		tail->next = buf;
		buf->status = NDSP_WBUF_QUEUED;
	}
	unlock();
}
