// sh2-vita: stand-in for libctru's <3ds.h>.
//
// The game code (port/source) was written for the 3DS and includes <3ds.h>. On the Vita this
// header takes its place: it declares the part of the libctru API the game uses, with the same
// names and argument types, and vita/compat/src/*.c implements it on top of the Vita's system.
// Only what the game calls is here. When a new upstream change uses something missing, the build
// fails with an "undeclared" error, and the fix is to add it here and in the matching .c file.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- types ---------------------------------------------------------------------------------
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef volatile u32 vu32;
typedef volatile u8 vu8;
typedef s32 Result;
typedef u32 Handle;

#define U64_MAX UINT64_MAX
#define U32_MAX UINT32_MAX

// memory barrier (libctru's synchronization.h)
static inline void __dmb(void) { __sync_synchronize(); }
static inline void __dsb(void) { __sync_synchronize(); }

#define BIT(n) (1U << (n))
#define ALIGN(m) __attribute__((aligned(m)))
#define PACKED __attribute__((packed))
#ifndef UNUSED
#define UNUSED __attribute__((unused))
#endif

#define R_SUCCEEDED(res) ((res) >= 0)
#define R_FAILED(res) ((res) < 0)

#define CUR_PROCESS_HANDLE 0xFFFF8001
#define CUR_THREAD_HANDLE 0xFFFF8000

// ---- time ----------------------------------------------------------------------------------
// The game measures time in ARM11 ticks. The compat layer keeps that unit, so every
// "ticks / SYSCLOCK_ARM11" in the game still means seconds.
#define SYSCLOCK_ARM11 268111856ULL
#define CPU_TICKS_PER_MSEC (SYSCLOCK_ARM11 / 1000.0)
#define CPU_TICKS_PER_USEC (SYSCLOCK_ARM11 / 1000000.0)

u64 svcGetSystemTick(void);
void svcSleepThread(s64 ns);
u64 osGetTime(void);                 // milliseconds
void osSetSpeedupEnable(bool enable);
float osGet3DSliderState(void);   // the 3D slider: always 0 on the Vita

// ---- svc odds and ends ----------------------------------------------------------------------
typedef enum { USERBREAK_PANIC = 0, USERBREAK_ASSERT = 1, USERBREAK_USER = 2 } UserBreakType;
void svcBreak(UserBreakType reason) __attribute__((noreturn));
s32 svcGetProcessorID(void);
Result svcGetThreadPriority(s32* out, Handle thread);
Result svcFlushProcessDataCache(Handle process, u32 addr, u32 size);
Result svcWaitSynchronization(Handle h, s64 ns);
Result svcClearEvent(Handle h);
Result svcCloseHandle(Handle h);

typedef enum { RESLIMIT_COMMIT = 1 } ResourceLimitType;
Result svcGetResourceLimit(Handle* out, Handle process);
Result svcGetResourceLimitLimitValues(s64* values, Handle res, ResourceLimitType* names, s32 count);
Result svcGetResourceLimitCurrentValues(s64* values, Handle res, ResourceLimitType* names, s32 count);

typedef enum {
	MEMOP_FREE = 1, MEMOP_ALLOC = 3, MEMOP_ALLOC_LINEAR = 0x10003,
} MemOp;
typedef enum { MEMPERM_READ = 1, MEMPERM_WRITE = 2 } MemPerm;
Result svcControlMemory(u32* addr_out, u32 addr0, u32 addr1, u32 size, MemOp op, MemPerm perm);
#define OS_HEAP_AREA_BEGIN 0x08000000

// ---- memory shared with the GPU --------------------------------------------------------------
// On the 3DS this memory is visible to the GPU; on the Vita, vitaGL copies what it needs, so it
// is plain heap memory and the cache calls do nothing.
void* linearAlloc(size_t size);
void* linearMemAlign(size_t size, size_t alignment);
void linearFree(void* mem);
u32 linearSpaceFree(void);
Result GSPGPU_FlushDataCache(const void* adr, u32 size);
Result GSPGPU_InvalidateDataCache(const void* adr, u32 size);
void* vramAlloc(size_t size);
void vramFree(void* mem);
u32 vramSpaceFree(void);
Result DSP_FlushDataCache(const void* address, u32 size);

// ---- threads and locks -----------------------------------------------------------------------
typedef struct Thread_tag* Thread;
typedef void (*ThreadFunc)(void*);
Thread threadCreate(ThreadFunc entry, void* arg, size_t stack_size, int prio, int core_id, bool detached);
Result threadJoin(Thread thread, u64 timeout_ns);
void threadFree(Thread thread);

typedef struct { s32 uid; } LightLock;
void LightLock_Init(LightLock* lock);
void LightLock_Lock(LightLock* lock);
int LightLock_TryLock(LightLock* lock);
void LightLock_Unlock(LightLock* lock);

typedef enum { RESET_ONESHOT = 0, RESET_STICKY = 1, RESET_PULSE = 2 } ResetType;
typedef struct { s32 uid; ResetType type; } LightEvent;
void LightEvent_Init(LightEvent* ev, ResetType type);
void LightEvent_Clear(LightEvent* ev);
void LightEvent_Signal(LightEvent* ev);
void LightEvent_Wait(LightEvent* ev);
int LightEvent_WaitTimeout(LightEvent* ev, s64 timeout_ns);  // 0 signalled, 1 timed out

// ---- applet / system -------------------------------------------------------------------------
bool aptMainLoop(void);
bool aptShouldClose(void);
Result APT_SetAppCpuTimeLimit(u32 percent);

typedef enum {
	CFG_LANGUAGE_JP = 0, CFG_LANGUAGE_EN = 1, CFG_LANGUAGE_FR = 2, CFG_LANGUAGE_DE = 3,
	CFG_LANGUAGE_IT = 4, CFG_LANGUAGE_ES = 5, CFG_LANGUAGE_ZH = 6, CFG_LANGUAGE_KO = 7,
	CFG_LANGUAGE_NL = 8, CFG_LANGUAGE_PT = 9, CFG_LANGUAGE_RU = 10, CFG_LANGUAGE_TW = 11,
} CFG_Language;
Result cfguInit(void);
void cfguExit(void);
Result CFGU_GetSystemLanguage(u8* language);

// ---- input -----------------------------------------------------------------------------------
// The 3DS key bits. vita/compat/src/hid.c fills them from the Vita's buttons and sticks.
enum {
	KEY_A = BIT(0), KEY_B = BIT(1), KEY_SELECT = BIT(2), KEY_START = BIT(3),
	KEY_DRIGHT = BIT(4), KEY_DLEFT = BIT(5), KEY_DUP = BIT(6), KEY_DDOWN = BIT(7),
	KEY_R = BIT(8), KEY_L = BIT(9), KEY_X = BIT(10), KEY_Y = BIT(11),
	KEY_ZL = BIT(14), KEY_ZR = BIT(15), KEY_TOUCH = BIT(20),
	KEY_CSTICK_RIGHT = BIT(24), KEY_CSTICK_LEFT = BIT(25), KEY_CSTICK_UP = BIT(26), KEY_CSTICK_DOWN = BIT(27),
	KEY_CPAD_RIGHT = BIT(28), KEY_CPAD_LEFT = BIT(29), KEY_CPAD_UP = BIT(30), KEY_CPAD_DOWN = BIT(31),
	KEY_UP = KEY_DUP | KEY_CPAD_UP, KEY_DOWN = KEY_DDOWN | KEY_CPAD_DOWN,
	KEY_LEFT = KEY_DLEFT | KEY_CPAD_LEFT, KEY_RIGHT = KEY_DRIGHT | KEY_CPAD_RIGHT,
};
typedef struct { u16 px, py; } touchPosition;
typedef struct { s16 dx, dy; } circlePosition;

void hidScanInput(void);
u32 hidKeysDown(void);
u32 hidKeysHeld(void);
u32 hidKeysUp(void);
void hidTouchRead(touchPosition* pos);
void hidCircleRead(circlePosition* pos);
void hidCstickRead(circlePosition* pos);
bool hidShouldUseIrrst(void);

// ---- screens ---------------------------------------------------------------------------------
typedef enum { GFX_TOP = 0, GFX_BOTTOM = 1 } gfxScreen_t;
typedef enum { GFX_LEFT = 0, GFX_RIGHT = 1 } gfx3dSide_t;
void gfxInitDefault(void);
void gfxExit(void);
u8* gfxGetFramebuffer(gfxScreen_t screen, gfx3dSide_t side, u16* width, u16* height);
void gfxFlushBuffers(void);
void gfxSwapBuffers(void);
void gfxSet3D(bool enable);
void gspWaitForVBlank(void);

typedef struct { u16* frameBuffer; int consoleWidth, consoleHeight, cursorX, cursorY; } PrintConsole;
PrintConsole* consoleInit(gfxScreen_t screen, PrintConsole* console);
PrintConsole* consoleSelect(PrintConsole* console);
void consoleClear(void);

// ---- files -----------------------------------------------------------------------------------
// The game opens most files with stdio and "sdmc:/3ds/sh23ds/..." paths; vita/compat/src/fs.c
// wraps fopen and friends so those paths land in ux0:data/sh2vita/.
typedef enum { PATH_INVALID = 0, PATH_EMPTY = 1, PATH_BINARY = 2, PATH_ASCII = 3, PATH_UTF16 = 4 } FS_PathType;
typedef struct { FS_PathType type; u32 size; const void* data; } FS_Path;
typedef enum { ARCHIVE_SDMC = 9 } FS_ArchiveID;
typedef u64 FS_Archive;
enum { FS_OPEN_READ = BIT(0), FS_OPEN_WRITE = BIT(1), FS_OPEN_CREATE = BIT(2) };
enum { FS_WRITE_FLUSH = BIT(0) };
FS_Path fsMakePath(FS_PathType type, const void* path);
Result FSUSER_OpenFileDirectly(Handle* out, FS_ArchiveID archive_id, FS_Path archive_path, FS_Path file_path,
                               u32 open_flags, u32 attributes);
Result FSFILE_Read(Handle handle, u32* bytes_read, u64 offset, void* buffer, u32 size);
Result FSFILE_Write(Handle handle, u32* bytes_written, u64 offset, const void* buffer, u32 size, u32 flags);
Result FSFILE_GetSize(Handle handle, u64* size);
Result FSFILE_SetSize(Handle handle, u64 size);
Result FSFILE_Close(Handle handle);

// Maps a 3DS path ("sdmc:/3ds/sh23ds/x", "romfs:/x") to the Vita's ("ux0:data/sh2vita/x").
// Returns its argument when there is nothing to change. Not thread safe across calls with the same
// buffer: each thread gets its own.
const char* vita_path(const char* path);

// ---- audio (DSP) -----------------------------------------------------------------------------
#include "3ds/ndsp.h"

// ---- YUV->RGB hardware used by the movie player ----------------------------------------------
#include "3ds/y2r.h"

#ifdef __cplusplus
}
#endif

// fopen/opendir/stat/mkdir/remove/rename go through vita_path (fs.c).
#include "3ds/fsredirect.h"
