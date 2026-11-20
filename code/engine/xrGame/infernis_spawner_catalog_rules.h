#pragma once

#include <string_view>
#include <initializer_list>

// Inventory categories only. These are class rules, never lists of item sections.
namespace infernis_spawner {
enum class Category { none, weapons, ammo, addons, outfits, items, artefacts, quest };

inline std::string_view category_name(Category category) {
    switch (category) {
    case Category::weapons: return "weapons";
    case Category::ammo: return "ammo";
    case Category::addons: return "addons";
    case Category::outfits: return "outfits";
    case Category::items: return "items";
    case Category::artefacts: return "artefacts";
    case Category::quest: return "quest";
    default: return "";
    }
}

inline Category parse_category(std::string_view name) {
    for (Category category : {Category::weapons, Category::ammo, Category::addons,
                              Category::outfits, Category::items, Category::artefacts,
                              Category::quest}) {
        if (category_name(category) == name)
            return category;
    }
    return Category::none;
}

inline Category class_category(std::string_view class_id) {
    struct Rule { std::string_view id; Category category; };
    // Both native CLSIDs and the IDs registered by class_registrator.script.
    static constexpr Rule rules[] = {
        {"W_M134", Category::weapons}, {"W_FN2000", Category::weapons},
        {"W_AK74", Category::weapons}, {"W_LR300", Category::weapons},
        {"W_HPSA", Category::weapons}, {"W_PM", Category::weapons},
        {"W_FORT", Category::weapons}, {"W_BINOC", Category::weapons},
        {"W_SHOTGN", Category::weapons}, {"W_ASHTGN", Category::weapons},
        {"W_WMAGAZ", Category::weapons}, {"W_WMAGGL", Category::weapons},
        {"W_SVD", Category::weapons}, {"W_SVU", Category::weapons},
        {"W_RPG7", Category::weapons}, {"W_VAL", Category::weapons},
        {"W_VINT", Category::weapons}, {"W_WALTHR", Category::weapons},
        {"W_USP45", Category::weapons}, {"W_GROZA", Category::weapons},
        {"W_KNIFE", Category::weapons}, {"W_BM16", Category::weapons},
        {"W_RG6", Category::weapons},
        {"WP_BINOC", Category::weapons}, {"WP_KNIFE", Category::weapons},
        {"WP_BM16", Category::weapons}, {"WP_GROZA", Category::weapons},
        {"WP_SVD", Category::weapons}, {"WP_AK74", Category::weapons},
        {"WP_LR300", Category::weapons}, {"WP_HPSA", Category::weapons},
        {"WP_PM", Category::weapons}, {"WP_RG6", Category::weapons},
        {"WP_RPG7", Category::weapons}, {"WP_SHOTG", Category::weapons},
        {"WP_ASHTG", Category::weapons}, {"WP_MAGAZ", Category::weapons},
        {"WP_SVU", Category::weapons}, {"WP_USP45", Category::weapons},
        {"WP_VAL", Category::weapons}, {"WP_VINT", Category::weapons},
        {"WP_WALTH", Category::weapons},
        {"AMMO", Category::ammo}, {"A_VOG25", Category::ammo},
        {"A_OG7B", Category::ammo}, {"A_M209", Category::ammo},
        {"AMMO_S", Category::ammo}, {"S_VOG25", Category::ammo},
        {"S_OG7B", Category::ammo}, {"S_M209", Category::ammo},
        {"G_F1", Category::ammo}, {"G_RGD5", Category::ammo},
        {"G_F1_S", Category::ammo}, {"G_RGD5_S", Category::ammo},
        {"W_SCOPE", Category::addons}, {"W_SILENC", Category::addons},
        {"W_GLAUNC", Category::addons}, {"WP_SCOPE", Category::addons},
        {"WP_SILEN", Category::addons}, {"WP_GLAUN", Category::addons},
        {"EQU_SMPL", Category::outfits}, {"EQU_SCIE", Category::outfits},
        {"EQU_STLK", Category::outfits}, {"EQU_MLTR", Category::outfits},
        {"EQU_EXO", Category::outfits}, {"EQ_HLMET", Category::outfits},
        {"BACKPACK", Category::outfits}, {"E_STLK", Category::outfits},
        {"E_HLMET", Category::outfits}, {"E_BCKP", Category::outfits},
        {"ARTEFACT", Category::artefacts}, {"SCRPTART", Category::artefacts},
        {"AF_MBALL", Category::artefacts}, {"AF_GRAVI", Category::artefacts},
        {"AF_BDROP", Category::artefacts}, {"AF_NEEDL", Category::artefacts},
        {"AF_BAST", Category::artefacts}, {"AF_BGRAV", Category::artefacts},
        {"AF_DUMMY", Category::artefacts}, {"AF_ZUDA", Category::artefacts},
        {"AF_THORN", Category::artefacts}, {"AF_FBALL", Category::artefacts},
        {"AF_EBALL", Category::artefacts}, {"AF_RHAIR", Category::artefacts},
        {"AF_GALAN", Category::artefacts},
        {"II_DOC", Category::quest},
        {"II_BOLT", Category::items}, {"II_MEDKI", Category::items},
        {"II_BANDG", Category::items}, {"II_FOOD", Category::items},
        {"II_BOTTL", Category::items}, {"II_ANTIR", Category::items},
        {"II_EXPLO", Category::items}, {"II_ATTCH", Category::items},
        {"D_REPKIT", Category::items}, {"S_MEDKI", Category::items},
        {"S_BANDG", Category::items}, {"S_ANTIR", Category::items},
        {"S_FOOD", Category::items}, {"S_BOTTL", Category::items},
        {"S_EXPLO", Category::items}, {"D_PDA", Category::items},
        {"S_PDA", Category::items}, {"D_TORCH", Category::items},
        {"TORCH_S", Category::items}, {"D_SIMDET", Category::items},
        {"D_ADVANC", Category::items}, {"D_ELITE", Category::items},
        {"D_SCIENT", Category::items}, {"D_FLALIT", Category::items},
        {"DET_SIMP", Category::items}, {"DET_ADVA", Category::items},
        {"DET_ELIT", Category::items}, {"DET_SCIE", Category::items}
    };
    for (const Rule& rule : rules) {
        if (rule.id == class_id)
            return rule.category;
    }
    return Category::none;
}

struct Candidate {
    std::string_view class_id;
    std::string_view inv_name;
    std::string_view visual;
    bool registered = false;
    bool has_weight = false;
    int grid_width = 0;
    int grid_height = 0;
    bool hidden = false;
    bool quest_item = false;
    Category category_override = Category::none;
};

inline Category classify(const Candidate& candidate) {
    const Category category = class_category(candidate.class_id);
    if (category == Category::none || !candidate.registered || candidate.hidden ||
        candidate.inv_name.empty() || candidate.inv_name == "default" ||
        candidate.visual.empty() || !candidate.has_weight ||
        candidate.grid_width <= 0 || candidate.grid_height <= 0)
        return Category::none;
    if (candidate.category_override != Category::none)
        return candidate.category_override;
    return candidate.quest_item ? Category::quest : category;
}
} // namespace infernis_spawner
