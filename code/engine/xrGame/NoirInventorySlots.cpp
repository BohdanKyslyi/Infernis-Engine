#include "pch_script.h"
#include "NoirInventorySlots.h"

namespace {

struct SNoirInventorySlotSettings {
    bool enabled = false;
    bool knife = false;
    bool binocular = false;
    bool torch = false;
    bool extra_pistol = false;
    bool backpack = false;
};

SNoirInventorySlotSettings LoadNoirInventorySlotSettings() {
    SNoirInventorySlotSettings settings;

    string_path path;
    if (!FS.exist(path, "$game_config$", "noirEngineExtention.ltx"))
        return settings;

    CInifile extensions(path);
    if (!extensions.section_exist("inventory") ||
        !extensions.line_exist("inventory", "enable_modular_slots"))
        return settings;

    settings.enabled = extensions.r_bool("inventory", "enable_modular_slots");
    if (!settings.enabled)
        return settings;

    if (!FS.exist(path, "$game_config$", "noirInventorySlots.ltx")) {
        Msg("! [NoirInventorySlots] noirInventorySlots.ltx is missing; optional slots are disabled");
        return settings;
    }

    CInifile slots(path);
    if (!slots.section_exist("slots")) {
        Msg("! [NoirInventorySlots] section [slots] is missing; optional slots are disabled");
        return settings;
    }

    settings.knife =
        slots.line_exist("slots", "knife_slot") && slots.r_bool("slots", "knife_slot");
    settings.binocular = slots.line_exist("slots", "binocular_slot") &&
        slots.r_bool("slots", "binocular_slot");
    settings.torch =
        slots.line_exist("slots", "torch_slot") && slots.r_bool("slots", "torch_slot");
    settings.extra_pistol = slots.line_exist("slots", "pistol_slot") &&
        slots.r_bool("slots", "pistol_slot");
    settings.backpack = slots.line_exist("slots", "backpack_slot") &&
        slots.r_bool("slots", "backpack_slot");

    Msg("* [NoirInventorySlots] knife=%s, binocular=%s, torch=%s, pistol=%s, backpack=%s",
        settings.knife ? "on" : "off", settings.binocular ? "on" : "off",
        settings.torch ? "on" : "off", settings.extra_pistol ? "on" : "off", settings.backpack ? "on" : "off");

    return settings;
}

const SNoirInventorySlotSettings& Settings() {
    static const SNoirInventorySlotSettings settings = LoadNoirInventorySlotSettings();
    return settings;
}

} // namespace

namespace NoirInventorySlots {

bool Enabled() { return Settings().enabled; }

bool KnifeEnabled() { return Enabled() && Settings().knife; }

bool BinocularEnabled() { return Enabled() && Settings().binocular; }

bool TorchEnabled() { return Enabled() && Settings().torch; }

bool ExtraPistolEnabled() { return Enabled() && Settings().extra_pistol; }

bool BackpackEnabled() { return Enabled() && Settings().backpack; }

bool IsSlotEnabled(u16 slot_id) {
    switch (slot_id) {
    case KNIFE_SLOT:
        return KnifeEnabled();
    case BINOCULAR_SLOT:
        return BinocularEnabled();
    case TORCH_SLOT:
        return TorchEnabled();
    case EXTRA_PISTOL_SLOT:
        return ExtraPistolEnabled();
    case BACKPACK_SLOT:
        return BackpackEnabled();
    default:
        return false;
    }
}

} // namespace NoirInventorySlots
