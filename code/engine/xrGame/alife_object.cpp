////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_object.cpp
//	Created 	: 27.10.2005
//  Modified 	: 27.10.2005
//	Author		: Dmitriy Iassenev
//	Description : ALife object class
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "xrServer_Objects_ALife.h"
#include "alife_simulator.h"
#include "xrServer_Objects_ALife_Items.h"

namespace {
LPCSTR find_loadout_parameter(LPCSTR value, LPCSTR name) {
    if (!value || !name || !xr_strlen(name))
        return NULL;

    const u32 name_length = xr_strlen(name);
    LPCSTR parameter = strstr(value, name);

    while (parameter) {
        if (parameter == value || parameter[-1] == ',' || parameter[-1] == ' ' ||
            parameter[-1] == '\t') {
            return parameter + name_length;
        }

        parameter = strstr(parameter + name_length, name);
    }

    return NULL;
}

bool has_loadout_flag(LPCSTR value, LPCSTR flag) {
    if (!value || !flag || !xr_strlen(flag))
        return false;

    const u32 flag_length = xr_strlen(flag);
    LPCSTR position = strstr(value, flag);

    while (position) {
        const bool valid_start =
            position == value || position[-1] == ',' || position[-1] == ' ' ||
            position[-1] == '\t';
        const char next = position[flag_length];
        const bool valid_end =
            !next || next == ',' || next == ' ' || next == '\t';

        if (valid_start && valid_end)
            return true;

        position = strstr(position + flag_length, flag);
    }

    return false;
}

struct SSpawnLoadoutEntry {
    xr_string section;
    float weight;
    float condition;
    u32 ammo_count;
    s32 ammo_type;
    u32 drop_ammo_min;
    u32 drop_ammo_max;
    bool custom_drop_ammo;
    bool keep_ammo;
    bool scope;
    bool silencer;
    bool launcher;

    SSpawnLoadoutEntry(LPCSTR item_section, LPCSTR value)
        : section(item_section ? item_section : ""), weight(1.f), condition(1.f), ammo_count(1),
          ammo_type(0), drop_ammo_min(0), drop_ammo_max(0), custom_drop_ammo(false),
          keep_ammo(false), scope(false), silencer(false), launcher(false) {
        if (!value || !xr_strlen(value))
            return;

        LPCSTR parameter = find_loadout_parameter(value, "weight=");
        if (parameter)
            weight = (float)atof(parameter);

        parameter = find_loadout_parameter(value, "cond=");
        if (parameter)
            condition = (float)atof(parameter);

        parameter = find_loadout_parameter(value, "ammo=");
        if (parameter) {
            const s32 parsed_ammo_count = atoi(parameter);
            ammo_count = parsed_ammo_count > 0 ? (u32)parsed_ammo_count : 0;
        }

        parameter = find_loadout_parameter(value, "ammo_type=");
        if (parameter)
            ammo_type = atoi(parameter);

        parameter = find_loadout_parameter(value, "drop_ammo=");
        if (parameter) {
            const s32 parsed_min = atoi(parameter);
            s32 parsed_max = parsed_min;

            LPCSTR separator = strchr(parameter, '-');
            LPCSTR parameter_end = strchr(parameter, ',');

            if (separator && (!parameter_end || separator < parameter_end))
                parsed_max = atoi(separator + 1);

            drop_ammo_min = parsed_min > 0 ? (u32)parsed_min : 0;
            drop_ammo_max = parsed_max > 0 ? (u32)parsed_max : 0;

            if (drop_ammo_max < drop_ammo_min)
                std::swap(drop_ammo_min, drop_ammo_max);

            custom_drop_ammo = true;
        }

        keep_ammo = has_loadout_flag(value, "keep_ammo");

        if (condition < 0.f)
            condition = 0.f;
        else if (condition > 1.f)
            condition = 1.f;

        scope = has_loadout_flag(value, "scope");
        silencer = has_loadout_flag(value, "silencer");
        launcher = has_loadout_flag(value, "launcher");
    }

    bool is_empty() const { return 0 == xr_strcmp(section.c_str(), "none"); }
};

void apply_loadout_properties(CSE_Abstract* entity, const SSpawnLoadoutEntry& entry) {
    if (!entity)
        return;

    CSE_ALifeItemWeapon* weapon = smart_cast<CSE_ALifeItemWeapon*>(entity);
    if (weapon) {
        if (weapon->m_scope_status == ALife::eAddonAttachable)
            weapon->m_addon_flags.set(CSE_ALifeItemWeapon::eWeaponAddonScope, entry.scope);

        if (weapon->m_silencer_status == ALife::eAddonAttachable)
            weapon->m_addon_flags.set(CSE_ALifeItemWeapon::eWeaponAddonSilencer,
                                      entry.silencer);

        if (weapon->m_grenade_launcher_status == ALife::eAddonAttachable)
            weapon->m_addon_flags.set(CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher,
                                      entry.launcher);
    }

    CSE_ALifeInventoryItem* inventory_item = smart_cast<CSE_ALifeInventoryItem*>(entity);
    if (inventory_item)
        inventory_item->m_fCondition = entry.condition;
}
} // namespace

void CSE_ALifeObject::spawn_supplies() { spawn_supplies(*m_ini_string); }

void CSE_ALifeObject::spawn_supplies(LPCSTR ini_string) {
    if (!ini_string)
        return;

    if (!xr_strlen(ini_string))
        return;

#pragma warning(push)
#pragma warning(disable : 4238)
    CInifile ini(&IReader((void*)(ini_string), xr_strlen(ini_string)),
                 FS.get_path("$game_config$")->m_Path);
#pragma warning(pop)

    xr_string death_ammo_metadata;

    for (u32 loadout_index = 1;; ++loadout_index) {
        string32 loadout_section;

        if (loadout_index == 1)
            xr_strcpy(loadout_section, "spawn_loadout");
        else
            xr_sprintf(loadout_section, "spawn_loadout%u", loadout_index);

        if (!ini.section_exist(loadout_section))
            break;

        xr_vector<SSpawnLoadoutEntry> entries;
        float total_weight = 0.f;
        LPCSTR item_section;
        LPCSTR value;

        for (u32 line = 0; ini.r_line(loadout_section, line, &item_section, &value); ++line) {
            if (!item_section || !xr_strlen(item_section)) {
                Msg("! [%s] ignored an empty item section", loadout_section);
                continue;
            }

            SSpawnLoadoutEntry entry(item_section, value);

            if (entry.weight <= 0.f) {
                Msg("! [%s] ignored [%s]: weight must be greater than zero", loadout_section,
                    item_section);
                continue;
            }

            if (!entry.is_empty() && (!pSettings || !pSettings->section_exist(item_section))) {
                Msg("! [%s] ignored unknown item section [%s]", loadout_section, item_section);
                continue;
            }

            total_weight += entry.weight;
            entries.push_back(entry);
        }

        if (entries.empty() || total_weight <= 0.f) {
            Msg("! [%s] contains no valid loadout entries", loadout_section);
            continue;
        }

        const SSpawnLoadoutEntry* selected = &entries.back();

        if (entries.size() > 1) {
            float roll = Random.randF(total_weight);

            for (u32 entry_index = 0; entry_index < entries.size(); ++entry_index) {
                if (roll < entries[entry_index].weight) {
                    selected = &entries[entry_index];
                    break;
                }

                roll -= entries[entry_index].weight;
            }
        }

        if (selected->is_empty())
            continue;

        CSE_Abstract* entity = alife().spawn_item(selected->section.c_str(), o_Position,
                                                  m_tNodeID, m_tGraphID, ID);

        if (!entity) {
            Msg("! [%s] failed to spawn item [%s]", loadout_section,
                selected->section.c_str());
            continue;
        }

        apply_loadout_properties(entity, *selected);

        CSE_ALifeItemWeapon* weapon = smart_cast<CSE_ALifeItemWeapon*>(entity);

        if (!weapon)
            continue;

        if (!selected->ammo_count && !selected->custom_drop_ammo)
            continue;

        LPCSTR ammo_classes = weapon->m_caAmmoSections;

        if (!ammo_classes || !xr_strlen(ammo_classes))
            continue;

        const s32 ammo_type_count = _GetItemCount(ammo_classes);

        if (ammo_type_count <= 0)
            continue;

        s32 ammo_type = selected->ammo_type;

        if (ammo_type < 0 || ammo_type >= ammo_type_count) {
            Msg("! [%s] item [%s] has invalid ammo_type=%d; using ammo_type=0",
                loadout_section, selected->section.c_str(), ammo_type);
            ammo_type = 0;
        }

        string128 ammo_section_buffer;
        LPCSTR ammo_section = _GetItem(ammo_classes, ammo_type, ammo_section_buffer);

        if (!ammo_section || !xr_strlen(ammo_section) ||
            !pSettings->section_exist(ammo_section)) {
            Msg("! [%s] item [%s] references unknown ammo section [%s]",
                loadout_section, selected->section.c_str(),
                ammo_section && xr_strlen(ammo_section) ? ammo_section : "<empty>");
            continue;
        }

        weapon->ammo_type = (u8)ammo_type;

        if (death_ammo_metadata.empty())
            death_ammo_metadata = "[spawn_loadout_death_ammo]\n";

        string256 metadata_line;

        if (selected->keep_ammo) {
            xr_sprintf(metadata_line, "%s = %s, keep\n", loadout_section, ammo_section);
        } else if (selected->custom_drop_ammo) {
            xr_sprintf(metadata_line, "%s = %s, custom, %u, %u\n", loadout_section,
                       ammo_section, selected->drop_ammo_min, selected->drop_ammo_max);
        } else {
            xr_sprintf(metadata_line, "%s = %s, default\n", loadout_section, ammo_section);
        }

        death_ammo_metadata += metadata_line;

        for (u32 ammo_index = 0; ammo_index < selected->ammo_count; ++ammo_index) {
            CSE_Abstract* ammo = alife().spawn_item(ammo_section, o_Position, m_tNodeID,
                                                    m_tGraphID, ID);

            if (!ammo) {
                Msg("! [%s] failed to spawn ammo [%s] for item [%s]", loadout_section,
                    ammo_section, selected->section.c_str());
                break;
            }
        }
    }

    if (ini.section_exist("spawn")) {
        LPCSTR N, V;
        float p;
        for (u32 k = 0, j; ini.r_line("spawn", k, &N, &V); k++) {
            VERIFY(xr_strlen(N));

            float f_cond = 1.0f;
            bool bScope = false;
            bool bSilencer = false;
            bool bLauncher = false;

            j = 1;
            p = 1.f;

            if (V && xr_strlen(V)) {
                string64 buf;
                j = atoi(_GetItem(V, 0, buf));
                if (!j)
                    j = 1;

                bScope = (NULL != strstr(V, "scope"));
                bSilencer = (NULL != strstr(V, "silencer"));
                bLauncher = (NULL != strstr(V, "launcher"));
                // probability
                if (NULL != strstr(V, "prob="))
                    p = (float)atof(strstr(V, "prob=") + 5);
                if (fis_zero(p))
                    p = 1.0f;
                if (NULL != strstr(V, "cond="))
                    f_cond = (float)atof(strstr(V, "cond=") + 5);
            }
            for (u32 i = 0; i < j; ++i) {
                if (randF(1.f) < p) {
                    CSE_Abstract* E = alife().spawn_item(N, o_Position, m_tNodeID, m_tGraphID, ID);
                    //подсоединить аддоны к оружию, если включены соответствующие флажки
                    CSE_ALifeItemWeapon* W = smart_cast<CSE_ALifeItemWeapon*>(E);
                    if (W) {
                        if (W->m_scope_status == ALife::eAddonAttachable)
                            W->m_addon_flags.set(CSE_ALifeItemWeapon::eWeaponAddonScope, bScope);
                        if (W->m_silencer_status == ALife::eAddonAttachable)
                            W->m_addon_flags.set(CSE_ALifeItemWeapon::eWeaponAddonSilencer,
                                                 bSilencer);
                        if (W->m_grenade_launcher_status == ALife::eAddonAttachable)
                            W->m_addon_flags.set(CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher,
                                                 bLauncher);
                    }
                    CSE_ALifeInventoryItem* IItem = smart_cast<CSE_ALifeInventoryItem*>(E);
                    if (IItem)
                        IItem->m_fCondition = f_cond;
                }
            }
        }
    }

    if (!death_ammo_metadata.empty()) {
        LPCSTR current_ini = *m_ini_string;

        if (!current_ini || !strstr(current_ini, "[spawn_loadout_death_ammo]")) {
            xr_string updated_ini = current_ini ? current_ini : "";

            if (!updated_ini.empty() && updated_ini[updated_ini.size() - 1] != '\n')
                updated_ini += "\n";

            updated_ini += death_ammo_metadata;

            xr_delete(m_ini_file);
            m_ini_string = updated_ini.c_str();
        } else {
            Msg("! [spawn_loadout_death_ammo] already exists for object [%s]; metadata was not "
                "replaced",
                name_replace());
        }
    }
}

bool CSE_ALifeObject::keep_saved_data_anyway() const { return (false); }
