/**
 * @file paths.c
 * Where the port's files live (ROM, eeprom.bin, input.ini, future caches).
 * Two layouts:
 *
 *  portable:  a data/ directory under the working directory (the repo, a
 *             PortMaster port folder, an unzipped release) — everything
 *             stays inside it, as it always has.
 *  installed: no data/ in the cwd — resolve a per-user directory via
 *             SDL_GetPrefPath (~/.local/share/ge007/, %AppData%\ge007\,
 *             Android internal storage), so a bare binary run from
 *             anywhere still finds its save/config and the ROM error
 *             message names a sensible place to put it.
 *
 * PORT_DATA_DIR overrides the base outright.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "port.h"

#ifdef GE_HAVE_SDL2
#include <SDL.h>
#endif

static const char *sBase; /* no trailing slash */

static int isDir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

static const char *portPathBase(void)
{
    const char *env;

    if (sBase != NULL) {
        return sBase;
    }

    env = getenv("PORT_DATA_DIR");
    if (env != NULL && env[0] != '\0') {
        sBase = strdup(env);
        return sBase;
    }
    if (isDir("data")) {
        sBase = "data";
        return sBase;
    }
#if defined(GE_HAVE_SDL2) && defined(__ANDROID__)
    {
        /* app-scoped external storage (/sdcard/Android/data/<pkg>/files):
         * reachable with adb push / a file manager, unlike the pref path */
        const char *ext = SDL_AndroidGetExternalStoragePath();
        if (ext != NULL) {
            sBase = strdup(ext);
            return sBase;
        }
    }
#endif
#ifdef GE_HAVE_SDL2
    {
        /* SDL creates the directory; returned path has a trailing
         * separator, which portPathFile's "/" tolerates on every OS. */
        char *pref = SDL_GetPrefPath("", "ge007");
        if (pref != NULL) {
            size_t n = strlen(pref);
            char *base = malloc(n + 1);
            memcpy(base, pref, n + 1);
            while (n > 0 && (base[n - 1] == '/' || base[n - 1] == '\\')) {
                base[--n] = '\0';
            }
            SDL_free(pref);
            sBase = base;
            fprintf(stderr, "port/paths: no ./data — using %s\n", sBase);
            return sBase;
        }
    }
#endif
    sBase = "data";
    return sBase;
}

/* "<base>/<name>" into buf; returns buf. */
const char *portPathFile(char *buf, size_t len, const char *name)
{
    snprintf(buf, len, "%s/%s", portPathBase(), name);
    return buf;
}
