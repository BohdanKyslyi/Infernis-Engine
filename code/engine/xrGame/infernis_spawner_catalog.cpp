#include "pch_script.h"
#include "infernis_spawner_runtime.h"
#include "infernis_spawner_catalog.h"
#include "infernis_spawner_catalog_rules.h"
#include "infernis_spawner_service.h"
#include "infernis_spawner_world.h"
#include "infernis_spawner_tools.h"
#include "object_factory.h"
#include "string_table.h"

namespace infernis_spawner {
namespace {
struct Entry {
    xr_string section;
    xr_string inv_name;
    xr_string class_id;
    Category category;
};

xr_vector<Entry> s_entries;
const CInifile* s_settings = nullptr;
const CObjectFactory* s_factory = nullptr;
u32 s_section_count = 0;
bool s_ready = false;

LPCSTR value(LPCSTR section, LPCSTR key) {
    if (!pSettings->line_exist(section, key))
        return "";
    LPCSTR result = pSettings->r_string(section, key);
    return result ? result : "";
}

xr_string unquoted_value(LPCSTR section, LPCSTR key) {
    xr_string result = value(section, key);
    const auto first = result.find_first_not_of(" \t\r\n\"");
    if (first == xr_string::npos)
        return "";
    return result.substr(first, result.find_last_not_of(" \t\r\n\"") - first + 1);
}

bool bool_value(LPCSTR section, LPCSTR key) {
    const xr_string result = unquoted_value(section, key);
    return !_stricmp(result.c_str(), "true") || !_stricmp(result.c_str(), "on") ||
           !_stricmp(result.c_str(), "yes") || result == "1";
}

void refresh() {
    s_entries.clear();
    s_settings = pSettings;
    s_factory = nullptr;
    s_section_count = pSettings ? pSettings->section_count() : 0;
    s_ready = false;
    if (!pSettings)
        return;

    const CObjectFactory& factory = object_factory();
    s_factory = &factory;
    for (const CInifile::Sect* section : pSettings->sections()) {
        const LPCSTR name = section->Name.c_str();
        const xr_string class_id = unquoted_value(name, "class");
        // Unknown/temporary/non-inventory types never enter an inventory spawn list.
        if (class_id.empty() || class_id.size() > 8 ||
            class_category(class_id.c_str()) == Category::none)
            continue;

        const xr_string inv_name = unquoted_value(name, "inv_name");
        const xr_string visual = unquoted_value(name, "visual");
        const xr_string category_override = unquoted_value(name, "spawner_category");
        Candidate candidate;
        candidate.class_id = class_id.c_str();
        candidate.inv_name = inv_name.c_str();
        candidate.visual = visual.c_str();
        candidate.registered = factory.has_class(TEXT2CLSID(class_id.c_str()));
        candidate.has_weight = pSettings->line_exist(name, "inv_weight") && value(name, "inv_weight")[0];
        candidate.grid_width = atoi(value(name, "inv_grid_width"));
        candidate.grid_height = atoi(value(name, "inv_grid_height"));
        candidate.hidden = bool_value(name, "spawner_hidden");
        candidate.quest_item = bool_value(name, "quest_item");
        candidate.category_override = parse_category(category_override.c_str());
        if (!category_override.empty() && candidate.category_override == Category::none)
            Msg("! [infernis_spawner] [%s]: unknown spawner_category '%s', using automatic category",
                name, category_override.c_str());

        const Category category = classify(candidate);
        if (category != Category::none)
            s_entries.push_back({name, inv_name, class_id, category});
    }
    s_ready = true;
    Msg("* [infernis_spawner] catalog: %u inventory sections", static_cast<u32>(s_entries.size()));
}

void ensure_catalog() {
    if (!s_ready || s_settings != pSettings || s_factory != g_object_factory ||
        (pSettings && s_section_count != pSettings->section_count()))
        refresh();
}

void string_field(lua_State* L, LPCSTR key, LPCSTR value) {
    lua_pushstring(L, value);
    lua_setfield(L, -2, key);
}

int get_catalog(lua_State* L) {
    const LPCSTR requested = luaL_optstring(L, 1, "all");
    const bool all = !xr_strcmp(requested, "all");
    const Category category = parse_category(requested);
    if (!all && category == Category::none)
        return luaL_error(L, "infernis_spawner.get_catalog: unknown category '%s'", requested);
    ensure_catalog();
    if (s_entries.empty()) {
        lua_newtable(L);
        return 1;
    }

    struct NamedEntry { const Entry* entry; xr_string name; };
    xr_vector<NamedEntry> entries;
    CStringTable strings;
    for (const Entry& entry : s_entries) {
        if (!all && entry.category != category)
            continue;
        const shared_str translated = strings.translate(entry.inv_name.c_str());
        const LPCSTR name = translated.c_str();
        entries.push_back({&entry, name && name[0] && xr_strcmp(name, entry.inv_name.c_str())
                                      ? name : entry.section.c_str()});
    }
    std::sort(entries.begin(), entries.end(), [](const NamedEntry& left, const NamedEntry& right) {
        const int result = _stricmp(left.name.c_str(), right.name.c_str());
        return result ? result < 0 : left.entry->section < right.entry->section;
    });

    lua_createtable(L, static_cast<int>(entries.size()), 0);
    int index = 1;
    for (const NamedEntry& named : entries) {
        const Entry& entry = *named.entry;
        lua_createtable(L, 0, 5);
        string_field(L, "section", entry.section.c_str());
        string_field(L, "name", named.name.c_str());
        string_field(L, "inv_name", entry.inv_name.c_str());
        string_field(L, "class_id", entry.class_id.c_str());
        string_field(L, "category", category_name(entry.category).data());
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

int refresh_catalog(lua_State* L) {
    refresh();
    lua_pushinteger(L, static_cast<lua_Integer>(s_entries.size()));
    return 1;
}
} // namespace
} // namespace infernis_spawner

void CInfernisSpawnerCatalog::script_register(lua_State* L) {
    // Reset copied metadata on script-VM recreation; never store Lua objects in the cache.
    infernis_spawner::s_ready = false;
    static const luaL_Reg functions[] = {
        {"get_catalog", infernis_spawner::get_catalog},
        {"refresh", infernis_spawner::refresh_catalog},
        {nullptr, nullptr}
    };
    luaL_register(L, "infernis_spawner", functions);
    lua_pop(L, 1);
    infernis_spawner::register_spawn_api(L);
    infernis_spawner::register_world_api(L);
    infernis_spawner::tools::register_api(L);
    infernis_spawner::runtime::register_api(L);
}
