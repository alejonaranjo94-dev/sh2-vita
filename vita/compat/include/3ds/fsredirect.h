// sh2-vita: sends the game's file calls through vita_path(), so "sdmc:/3ds/sh23ds/..." becomes
// "ux0:data/sh2vita/...". Included at the end of <3ds.h>, after the C library headers the game
// includes first; a game file that includes <stdio.h> after <3ds.h> still gets these macros because
// the include guards stop stdio.h from being read twice.
#pragma once

#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef SH2VITA_NO_FS_REDIRECT
#ifdef __cplusplus
extern "C" {
#endif
FILE* vita_fopen(const char* path, const char* mode);
DIR* vita_opendir(const char* path);
int vita_stat(const char* path, struct stat* st);
int vita_mkdir(const char* path, mode_t mode);
int vita_remove(const char* path);
int vita_rename(const char* from, const char* to);
int vita_unlink(const char* path);
#ifdef __cplusplus
}
#endif

#define fopen(p, m) vita_fopen((p), (m))
#define opendir(p) vita_opendir((p))
#define stat(p, s) vita_stat((p), (s))
#define mkdir(p, m) vita_mkdir((p), (m))
#define remove(p) vita_remove((p))
#define rename(a, b) vita_rename((a), (b))
#define unlink(p) vita_unlink((p))
#endif
