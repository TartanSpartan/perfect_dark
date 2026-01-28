#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <PR/ultratypes.h>
#include "config.h"
#include "system.h"
#include "platform.h"
#include "utils.h"
#include "fs.h"
#ifdef PLATFORM_WIN32
#include <direct.h>
#endif

#define DEFAULT_BASEDIR_NAME "data"

static char baseDir[FS_MAXPATH + 1]; // replaces $B
static char modDir[FS_MAXPATH + 1];  // replaces $M
static char saveDir[FS_MAXPATH + 1]; // replaces $S
static char homeDir[FS_MAXPATH + 1]; // replaces $H
static char exeDir[FS_MAXPATH + 1];  // replaces $E

static s32 fsPathIsWritable(const char *path)
{
#ifdef PLATFORM_WIN32
	// on windows access() on directories will only check if the directory exists, so
	char tmp[FS_MAXPATH + 1] = { 0 };
	snprintf(tmp, sizeof(tmp), "%s/.tmp", path);
	FILE *f = fopen(tmp, "wb");
	if (f) {
		fclose(f);
		remove(tmp);
		return 1;
	}
	return 0;
#else
	return (access(path, W_OK) == 0);
#endif
}

// Create directories recursively for a given full path. This will try to
// create each intermediate component so that a later fopen("wb") can
// succeed even when parent directories are missing (useful on platforms
// where the SDL pref path may not create all components).
static void fsCreateDirRecursive(const char *fullpath)
{
	char tmp[FS_MAXPATH + 1];
	size_t len = strlen(fullpath);

	if (len == 0 || len >= sizeof(tmp)) {
		return;
	}

	// copy the path and ensure null termination
	strncpy(tmp, fullpath, sizeof(tmp));
	tmp[sizeof(tmp) - 1] = '\0';

	// Skip over path prefixes that shouldn't be created as directories:
	// Unix root (/), Windows drive (C:), or Vita device (ux0:)
	char *p = tmp;
	if (p[0] == '/') {
		p++;
	} else {
		// Skip device/drive prefix (e.g., "ux0:" or "C:")
		char *colon = strchr(p, ':');
		if (colon && colon - p <= 3) {
			p = colon + 1;
			if (*p == '/') p++;
		}
	}

	// Iterate through the path, creating each prefix when missing
	for (; *p; ++p) {
		if (*p == '/') {
			*p = '\0';
			if (fsFileSize(tmp) < 0) {
				if (fsCreateDir(tmp) != 0) {
					sysLogPrintf(LOG_WARNING, "fsCreateDirRecursive: could not create %s", tmp);
				}
			}
			*p = '/';
		}
	}

	// create the final directory
	if (fsFileSize(tmp) < 0) {
		if (fsCreateDir(tmp) != 0) {
			sysLogPrintf(LOG_WARNING, "fsCreateDirRecursive: could not create %s", tmp);
		}
	}
}

s32 fsPathIsAbsolute(const char *path)
{
	// Check for Unix absolute path, Windows drive letter (C:), or Vita device (ux0:)
	return (path[0] == '/' || (isalpha((unsigned char)path[0]) && path[1] == ':') || (strlen(path) > 3 && isalpha((unsigned char)path[0]) && path[3] == ':'));
}

s32 fsPathIsCwdRelative(const char *path)
{
	// ., .., ./, ../
	return (path[0] == '.' && (path[1] == '.' || path[1] == '/' || path[1] == '\\' || path[1] == '\0'));
}

const char *fsFullPath(const char *relPath)
{
	static char pathBuf[FS_MAXPATH + 1];

	if (relPath[0] == '$') {
		// expandable placeholder $X; will be replaced with the corresponding path, if any
		const char *expStr = NULL;
		switch (relPath[1]) {
			case 'E': expStr = exeDir; break;
			case 'H': expStr = homeDir; break;
			case 'M': expStr = modDir; break;
			case 'B': expStr = baseDir; break;
			case 'S': expStr = saveDir; break;
			default: break;
		}
		if (expStr) {
			const u32 len = strlen(expStr);
			if (len > 0) {
				memcpy(pathBuf, expStr, len);
				strncpy(pathBuf + len, relPath + 2, FS_MAXPATH - len);
				return pathBuf;
			}
		}
		// couldn't expand anything, return as is
		return relPath;
	} else if (!baseDir[0] || fsPathIsAbsolute(relPath) || fsPathIsCwdRelative(relPath)) {
		// user explicitly wants working directory or this is an absolute path or we have no baseDir set up yet
		return relPath;
	}

	// path relative to mod or base dir; this will be a read request, so check where the file actually is
	if (modDir[0]) {
		snprintf(pathBuf, FS_MAXPATH, "%s/%s", modDir, relPath);
		if (fsFileSize(pathBuf) >= 0) {
			return pathBuf;
		}
	}
	// fall back to basedir
	snprintf(pathBuf, FS_MAXPATH, "%s/%s", baseDir, relPath);
	return pathBuf;
}

s32 fsInit(void)
{
	sysGetExecutablePath(exeDir, FS_MAXPATH);

	// if this is set, default to exe path for everything
	const s32 portable = sysArgCheck("--portable");
	if (portable) {
		strcpy(homeDir, exeDir);
	} else {
		sysGetHomePath(homeDir, FS_MAXPATH);
	}

	// get path to base dir and expand it if needed
	const char *path = sysArgGetString("--basedir");
	if (!path) {
		// check if there's a `data` directory in working directory or homeDir, otherwise default to exe directory
		path = "$E/" DEFAULT_BASEDIR_NAME;
		if (!portable) {
			if (fsFileSize("./" DEFAULT_BASEDIR_NAME) >= 0) {
				path = "./" DEFAULT_BASEDIR_NAME;
			} else if (fsFileSize("$H/" DEFAULT_BASEDIR_NAME) >= 0) {
				path = "$H/" DEFAULT_BASEDIR_NAME;
			}
		}
	}
	strncpy(baseDir, fsFullPath(path), FS_MAXPATH);

	// get path to mod dir and expand it if needed
	// mod directory is overlaid on top of base directory
	path = sysArgGetString("--moddir");
	if (path) {
		if (fsPathIsAbsolute(path) || fsPathIsCwdRelative(path) || path[0] == '$') {
			// path is explicit; check as-is
			if (fsFileSize(path) >= 0) {
				strncpy(modDir, fsFullPath(path), FS_MAXPATH);
			}
		} else {
			// path is relative to workdir; try to find it
			const char *priority[] = { ".", "$E", "$H" };
			for (s32 i = 0; i < 2 + (portable != 0); ++i) {
				char *tmp = strFmt("%s/%s", priority[i], path);
				if (fsFileSize(tmp) >= 0) {
					strncpy(modDir, fsFullPath(tmp), FS_MAXPATH);
					break;
				}
			}
		}
		if (!modDir[0]) {
			sysLogPrintf(LOG_WARNING, "could not find specified moddir `%s`", path);
		}
	}

	// get path to save dir and expand it if needed
	path = sysArgGetString("--savedir");
	if (!path) {
		if (portable) {
			path = "$E";
		} else {
#if defined(PLATFORM_LINUX) || defined(PLATFORM_OSX)
			// check if there's a config in the working directory, otherwise default to homeDir
			if (fsFileSize("./" CONFIG_FNAME) >= 0) {
				path = ".";
			} else {
				path = "$H";
			}
#else
			// check if working directory is writable, otherwise default to homeDir
			if (fsPathIsWritable("./")) {
				path = ".";
			} else {
				sysLogPrintf(LOG_WARNING, "cannot write to working directory, will use %s for saves instead", homeDir);
				path = "$H";
			}
#endif
		}
	}

	strncpy(saveDir, fsFullPath(path), FS_MAXPATH);

	// Ensure the save directory exists. On some platforms (e.g. Vita)
	// the preference path may not be present and fopen() will fail when
	// trying to create files. Try to create it here to avoid later
	// write failures when saving (mp setups, configs, etc.). Players
	// may wish to import their saves from multiplayer-friendly platforms
	// like PSTV, and should be able to save configs e.g. for solo play
	// of Combat Simulator on Vita. Create intermediate directories
	// as needed.
	if (fsFileSize(saveDir) < 0) {
		fsCreateDirRecursive(saveDir);
		if (fsCreateDir(saveDir) < 0) {
			sysLogPrintf(LOG_WARNING, "fsInit: could not create save dir %s", saveDir);
		}
	}

	if (modDir[0]) {
		sysLogPrintf(LOG_NOTE, " mod dir: %s", modDir);
	}
	sysLogPrintf(LOG_NOTE, "base dir: %s", baseDir);
	sysLogPrintf(LOG_NOTE, "save dir: %s", saveDir);

	return 0;
}

const char *fsGetModDir(void)
{
	return modDir[0] ? modDir : NULL;
}

s32 fsFileLoadTo(const char *name, void *dst, u32 dstSize)
{
	const char *fullName = fsFullPath(name);

	FILE *f = fopen(fullName, "rb");
	if (!f) {
		return -1;
	}

	fseek(f, 0, SEEK_END);
	const s32 size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size < 0) {
		sysLogPrintf(LOG_ERROR, "fsFileLoadTo: empty file or invalid size (%d): %s", size, fullName);
		fclose(f);
		return -1;
	}

	if ((u32)size > dstSize) {
		sysLogPrintf(LOG_ERROR, "fsFileLoadTo: file too big for buffer (%u > %u): %s", size, dstSize, fullName);
		fclose(f);
		return -1;
	}

	fread(dst, 1, size, f);
	fclose(f);

	return size;
}

void *fsFileLoad(const char *name, u32 *outSize)
{
	const char *fullName = fsFullPath(name);

	FILE *f = fopen(fullName, "rb");
	if (!f) {
		sysLogPrintf(LOG_ERROR, "fsFileLoad: could not find file: %s", fullName);
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	const s32 size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size < 0) {
		sysLogPrintf(LOG_ERROR, "fsFileLoad: empty file or invalid size (%d): %s", size, fullName);
		fclose(f);
		return NULL;
	}

	void *buf = NULL;
	if (size) {
		buf = sysMemZeroAlloc(size + 1); // sick hack for a free null terminator
		if (!buf) {
			sysLogPrintf(LOG_ERROR, "fsFileLoad: could not alloc %d bytes for file: %s", size, fullName);
			fclose(f);
			return NULL;
		}
		fread(buf, 1, size, f);
	}

	fclose(f);

	if (outSize) {
		*outSize = size;
	}

	return buf;
}

s32 fsFileSize(const char *name)
{
	const char *fullName = fsFullPath(name);
	struct stat st;
	if (stat(fullName, &st) < 0) {
		return -1;
	} else {
		return st.st_size;
	}
}

FILE *fsFileOpenWrite(const char *name)
{
	return fopen(fsFullPath(name), "wb");
}

FILE *fsFileOpenRead(const char *name)
{
	return fopen(fsFullPath(name), "rb");
}

void fsFileFree(FILE *f)
{
	fclose(f);
}

s32 fsCreateDir(const char *path)
{
#ifdef PLATFORM_WIN32
	return _mkdir(fsFullPath(path));
#else
	return mkdir(fsFullPath(path), 0777);
#endif
}
