#include "stdafx.h"
#pragma hdrstop

#include "SoundRender_Core.h"
#include "SoundRender_Source.h"
#include <sys/stat.h>

static bool source_file_exists(LPCSTR name) {
    string_path physical;
    if (FS.exist(physical, "$game_sounds$", name)) return true;
    FS.update_path(physical, "$game_sounds$", name);
    struct _stat info;
    return _stat(physical, &info) == 0 && info.st_size > 0;
}

bool snd_localized_exists(LPCSTR name) {
    if (source_file_exists(name)) return true;
    if (!snd_language[0] || !xr_strcmp(snd_language, "default")) return false;
    for (const char* p = snd_language; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return false;
    string_path localized;
    xr_sprintf(localized, "localization\\%s\\%s", snd_language, name);
    return source_file_exists(localized);
}

CSoundRender_Source* CSoundRender_Core::i_create_source(LPCSTR name) {
    // Search
    string_path id;
    xr_strcpy(id, name);
    strlwr(id);
    if (strext(id))
        *strext(id) = 0;
    // Language packs mirror paths under gamedata/sounds/localization/<language>/.
    // Each missing file keeps the unprefixed sound from the original game.
    if (snd_language[0] && xr_strcmp(snd_language, "default") && xr_strlen(snd_language) < 32) {
        bool safe = true;
        for (const char* p = snd_language; *p; ++p)
            if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) safe = false;
        if (safe) {
            string_path localized;
            xr_sprintf(localized, "localization\\%s\\%s.ogg", snd_language, id);
            if (source_file_exists(localized)) {
                xr_sprintf(id, "localization\\%s\\%s", snd_language, name);
                strlwr(id);
                if (strext(id)) *strext(id) = 0;
            }
        }
    }
    for (u32 it = 0; it < s_sources.size(); it++) {
        if (0 == xr_strcmp(*s_sources[it]->fname, id))
            return s_sources[it];
    }

    // Load a _new one
    CSoundRender_Source* S = xr_new<CSoundRender_Source>();
    S->load(id);
    s_sources.push_back(S);
    return S;
}

void CSoundRender_Core::i_destroy_source(CSoundRender_Source* S) {
    // No actual destroy at all
}
