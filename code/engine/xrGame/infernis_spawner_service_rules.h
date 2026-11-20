#pragma once

#include <cmath>
#include <string_view>
#include <string>
#include <cctype>
#include <cstdlib>
#include <cerrno>

namespace infernis_spawner {
constexpr unsigned max_spawn_objects = 250;
constexpr double max_spawn_distance = 1000.0;

struct BatchPlan {
    unsigned objects = 0;
    unsigned quantity = 0;
    unsigned box_size = 0;
    const char* error = "";
};

inline BatchPlan plan_batch(double quantity, bool ammo_units, unsigned box_size = 0) {
    if (!std::isfinite(quantity) || quantity < 1 || std::floor(quantity) != quantity)
        return {0, 0, 0, "Quantity must be a positive integer"};
    if (!ammo_units) {
        if (quantity > max_spawn_objects)
            return {0, 0, 0, "Maximum 250 objects per request"};
        return {static_cast<unsigned>(quantity), static_cast<unsigned>(quantity), 0, ""};
    }
    if (!box_size || box_size > 65535)
        return {0, 0, 0, "Invalid ammo box_size (expected 1..65535)"};
    if (quantity > static_cast<double>(box_size) * max_spawn_objects)
        return {0, 0, 0, "Requested ammo would require more than 250 boxes"};
    const unsigned rounds = static_cast<unsigned>(quantity);
    return {(rounds + box_size - 1) / box_size, rounds, box_size, ""};
}

inline unsigned rounds_in_box(const BatchPlan& plan, unsigned index) {
    if (!plan.box_size || index >= plan.objects)
        return 0;
    const unsigned remaining = plan.quantity - index * plan.box_size;
    return remaining < plan.box_size ? remaining : plan.box_size;
}

inline bool ammo_class(std::string_view id) {
    return id == "AMMO" || id == "A_VOG25" || id == "A_OG7B" || id == "A_M209" ||
           id == "AMMO_S" || id == "S_VOG25" || id == "S_OG7B" || id == "S_M209";
}

inline bool valid_distance(double distance) {
    return std::isfinite(distance) && distance >= 0 && distance <= max_spawn_distance;
}

struct ConsoleRequest {
    std::string section;
    double quantity = 1;
    const char* error = "";
};

inline ConsoleRequest parse_console_request(const char* args) {
    ConsoleRequest result;
    if (!args) { result.error = "Expected a section name"; return result; }
    while (*args && std::isspace(static_cast<unsigned char>(*args))) ++args;
    const char* start = args;
    while (*args && !std::isspace(static_cast<unsigned char>(*args))) ++args;
    result.section.assign(start, args);
    if (result.section.empty() || result.section.size() > 63) {
        result.error = "Expected a section name of 1..63 bytes";
        return result;
    }
    while (*args && std::isspace(static_cast<unsigned char>(*args))) ++args;
    if (!*args) return result;
    char* end = nullptr;
    errno = 0;
    result.quantity = std::strtod(args, &end);
    if (errno || end == args) { result.error = "Invalid object count"; return result; }
    while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (*end) result.error = "Unexpected text after the object count";
    return result;
}
} // namespace infernis_spawner
