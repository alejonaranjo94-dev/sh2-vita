// sh2-vita: file paths and the 3DS's direct file API.
//
// The game reads its data from "sdmc:/3ds/sh23ds/..." (the converted pack on the SD card). On the
// Vita the pack lives in ux0:data/sh2vita/, and the app's own read-only files in app0:/.
#define SH2VITA_NO_FS_REDIRECT
#include <3ds.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "vita_compat.h"

#define RING 16
#define PATH_MAX_LEN 512

static char s_ring[RING][PATH_MAX_LEN];
static volatile unsigned s_next;

static const struct { const char* from; const char* to; } MAP[] = {
	{ "sdmc:/3ds/sh23ds", VITA_DATA_DIR },
	{ "sdmc:/", VITA_DATA_DIR "/sdmc/" },
	{ "romfs:/", "app0:/" },
};

const char* vita_path(const char* path)
{
	if (!path)
		return path;
	for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++) {
		size_t n = strlen(MAP[i].from);
		if (strncmp(path, MAP[i].from, n) == 0) {
			// Several threads open files at once (streaming, sound, music); a ring of buffers keeps
			// each result alive until the call that asked for it has used it.
			char* out = s_ring[__atomic_fetch_add(&s_next, 1, __ATOMIC_RELAXED) % RING];
			snprintf(out, PATH_MAX_LEN, "%s%s", MAP[i].to, path + n);
			return out;
		}
	}
	return path;
}

FILE* vita_fopen(const char* path, const char* mode) { return fopen(vita_path(path), mode); }
DIR* vita_opendir(const char* path) { return opendir(vita_path(path)); }
int vita_stat(const char* path, struct stat* st) { return stat(vita_path(path), st); }
int vita_mkdir(const char* path, mode_t mode) { return mkdir(vita_path(path), mode); }
int vita_remove(const char* path) { return remove(vita_path(path)); }
int vita_unlink(const char* path) { return unlink(vita_path(path)); }

int vita_rename(const char* from, const char* to)
{
	// vita_path returns ring buffers, so both results stay valid here.
	return rename(vita_path(from), vita_path(to));
}

// ---- the direct file API (FSUSER/FSFILE), used for writes that must reach the card -----------
FS_Path fsMakePath(FS_PathType type, const void* path)
{
	FS_Path p = { type, type == PATH_ASCII ? (u32)strlen((const char*)path) + 1 : 1, path };
	return p;
}

#define MAX_FILES 8
static FILE* s_files[MAX_FILES + 1];   // handle n is s_files[n]; 0 is never used

Result FSUSER_OpenFileDirectly(Handle* out, FS_ArchiveID archive_id, FS_Path archive_path, FS_Path file_path,
                               u32 open_flags, u32 attributes)
{
	(void)archive_id; (void)archive_path; (void)attributes;
	if (file_path.type != PATH_ASCII)
		return -1;
	char path[PATH_MAX_LEN];
	snprintf(path, sizeof path, "sdmc:%s%s", ((const char*)file_path.data)[0] == '/' ? "" : "/",
	         (const char*)file_path.data);
	const char* real = vita_path(path);

	const char* mode = "rb";
	if (open_flags & FS_OPEN_WRITE) {
		FILE* probe = fopen(real, "rb");
		if (probe) {
			fclose(probe);
			mode = "r+b";
		} else if (open_flags & FS_OPEN_CREATE) {
			mode = "w+b";
		} else {
			return -1;
		}
	}
	for (Handle h = 1; h <= MAX_FILES; h++) {
		if (!s_files[h]) {
			s_files[h] = fopen(real, mode);
			if (!s_files[h])
				return -1;
			*out = h;
			return 0;
		}
	}
	return -1;
}

static FILE* file_of(Handle h) { return h >= 1 && h <= MAX_FILES ? s_files[h] : NULL; }

Result FSFILE_Read(Handle h, u32* bytes_read, u64 offset, void* buffer, u32 size)
{
	FILE* f = file_of(h);
	if (!f || fseek(f, (long)offset, SEEK_SET) != 0)
		return -1;
	size_t n = fread(buffer, 1, size, f);
	if (bytes_read)
		*bytes_read = (u32)n;
	return 0;
}

Result FSFILE_Write(Handle h, u32* bytes_written, u64 offset, const void* buffer, u32 size, u32 flags)
{
	FILE* f = file_of(h);
	if (!f || fseek(f, (long)offset, SEEK_SET) != 0)
		return -1;
	size_t n = fwrite(buffer, 1, size, f);
	if (flags & FS_WRITE_FLUSH)
		fflush(f);
	if (bytes_written)
		*bytes_written = (u32)n;
	return n == size ? 0 : -1;
}

Result FSFILE_GetSize(Handle h, u64* size)
{
	FILE* f = file_of(h);
	if (!f)
		return -1;
	long cur = ftell(f);
	fseek(f, 0, SEEK_END);
	*size = (u64)ftell(f);
	fseek(f, cur, SEEK_SET);
	return 0;
}

Result FSFILE_SetSize(Handle h, u64 size)
{
	FILE* f = file_of(h);
	if (!f)
		return -1;
	fflush(f);
	return ftruncate(fileno(f), (off_t)size) == 0 ? 0 : -1;
}

Result FSFILE_Close(Handle h)
{
	FILE* f = file_of(h);
	if (!f)
		return -1;
	fclose(f);
	s_files[h] = NULL;
	return 0;
}
