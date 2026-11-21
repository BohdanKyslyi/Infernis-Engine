#pragma once
#include "RainConfigEdit.h"
#include <io.h>

namespace weather_effect_config {
// Read the physical override first. FS.r_open alone cannot recover an
// unindexed .ltx, while .ogg/.xml have a special fallback in LocatorAPI.
inline bool read(LPCSTR path, std::string& bytes, bool& exists) {
    exists = false;
    const auto result = rain_config::read_loose_file(path, bytes);
    if (result == rain_config::ReadResult::loaded) {
        exists = true;
    } else if (result == rain_config::ReadResult::error) {
        Msg("! Weather editor: weather effect config disk read failed [%s], errno=%d", path, errno);
        return false;
    } else {
        if (!FS.exist(path)) return true;
        exists = true; // A packed config is still a valid source.
        IReader* reader = FS.r_open(path);
        if (!reader) {
            Msg("! Weather editor: weather effect config archive read failed [%s]", path);
            return false;
        }
        bytes.assign(static_cast<const char*>(reader->pointer()), reader->length());
        FS.r_close(reader);
    }
    if (bytes.size() > 0x7fffffffU) {
        Msg("! Weather editor: weather effect config is too large [%s]", path);
        return false;
    }
    return true;
}


inline bool write(LPCSTR source_path, const std::string& text, const std::string& original, bool has_original) {
    // The VFS lowercases resolved paths. Preserve the filename for native writes.
    string_path directory, path;
    FS.update_path(directory, "$game_config$", "environment\\");
    xr_strcpy(path, directory);
    xr_strcat(path, "noirWeatherEffect.ltx");

    string_path temporary, backup;
    xr_strcpy(temporary, path);
    xr_strcat(temporary, ".weather_editor.tmp");
    createPath(temporary);
    const char* stage = nullptr;
    if (!rain_config::write_verified_file(temporary, text, stage)) {
        Msg("! Weather editor: weather effect config %s failed [%s], errno=%d", stage, temporary, errno);
        return false;
    }
    xr_strcpy(backup, source_path);
    xr_strcat(backup, ".weather_editor.bak");
    if (has_original && _access(backup, 0) != 0) {
        if (!rain_config::write_verified_file(backup, original, stage)) {
            Msg("! Weather editor: weather effect backup %s failed [%s], errno=%d", stage, backup, errno);
            return false;
        }
    }
    const DWORD attributes = GetFileAttributesA(path);
    const bool read_only = attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_READONLY);
    if (read_only && !SetFileAttributesA(path, attributes & ~FILE_ATTRIBUTE_READONLY)) {
        Msg("! Weather editor: cannot make weather effect config writable [%s], win_error=%lu", path, GetLastError());
        return false;
    }
    // Replace without unlinking the original first. A failed replacement keeps it intact.
    if (!MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        if (read_only) SetFileAttributesA(path, attributes);
        Msg("! Weather editor: weather effect config replacement failed [%s], win_error=%lu", path, error);
        return false;
    }
    // Native writes must be made visible to subsequent CInifile/VFS reads.
    FS.rescan_path(directory, FALSE);
    Msg("* Weather editor: saved weather effect settings [%s]", path);
    return true;
}
} // namespace weather_effect_config
