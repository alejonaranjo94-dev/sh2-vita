// sh2-vita: time, threads, locks, events, memory and system odds and ends from libctru,
// on the Vita kernel.
#define SH2VITA_NO_FS_REDIRECT
#include <3ds.h>

#include <malloc.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/apputil.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/system_param.h>

#include "vita_compat.h"

// The game's heap: textures, models and the stream buffers live here. vitaGL keeps its own pools
// in video and CDRAM memory, so this leaves the rest of the 256 MB for the system.
int _newlib_heap_size_user = 200 * 1024 * 1024;

// ---- log ----------------------------------------------------------------------------------------
static SceUID s_log_lock = -1;

void vita_log(const char* fmt, ...)
{
	char line[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	if (s_log_lock < 0)
		s_log_lock = sceKernelCreateMutex("sh2v_log", 0, 0, NULL);
	sceKernelLockMutex(s_log_lock, 1, NULL);
	FILE* f = fopen(VITA_DATA_DIR "/log.txt", "a");
	if (f) {
		fprintf(f, "[%8.3f] %s\n", (double)sceKernelGetProcessTimeWide() / 1e6, line);
		fclose(f);
	}
	sceKernelUnlockMutex(s_log_lock, 1);
}

void vita_fatal(const char* fmt, ...)
{
	char line[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	vita_log("FATAL: %s", line);
	sceKernelExitProcess(1);
	for (;;) {}
}

// ---- time ---------------------------------------------------------------------------------------
u64 svcGetSystemTick(void)
{
	// Microseconds to ARM11 ticks (268.111856 per microsecond), in integers to keep precision.
	u64 us = sceKernelGetProcessTimeWide();
	return us * 268ULL + us * 111856ULL / 1000000ULL;
}

void svcSleepThread(s64 ns)
{
	if (ns <= 0) {
		sceKernelDelayThread(0);
		return;
	}
	sceKernelDelayThread((SceUInt)(ns / 1000));
}

u64 osGetTime(void) { return sceKernelGetProcessTimeWide() / 1000ULL; }
void osSetSpeedupEnable(bool enable) { (void)enable; }
float osGet3DSliderState(void) { return 0.0f; }

// ---- svc ----------------------------------------------------------------------------------------
void svcBreak(UserBreakType reason)
{
	vita_fatal("svcBreak(%d)", (int)reason);
	for (;;) {}
}

s32 svcGetProcessorID(void) { return 0; }

// 3DS thread priorities run 0x18 (highest) to 0x3F; the game asks for its own and adds or subtracts
// one or two. The Vita's user range is centred on SCE_KERNEL_DEFAULT_PRIORITY_USER, also with
// lower numbers first, so the 3DS value is kept and shifted onto it in threadCreate.
#define CTR_MAIN_PRIO 0x30

Result svcGetThreadPriority(s32* out, Handle thread)
{
	(void)thread;
	*out = CTR_MAIN_PRIO;
	return 0;
}

Result svcFlushProcessDataCache(Handle process, u32 addr, u32 size)
{
	(void)process; (void)addr; (void)size;
	return 0;
}

// Only the movie player's Y2R event uses these, and y2rInit fails on the Vita, so they are never
// reached with a real handle.
Result svcWaitSynchronization(Handle h, s64 ns) { (void)h; (void)ns; return -1; }
Result svcClearEvent(Handle h) { (void)h; return 0; }
Result svcCloseHandle(Handle h) { (void)h; return 0; }

Result svcGetResourceLimit(Handle* out, Handle process) { (void)process; *out = 1; return 0; }

Result svcGetResourceLimitLimitValues(s64* values, Handle res, ResourceLimitType* names, s32 count)
{
	(void)res; (void)names;
	for (s32 i = 0; i < count; i++)
		values[i] = _newlib_heap_size_user;
	return 0;
}

Result svcGetResourceLimitCurrentValues(s64* values, Handle res, ResourceLimitType* names, s32 count)
{
	(void)res; (void)names;
	struct mallinfo mi = mallinfo();
	for (s32 i = 0; i < count; i++)
		values[i] = mi.uordblks;
	return 0;
}

Result svcControlMemory(u32* addr_out, u32 addr0, u32 addr1, u32 size, MemOp op, MemPerm perm)
{
	(void)addr0; (void)addr1; (void)op; (void)perm; (void)size;
	*addr_out = 0;
	return -1;   // the 3DS heap setup in profile.c is not built on the Vita
}

// ---- memory -------------------------------------------------------------------------------------
void* linearAlloc(size_t size) { return memalign(0x80, size); }
void* linearMemAlign(size_t size, size_t alignment) { return memalign(alignment < 0x80 ? 0x80 : alignment, size); }
void linearFree(void* mem) { free(mem); }
u32 linearSpaceFree(void) { return 64 * 1024 * 1024; }
void* vramAlloc(size_t size) { return memalign(0x80, size); }
void vramFree(void* mem) { free(mem); }
u32 vramSpaceFree(void) { return 32 * 1024 * 1024; }
Result DSP_FlushDataCache(const void* address, u32 size) { (void)address; (void)size; return 0; }
void c3d_tex_mark_written(const void* adr, u32 size);   // c3d_tex.c
Result GSPGPU_FlushDataCache(const void* adr, u32 size)
{
	c3d_tex_mark_written(adr, size);
	return 0;
}
Result GSPGPU_InvalidateDataCache(const void* adr, u32 size) { (void)adr; (void)size; return 0; }

// ---- threads ------------------------------------------------------------------------------------
struct Thread_tag {
	SceUID uid;
	ThreadFunc entry;
	void* arg;
	bool detached;
	volatile bool finished;
};

static int thread_trampoline(SceSize args, void* argp)
{
	(void)args;
	struct Thread_tag* t = *(struct Thread_tag**)argp;
	t->entry(t->arg);
	t->finished = true;
	if (t->detached) {
		free(t);
		sceKernelExitDeleteThread(0);
	}
	return 0;
}

Thread threadCreate(ThreadFunc entry, void* arg, size_t stack_size, int prio, int core_id, bool detached)
{
	struct Thread_tag* t = calloc(1, sizeof *t);
	if (!t)
		return NULL;
	t->entry = entry;
	t->arg = arg;
	t->detached = detached;

	int vprio = 0x10000100 + (prio - CTR_MAIN_PRIO)   /* the Vita default user priority */;
	// Core 0 runs the game and its GPU work; the 3DS's core 1 / core 2 requests land on the Vita's
	// cores 1 and 2. -1 and -2 ("any" / "default") let the kernel choose.
	int affinity = 0;
	if (core_id == 0)
		affinity = SCE_KERNEL_CPU_MASK_USER_0;
	else if (core_id == 1)
		affinity = SCE_KERNEL_CPU_MASK_USER_1;
	else if (core_id >= 2)
		affinity = SCE_KERNEL_CPU_MASK_USER_2;

	if (stack_size < 64 * 1024)
		stack_size = 64 * 1024;   // the Vita's newlib needs more stack than the 3DS's for printf & co.
	t->uid = sceKernelCreateThread("sh2v_thread", thread_trampoline, vprio, stack_size, 0, affinity, NULL);
	if (t->uid < 0) {
		vita_log("threadCreate failed: 0x%08X", t->uid);
		free(t);
		return NULL;
	}
	struct Thread_tag* argp = t;
	if (sceKernelStartThread(t->uid, sizeof argp, &argp) < 0) {
		sceKernelDeleteThread(t->uid);
		free(t);
		return NULL;
	}
	return detached ? (Thread)1 : t;   // a detached thread frees itself; callers only test it for NULL
}

Result threadJoin(Thread thread, u64 timeout_ns)
{
	if (!thread || thread == (Thread)1)
		return -1;
	if (timeout_ns == UINT64_MAX)
		return sceKernelWaitThreadEnd(thread->uid, NULL, NULL) < 0 ? -1 : 0;
	SceUInt us = (SceUInt)(timeout_ns / 1000);
	return sceKernelWaitThreadEnd(thread->uid, NULL, &us) < 0 ? -1 : 0;
}

void threadFree(Thread thread)
{
	if (!thread || thread == (Thread)1)
		return;
	sceKernelDeleteThread(thread->uid);
	free(thread);
}

// ---- LightLock ----------------------------------------------------------------------------------
static SceUID s_lazy_lock = -1;

static SceUID lock_uid(LightLock* lock)
{
	// A LightLock may be used without LightLock_Init (static, zeroed). Create it on first use,
	// under a lock of our own so two threads don't both create one.
	if (lock->uid > 0)
		return lock->uid;
	if (s_lazy_lock < 0)
		s_lazy_lock = sceKernelCreateMutex("sh2v_lazy", 0, 0, NULL);
	sceKernelLockMutex(s_lazy_lock, 1, NULL);
	if (lock->uid <= 0)
		lock->uid = sceKernelCreateMutex("sh2v_lock", 0, 0, NULL);
	sceKernelUnlockMutex(s_lazy_lock, 1);
	return lock->uid;
}

void LightLock_Init(LightLock* lock) { lock->uid = sceKernelCreateMutex("sh2v_lock", 0, 0, NULL); }
void LightLock_Lock(LightLock* lock) { sceKernelLockMutex(lock_uid(lock), 1, NULL); }
int LightLock_TryLock(LightLock* lock) { return sceKernelTryLockMutex(lock_uid(lock), 1) < 0 ? 1 : 0; }
void LightLock_Unlock(LightLock* lock) { sceKernelUnlockMutex(lock_uid(lock), 1); }

// ---- LightEvent ---------------------------------------------------------------------------------
void LightEvent_Init(LightEvent* ev, ResetType type)
{
	ev->type = type;
	ev->uid = sceKernelCreateEventFlag("sh2v_event", SCE_EVENT_WAITMULTIPLE, 0, NULL);
}

void LightEvent_Clear(LightEvent* ev) { sceKernelClearEventFlag(ev->uid, ~1u); }
void LightEvent_Signal(LightEvent* ev) { sceKernelSetEventFlag(ev->uid, 1); }

void LightEvent_Wait(LightEvent* ev)
{
	int mode = SCE_EVENT_WAITOR | (ev->type == RESET_STICKY ? 0 : SCE_EVENT_WAITCLEAR);
	sceKernelWaitEventFlag(ev->uid, 1, mode, NULL, NULL);
}

int LightEvent_WaitTimeout(LightEvent* ev, s64 timeout_ns)
{
	int mode = SCE_EVENT_WAITOR | (ev->type == RESET_STICKY ? 0 : SCE_EVENT_WAITCLEAR);
	SceUInt us = timeout_ns > 0 ? (SceUInt)(timeout_ns / 1000) : 0;
	return sceKernelWaitEventFlag(ev->uid, 1, mode, NULL, &us) < 0 ? 1 : 0;
}

// ---- applet / config ----------------------------------------------------------------------------
volatile bool g_vita_quit;

bool aptMainLoop(void) { return !g_vita_quit; }
bool aptShouldClose(void) { return g_vita_quit; }
Result APT_SetAppCpuTimeLimit(u32 percent) { (void)percent; return 0; }

static bool s_apputil;

Result cfguInit(void)
{
	if (!s_apputil) {
		SceAppUtilInitParam init;
		SceAppUtilBootParam boot;
		memset(&init, 0, sizeof init);
		memset(&boot, 0, sizeof boot);
		if (sceAppUtilInit(&init, &boot) < 0)
			return -1;
		s_apputil = true;
	}
	return 0;
}

void cfguExit(void) {}

Result CFGU_GetSystemLanguage(u8* language)
{
	int lang = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
	sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &lang);
	switch (lang) {
	case SCE_SYSTEM_PARAM_LANG_JAPANESE: *language = CFG_LANGUAGE_JP; break;
	case SCE_SYSTEM_PARAM_LANG_FRENCH: *language = CFG_LANGUAGE_FR; break;
	case SCE_SYSTEM_PARAM_LANG_GERMAN: *language = CFG_LANGUAGE_DE; break;
	case SCE_SYSTEM_PARAM_LANG_ITALIAN: *language = CFG_LANGUAGE_IT; break;
	case SCE_SYSTEM_PARAM_LANG_SPANISH: *language = CFG_LANGUAGE_ES; break;
	default: *language = CFG_LANGUAGE_EN; break;
	}
	return 0;
}

// ---- y2r: not on the Vita; movie.c falls back to its CPU conversion ------------------------------
Result y2rInit(void) { return -1; }
void y2rExit(void) {}
Result Y2RU_SetTransferEndInterrupt(bool s) { (void)s; return -1; }
Result Y2RU_GetTransferEndEvent(Handle* e) { *e = 0; return -1; }
Result Y2RU_SetConversionParams(const Y2RU_ConversionParams* p) { (void)p; return -1; }
Result Y2RU_SetSendingY(const void* b, u32 s, s16 u, s16 g) { (void)b; (void)s; (void)u; (void)g; return -1; }
Result Y2RU_SetSendingU(const void* b, u32 s, s16 u, s16 g) { (void)b; (void)s; (void)u; (void)g; return -1; }
Result Y2RU_SetSendingV(const void* b, u32 s, s16 u, s16 g) { (void)b; (void)s; (void)u; (void)g; return -1; }
Result Y2RU_SetReceiving(void* b, u32 s, s16 u, s16 g) { (void)b; (void)s; (void)u; (void)g; return -1; }
Result Y2RU_StartConversion(void) { return -1; }
Result Y2RU_StopConversion(void) { return 0; }
