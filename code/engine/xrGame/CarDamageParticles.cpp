#include "stdafx.h"
#include "cardamageparticles.h"
#ifdef DEBUG

#include "PHDebug.h"
#endif
#include "alife_space.h"
#include "hit.h"
#include "PHDestroyable.h"
#include "Car.h"
#include "xrRender/Kinematics.h"
#include "../xrphysics/IPHWorld.h"
//#include "PHWorld.h"
// extern CPHWorld*	ph_world;
void read_bones(IKinematics* K, LPCSTR S, xr_vector<u16>& bones) {
    string64 S1;
    int count = _GetItemCount(S);
    for (int i = 0; i < count; ++i) {
        _GetItem(S, i, S1);

        u16 bone_id = K->LL_BoneID(S1);
        R_ASSERT3(bone_id != BI_NONE, "wrong bone", S1);
        xr_vector<u16>::iterator iter = std::find(bones.begin(), bones.end(), bone_id);
        R_ASSERT3(iter == bones.end(), "double bone", S1);
        bones.push_back(bone_id);
    }
}
void CCarDamageParticles::Init(CCar* car) {
    IKinematics* K = smart_cast<IKinematics*>(car->Visual());
    CInifile* ini = K->LL_UserData();
    if (ini->section_exist("damage_particles")) {
        m_car_damage_particles1 = ini->r_string("damage_particles", "car_damage_particles1");
        m_car_damage_particles2 = ini->r_string("damage_particles", "car_damage_particles2");
        m_wheels_damage_particles1 = ini->r_string("damage_particles", "wheels_damage_particles1");
        m_wheels_damage_particles2 = ini->r_string("damage_particles", "wheels_damage_particles2");

        read_bones(K, ini->r_string("damage_particles", "particle_bones1"), bones1);
        read_bones(K, ini->r_string("damage_particles", "particle_bones2"), bones2);
    }
}

void CCarDamageParticles::Play1(CCar* car) {
    if (*m_car_damage_particles1) {
        auto i = bones1.begin(), e = bones1.end();
        for (; e != i; ++i)
            car->StartParticles(m_car_damage_particles1, *i, Fvector().set(0, 1, 0), car->ID());
    }
}

void CCarDamageParticles::Play2(CCar* car) {
    VERIFY(!physics_world()->Processing());
    if (*m_car_damage_particles2) {
        auto i = bones2.begin(), e = bones2.end();
        for (; e != i; ++i)
            car->StartParticles(m_car_damage_particles2, *i, Fvector().set(0, 1, 0), car->ID());
    }
}

// Ported by Ivan Danko, 2026-11-17, from Monolith (author Ray Twitty (aka Shadows)): FPS plan step 2 - Monolith's Stop1 / Stop2 (added by Ray Twitty (aka
// Shadows)): they stop the damage smoke on the bones of Play1 / Play2. Adapted: BIDS_I -> auto (our
// CarDamageParticles.h has no BIDS_I).
// ѕеренесенн€ з Monolith (автор Ray Twitty (aka Shadows)) - Ivan Danko, 2026-11-17: крок 2 плану FPS - Stop1 / Stop2 з Monolith (додав Ray Twitty
// (aka Shadows)): зупин€ють дим пошкоджень на к≥стках Play1 / Play2. јдаптовано: BIDS_I -> auto (у
// нашому CarDamageParticles.h немаЇ BIDS_I).
void CCarDamageParticles::Stop1(CCar* car) {
    if (*m_car_damage_particles1) {
        auto i = bones1.begin(), e = bones1.end();
        for (; e != i; ++i)
            car->StopParticles(car->ID(), *i, false);
    }
}

void CCarDamageParticles::Stop2(CCar* car) {
    VERIFY(!physics_world()->Processing());
    if (*m_car_damage_particles2) {
        auto i = bones2.begin(), e = bones2.end();
        for (; e != i; ++i)
            car->StopParticles(car->ID(), *i, false);
    }
}

void CCarDamageParticles::PlayWheel1(CCar* car, u16 bone_id) const {
    VERIFY(!physics_world()->Processing());
    if (*m_wheels_damage_particles1)
        car->StartParticles(m_wheels_damage_particles1, bone_id, Fvector().set(0, 1, 0), car->ID());
}

void CCarDamageParticles::PlayWheel2(CCar* car, u16 bone_id) const {
    VERIFY(!physics_world()->Processing());
    if (*m_wheels_damage_particles2)
        car->StartParticles(m_wheels_damage_particles2, bone_id, Fvector().set(0, 1, 0), car->ID());
}

void CCarDamageParticles::Clear() {
    bones1.clear();
    bones2.clear();
}