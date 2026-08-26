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
struct SSpawnLoadoutEntry {
    xr_string section;
    float weight;
    float condition;
    bool scope;
    bool silencer;
    bool launcher;

    SSpawnLoadoutEntry(LPCSTR item_section, LPCSTR value)
        : section(item_section ? item_section : ""), weight(1.f), condition(1.f), scope(false),
          silencer(false), launcher(false) {
        if (!value || !xr_strlen(value))
            return;

        LPCSTR parameter = strstr(value, "weight=");
        if (parameter)
            weight = (float)atof(parameter + 7);

        parameter = strstr(value, "cond=");
        if (parameter)
            condition = (float)atof(parameter + 5);

        if (condition < 0.f)
            condition = 0.f;
        else if (condition > 1.f)
            condition = 1.f;

        scope = (NULL != strstr(value, "scope"));
        silencer = (NULL != strstr(value, "silencer"));
        launcher = (NULL != strstr(value, "launcher"));
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
}

bool CSE_ALifeObject::keep_saved_data_anyway() const { return (false); }
