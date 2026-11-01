#pragma once

#include <string>
#include <cstdio>
#include <cerrno>

namespace rain_config {

enum class ReadResult { missing, loaded, error };

// A loose config takes precedence even when the VFS index has not seen it.
// On error leave the caller's bytes intact so it cannot overwrite an unread file.
inline ReadResult read_loose_file(const char* path, std::string& data) {
    std::FILE* file = std::fopen(path, "rb");
    if (!file) return errno == ENOENT ? ReadResult::missing : ReadResult::error;
    std::string bytes;
    char buffer[4096];
    for (;;) {
        const std::size_t count = std::fread(buffer, 1, sizeof(buffer), file);
        bytes.append(buffer, count);
        if (count != sizeof(buffer)) {
            if (std::ferror(file)) {
                const int error = errno ? errno : EIO;
                std::fclose(file);
                errno = error;
                return ReadResult::error;
            }
            break;
        }
    }
    if (std::fclose(file) != 0) return ReadResult::error;
    data.swap(bytes);
    return ReadResult::loaded;
}

// Close before verification; do not rely on cached VFS metadata.
inline bool write_verified_file(const char* path, const std::string& data, const char*& stage) {
    stage = "open";
    std::FILE* file = std::fopen(path, "wb");
    if (!file) return false;
    stage = "write";
    bool ok = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    if (ok) {
        stage = "flush";
        ok = std::fflush(file) == 0;
    }
    const int write_error = errno;
    const int close_result = std::fclose(file);
    if (!ok) { errno = write_error; return false; }
    stage = "close";
    if (close_result != 0) return false;

    stage = "verify open";
    file = std::fopen(path, "rb");
    if (!file) return false;
    std::string actual(data.size() + 1, '\0');
    const std::size_t count = std::fread(&actual[0], 1, actual.size(), file);
    const bool read_ok = std::ferror(file) == 0;
    const int read_error = errno;
    std::fclose(file);
    stage = "verify bytes";
    if (!read_ok) { errno = read_error; return false; }
    if (count != data.size() || actual.compare(0, count, data) != 0) {
        errno = EIO;
        return false;
    }
    return true;
}

// Patch only the requested value; retain comments, encoding and unknown keys.
inline std::string set_dynamic_wind_value(const std::string& source,
    const std::string& key, const std::string& value) {
    const std::string newline = source.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string result;
    bool in_section = false;
    bool found_section = false;
    bool found_key = false;
    const auto append_key = [&]() {
        if (!result.empty() && result.back() != '\n') result += newline;
        result += key + " = " + value + newline;
    };
    for (std::size_t start = 0; start < source.size();) {
        const std::size_t lf = source.find('\n', start);
        const std::size_t end = lf == std::string::npos ? source.size() : lf + 1;
        std::string line = source.substr(start, end - start);
        std::size_t first = line.find_first_not_of(" \t\r\n");
        // A UTF-8 BOM is part of the file, not of the first section name.
        if (start == 0 && line.compare(0, 3, "\xef\xbb\xbf") == 0)
            first = line.find_first_not_of(" \t\r\n", 3);
        if (first != std::string::npos && line[first] == '[') {
            if (in_section && !found_key) append_key();
            const std::size_t close = line.find(']', first);
            in_section = close != std::string::npos &&
                line.substr(first + 1, close - first - 1) == "dynamic_wind";
            if (in_section) { found_section = true; found_key = false; }
        } else if (in_section && first != std::string::npos &&
                   line[first] != ';' && line.compare(first, 2, "//") != 0) {
            const std::size_t equals = line.find('=', first);
            const std::size_t comment = line.find(';', first);
            const std::size_t slash_comment = line.find("//", first);
            if (equals != std::string::npos && equals < comment && equals < slash_comment) {
                const std::size_t key_end = line.find_last_not_of(" \t", equals - 1);
                if (key_end != std::string::npos &&
                    line.substr(first, key_end - first + 1) == key) {
                    const std::size_t non_space = line.find_first_not_of(" \t", equals + 1);
                    const std::size_t value_start = non_space == std::string::npos
                        ? line.size() : non_space;
                    const std::size_t value_end = line.find_first_of(" \t;\r\n/", value_start);
                    line.replace(value_start, value_end == std::string::npos
                        ? line.size() - value_start : value_end - value_start, value);
                    found_key = true;
                }
            }
        }
        result += line;
        start = end;
    }
    if (in_section && !found_key) append_key();
    if (!found_section) {
        if (!result.empty() && result.back() != '\n') result += newline;
        result += "[dynamic_wind]" + newline;
        append_key();
    }
    return result;
}

inline std::string set_tilt_multiplier(const std::string& source, const std::string& value) {
    return set_dynamic_wind_value(source, "tilt_multiplier", value);
}

} // namespace rain_config
