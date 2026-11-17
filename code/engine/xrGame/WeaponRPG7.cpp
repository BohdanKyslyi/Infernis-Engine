#include "stdafx.h"
#include "weaponrpg7.h"
#include "xrserver_objects_alife_items.h"
#include "explosiverocket.h"
#include "entity.h"
#include "level.h"
#include "player_hud.h"
#include "hudmanager.h"
#include "Level_Bullet_Manager.h"
#include "../xrEngine/gamemtllib.h"
#include <cmath>

CWeaponRPG7::CWeaponRPG7() {}

CWeaponRPG7::~CWeaponRPG7() {}

void CWeaponRPG7::Load(LPCSTR section) {
    inherited::Load(section);
    CRocketLauncher::Load(section);

    m_zoom_params.m_fScopeZoomFactor = pSettings->r_float(section, "max_zoom_factor");

    m_sRocketSection = pSettings->r_string(section, "rocket_class");

    m_malfunction_start_condition =
        READ_IF_EXISTS(pSettings, r_float, section, "rocket_misfunc_start_condition", 0.f);
    m_malfunction_end_condition =
        READ_IF_EXISTS(pSettings, r_float, section, "rocket_misfunc_end_condition", 0.f);
    m_malfunction_start_probability =
        READ_IF_EXISTS(pSettings, r_float, section, "rocket_misfunc_start_probability", 0.f);
    m_malfunction_end_probability =
        READ_IF_EXISTS(pSettings, r_float, section, "rocket_misfunc_end_probability", 0.f);

    m_backblast.distance = READ_IF_EXISTS(pSettings, r_float, section, "reactive_hit_dist", 0.f);
    m_backblast.power = READ_IF_EXISTS(pSettings, r_float, section, "reactive_hit_power", 0.f);
    m_backblast.impulse = READ_IF_EXISTS(pSettings, r_float, section, "reactive_hit_impulse", 0.f);
    m_backblast.rays = READ_IF_EXISTS(pSettings, r_s32, section, "reactive_hit_buck", 1);
    m_backblast.dispersion = READ_IF_EXISTS(pSettings, r_float, section, "reactive_hit_buck_disp", 1.f);
    const int hit_type = READ_IF_EXISTS(pSettings, r_s32, section, "reactive_hit_type",
        int(ALife::eHitTypeExplosion));
    m_backblast.hit_type = hit_type >= 0 && hit_type < ALife::eHitTypeMax
        ? static_cast<ALife::EHitType>(hit_type) : ALife::eHitTypeExplosion;
    m_backblast.material =
        READ_IF_EXISTS(pSettings, r_string, section, "reactive_hit_bullet_material", "default");
    m_backblast.reflected_rays =
        READ_IF_EXISTS(pSettings, r_s32, section, "reactive_hit_reverse_buck", 1);
    m_backblast.reflection_dispersion =
        READ_IF_EXISTS(pSettings, r_float, section, "reactive_hit_reverse_disp", 0.1f);
    m_backblast.reflected_dispersion =
        READ_IF_EXISTS(pSettings, r_float, section, "reactive_hit_reverse_disp2", 0.1f);
    m_backblast.reflected_power =
        READ_IF_EXISTS(pSettings, r_float, section, "reactive_hit_reverse_power", m_backblast.power);
    m_backblast.reflected_range_factor =
        READ_IF_EXISTS(pSettings, r_float, section, "reactive_hit_reverse_k", 1.f);
}

bool CWeaponRPG7::ShouldExplodeOnLaunch() const {
    const float condition = GetCondition();
    if (!std::isfinite(m_malfunction_start_condition) || !std::isfinite(m_malfunction_end_condition) ||
        m_malfunction_start_condition <= m_malfunction_end_condition ||
        condition > m_malfunction_start_condition)
        return false;

    const float wear = clampr((m_malfunction_start_condition - condition) /
        (m_malfunction_start_condition - m_malfunction_end_condition), 0.f, 1.f);
    const float probability = m_malfunction_start_probability +
        (m_malfunction_end_probability - m_malfunction_start_probability) * wear;
    return std::isfinite(probability) && probability > 0.f &&
        ::Random.randF(0.f, 1.f) < clampr(probability, 0.f, 1.f);
}

void CWeaponRPG7::ApplyBackblast(const Fvector& position, const Fvector& muzzle_direction) {
    const auto& blast = m_backblast;
    if (!std::isfinite(blast.distance) || !std::isfinite(blast.power) || !std::isfinite(blast.impulse) ||
        !std::isfinite(blast.dispersion) || blast.distance <= 0.f || blast.power <= 0.f ||
        blast.impulse <= 0.f || blast.rays <= 0)
        return;

    Fvector rear;
    rear.invert(muzzle_direction).normalize_safe();
    if (rear.square_magnitude() < EPS_S)
        return;

    CCartridge cartridge;
    cartridge.param_s.kAP = 1.f;
    cartridge.param_s.kAirRes = 1.f;
    cartridge.param_s.fWallmarkSize = 0.05f;
    cartridge.m_LocalAmmoType = 0;
    cartridge.m_flags.zero();
    const auto material = blast.material.size() ? GMLib.GetMaterialIt(blast.material.c_str())
                                               : GMLib.LastMaterial();
    cartridge.bullet_material_idx = material != GMLib.LastMaterial()
        ? u16(material - GMLib.FirstMaterial()) : GMLib.GetMaterialIdx(WEAPON_MATERIAL_NAME);

    CObject* shooter = H_Parent();
    const bool send_hit = SendHitAllowed(shooter);
    auto& bullets = Level().BulletManager();
    const bool can_reflect = blast.reflected_rays > 0 && blast.reflected_power > 0.f &&
        blast.reflected_range_factor > 0.f && std::isfinite(blast.reflected_power) &&
        std::isfinite(blast.reflected_range_factor) && std::isfinite(blast.reflection_dispersion) &&
        std::isfinite(blast.reflected_dispersion);
    constexpr float blast_speed = 330.f;
    for (int i = 0; i < blast.rays; ++i) {
        Fvector ray;
        ray.random_dir(rear, blast.dispersion);
        bullets.AddBullet(position, ray, blast_speed, blast.power, blast.impulse,
            shooter->ID(), ID(), blast.hit_type, blast.distance, cartridge, 1.f, send_hit);

        if (!can_reflect)
            continue;

        ray.random_dir(rear, blast.reflection_dispersion);
        collide::rq_result wall;
        if (!Level().ObjectSpace.RayPick(position, ray, blast.distance, collide::rqtStatic, wall, shooter))
            continue;

        const float remaining = blast.distance - wall.range;
        if (remaining <= 0.f || wall.range <= EPS_S)
            continue;

        Fvector origin, toward_shooter;
        origin.mad(position, ray, wall.range * 0.9f);
        toward_shooter.invert(ray);
        const float power = blast.reflected_power * remaining / blast.distance;
        const float range = blast.reflected_range_factor * remaining;
        for (int j = 0; j < blast.reflected_rays; ++j) {
            Fvector reflected;
            reflected.random_dir(toward_shooter, blast.reflected_dispersion);
            // The reflected blast can hit the shooter.
            bullets.AddBullet(origin, reflected, blast_speed, power, blast.impulse,
                ID(), ID(), blast.hit_type, range * ::Random.randF(0.9f, 1.05f),
                cartridge, 1.f, send_hit, true);
        }
    }
}

bool CWeaponRPG7::AllowBore() { return inherited::AllowBore() && 0 != iAmmoElapsed; }

void CWeaponRPG7::FireTrace(const Fvector& P, const Fvector& D) {
    inherited::FireTrace(P, D);
    UpdateMissileVisibility();
}

void CWeaponRPG7::on_a_hud_attach() {
    inherited::on_a_hud_attach();
    UpdateMissileVisibility();
}

void CWeaponRPG7::UpdateMissileVisibility() {
    bool vis_hud, vis_weap;
    vis_hud = (!!iAmmoElapsed || GetState() == eReload);
    vis_weap = !!iAmmoElapsed;

    if (GetHUDmode()) {
        HudItemData()->set_bone_visible("grenade", vis_hud, TRUE);
    }

    IKinematics* pWeaponVisual = smart_cast<IKinematics*>(Visual());
    VERIFY(pWeaponVisual);
    pWeaponVisual->LL_SetBoneVisible(pWeaponVisual->LL_BoneID("grenade"), vis_weap, TRUE);
}

BOOL CWeaponRPG7::net_Spawn(CSE_Abstract* DC) {
    m_malfunction_rocket = u16(-1);
    BOOL l_res = inherited::net_Spawn(DC);

    UpdateMissileVisibility();
    if (iAmmoElapsed && !getCurrentRocket())
        CRocketLauncher::SpawnRocket(m_sRocketSection, this);

    return l_res;
}

void CWeaponRPG7::OnStateSwitch(u32 S) {
    inherited::OnStateSwitch(S);
    UpdateMissileVisibility();
}

void CWeaponRPG7::UnloadMagazine(bool spawn_ammo) {
    inherited::UnloadMagazine(spawn_ammo);
    UpdateMissileVisibility();
}

void CWeaponRPG7::ReloadMagazine() {
    inherited::ReloadMagazine();

    if (iAmmoElapsed && !getRocketCount())
        CRocketLauncher::SpawnRocket(m_sRocketSection.c_str(), this);
}

void CWeaponRPG7::SwitchState(u32 S) { inherited::SwitchState(S); }

void CWeaponRPG7::FireStart() { inherited::FireStart(); }

#include "inventory.h"
#include "inventoryOwner.h"
void CWeaponRPG7::switch2_Fire() {
    m_iShotNum = 0;
    m_bFireSingleShot = true;
    bWorking = false;

    if (GetState() == eFire && getRocketCount() && H_Parent()) {
        Fvector p1, d1, p;
        Fvector p2, d2, d;
        p1.set(get_LastFP());
        d1.set(get_LastFD());
        p = p1;
        d = d1;
        CEntity* E = smart_cast<CEntity*>(H_Parent());
        if (E && !TryGetGunslingerShotParams(*E, p, d)) {
            E->g_fireParams(this, p2, d2);
            p = p2;
            d = d2;

            if (IsHudModeNow()) {
                Fvector p0;
                float dist = HUD().GetCurrentRayQuery().range;
                p0.mul(d2, dist);
                p0.add(p1);
                p = p1;
                d.sub(p0, p1);
                d.normalize_safe();
            }
        }

        Fmatrix launch_matrix;
        launch_matrix.identity();
        launch_matrix.k.set(d);
        Fvector::generate_orthonormal_basis(launch_matrix.k, launch_matrix.j, launch_matrix.i);
        launch_matrix.c.set(p);

        d.normalize();
        d.mul(m_fLaunchSpeed);

        CExplosiveRocket* pGrenade = smart_cast<CExplosiveRocket*>(getCurrentRocket());
        VERIFY(pGrenade);
        pGrenade->SetInitiator(H_Parent()->ID());

        const bool actor_launch = OnServer() && IsGameTypeSingle() && ParentIsActor() &&
            iAmmoElapsed > 0 && !IsMisfire();
        const bool malfunction = actor_launch && ShouldExplodeOnLaunch();
        if (malfunction) {
            m_malfunction_rocket = pGrenade->ID();
            m_malfunction_position.set(Position());
            launch_matrix.c.set(m_malfunction_position);
            d.set(0.f, 0.f, 0.f);
            m_bFireSingleShot = false;
            UnloadMagazine(false);
            ForceUpdateAmmo();
        } else if (actor_launch) {
            ApplyBackblast(p1, d1);
        }

        CRocketLauncher::LaunchRocket(launch_matrix, d, zero_vel);

        if (OnServer()) {
            NET_Packet P;
            u_EventGen(P, GE_LAUNCH_ROCKET, ID());
            P.w_u16(pGrenade->ID());
            u_EventSend(P);
        }

        if (malfunction)
            SwitchState(eIdle);
    }
}

void CWeaponRPG7::PlayAnimReload() {
    VERIFY(GetState() == eReload);
    PlayHUDMotion("anm_reload", FALSE, this, GetState());
}

void CWeaponRPG7::OnEvent(NET_Packet& P, u16 type) {
    inherited::OnEvent(P, type);
    u16 id;
    switch (type) {
    case GE_OWNERSHIP_TAKE: {
        P.r_u16(id);
        CRocketLauncher::AttachRocket(id, this);
    } break;
    case GE_OWNERSHIP_REJECT:
    case GE_LAUNCH_ROCKET: {
        bool bLaunch = (type == GE_LAUNCH_ROCKET);
        P.r_u16(id);
        CRocketLauncher::DetachRocket(id, bLaunch);
        if (id == m_malfunction_rocket) {
            m_malfunction_rocket = u16(-1);
            if (bLaunch) {
                auto* rocket = smart_cast<CExplosiveRocket*>(Level().Objects.net_Find(id));
                if (rocket)
                    rocket->Contact(m_malfunction_position, Fvector().set(0.f, 1.f, 0.f));
            }
        }
        if (bLaunch)
            UpdateMissileVisibility();
    } break;
    }
}

void CWeaponRPG7::net_Import(NET_Packet& P) {
    inherited::net_Import(P);
    UpdateMissileVisibility();
}
