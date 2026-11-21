#pragma once
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace infernis_spawner::tools {
struct Bookmark {
    std::string name, level;
    float x = 0, y = 0, z = 0;
};
inline bool clean_text(const std::string& value, size_t limit = 128) {
    if (value.empty() || value.size() > limit) return false;
    for (unsigned char c : value) if (c < 32 || c == 127) return false;
    return true;
}
inline bool finite_point(double x, double y, double z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
           std::abs(x) <= 1e6 && std::abs(y) <= 1e6 && std::abs(z) <= 1e6;
}
inline bool valid_bookmark(const Bookmark& b) {
    return clean_text(b.name) && clean_text(b.level) && finite_point(b.x, b.y, b.z);
}
inline std::string encode_bookmarks(const std::vector<Bookmark>& bookmarks) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "INFERNIS_BOOKMARKS 1\n" << std::setprecision(9);
    for (const auto& b : bookmarks)
        out << std::quoted(b.name) << ' ' << std::quoted(b.level) << ' '
            << b.x << ' ' << b.y << ' ' << b.z << '\n';
    return out.str();
}
inline bool decode_bookmarks(const std::string& bytes, std::vector<Bookmark>& output) {
    if (bytes.size() > 256 * 1024) return false;
    std::istringstream input(bytes);
    input.imbue(std::locale::classic());
    std::string header;
    if (!std::getline(input, header) || header != "INFERNIS_BOOKMARKS 1") return false;
    std::vector<Bookmark> parsed;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        Bookmark b;
        std::istringstream row(line);
        row.imbue(std::locale::classic());
        if (!(row >> std::quoted(b.name) >> std::quoted(b.level) >> b.x >> b.y >> b.z) ||
            !valid_bookmark(b)) return false;
        row >> std::ws;
        if (!row.eof() || parsed.size() >= 256) return false;
        parsed.push_back(b);
    }
    output.swap(parsed);
    return true;
}
inline bool waypoint_identifier(const std::string& text) {
    if (text.empty() || text.size() > 128) return false;
    for (unsigned char c : text)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}
} // namespace infernis_spawner::tools
