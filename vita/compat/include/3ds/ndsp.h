// sh2-vita: the libctru DSP audio API (ndsp), implemented by vita/compat/src/ndsp.c as a software
// mixer that feeds the Vita's audio output. 24 channels, each with a queue of wave buffers, as on
// the 3DS.
#pragma once

#define NDSP_NUM_CHANNELS 24
#define NDSP_SAMPLE_RATE (16756991.0f / 512)   // the 3DS DSP's rate; the mixer resamples to 48 kHz

typedef enum { NDSP_OUTPUT_MONO = 0, NDSP_OUTPUT_STEREO = 1, NDSP_OUTPUT_SURROUND = 2 } ndspOutputMode;
typedef enum { NDSP_INTERP_POLYPHASE = 0, NDSP_INTERP_LINEAR = 1, NDSP_INTERP_NONE = 2 } ndspInterpType;

enum {
	NDSP_ENCODING_PCM8 = 0, NDSP_ENCODING_PCM16 = 1, NDSP_ENCODING_ADPCM = 2,
	NDSP_CHANNELS_1 = 1 << 0, NDSP_CHANNELS_2 = 2 << 0,
#define NDSP_CHANNELS(n) ((u32)(n) & 3)
#define NDSP_ENCODING(n) (((u32)(n) & 3) << 2)
	NDSP_FORMAT_MONO_PCM8 = 1 | (0 << 2), NDSP_FORMAT_MONO_PCM16 = 1 | (1 << 2),
	NDSP_FORMAT_STEREO_PCM8 = 2 | (0 << 2), NDSP_FORMAT_STEREO_PCM16 = 2 | (1 << 2),
};

enum { NDSP_WBUF_FREE = 0, NDSP_WBUF_QUEUED = 1, NDSP_WBUF_PLAYING = 2, NDSP_WBUF_DONE = 3 };

typedef struct { u16 index; s16 history0, history1; } ndspAdpcmData;

typedef struct tag_ndspWaveBuf ndspWaveBuf;
struct tag_ndspWaveBuf {
	union {
		s8* data_pcm8;
		s16* data_pcm16;
		u8* data_adpcm;
		const void* data_vaddr;
	};
	u32 nsamples;                 // frames (a stereo frame is two samples)
	ndspAdpcmData* adpcm_data;
	u32 offset;                   // first frame to play
	bool looping;
	volatile u8 status;           // NDSP_WBUF_*
	u16 sequence_id;
	ndspWaveBuf* next;
};

Result ndspInit(void);
void ndspExit(void);
void ndspSetOutputMode(ndspOutputMode mode);
void ndspSetMasterVol(float volume);

void ndspChnReset(int id);
void ndspChnSetPaused(int id, bool paused);
bool ndspChnIsPaused(int id);
bool ndspChnIsPlaying(int id);
u32 ndspChnGetSamplePos(int id);
u16 ndspChnGetWaveBufSeq(int id);
void ndspChnSetFormat(int id, u16 format);
void ndspChnSetInterp(int id, ndspInterpType type);
void ndspChnSetRate(int id, float rate);
void ndspChnSetMix(int id, float mix[12]);   // [0] front left, [1] front right; the rest is ignored
void ndspChnWaveBufClear(int id);
void ndspChnWaveBufAdd(int id, ndspWaveBuf* buf);
