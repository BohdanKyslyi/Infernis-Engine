// WeaponFire.cpp: implementation of the CWeapon class.
// function responsible for firing with CWeapon
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "Weapon.h"
#include "ParticlesObject.h"
#include "entity.h"
#include "actor.h"

#include "actoreffector.h"
#include "effectorshot.h"

#include "level_bullet_manager.h"
#include "HUDTarget.h"
#include "level.h"
#include <cmath>
#include "../xrEngine/IGame_Persistent.h"
#include "../xrEngine/ssfx_console_vars.h" // ps_ssfx_int_grass_params_2

#define FLAME_TIME 0.05f

Flags32 g_gunslinger_real_shooting = { 0 };

float _nrand(float sigma) {
#define ONE_OVER_SIGMA_EXP (1.0f / 0.7975f)

    if (sigma == 0)
        return 0;

    float y;
    do {
        y = -logf(Random.randF());
    } while (Random.randF() > expf(-xr::sqr(y - 1.0f) * 0.5f));
    if (rand() & 0x1)
        return y * sigma * ONE_OVER_SIGMA_EXP;
    else
        return -y * sigma * ONE_OVER_SIGMA_EXP;
}

void random_dir(Fvector& tgt_dir, const Fvector& src_dir, float dispersion) {
    float sigma = dispersion / 3.f;
    float alpha = clampr(_nrand(sigma), -dispersion, dispersion);
    float theta = Random.randF(0, PI);
    float r = tan(alpha);
    Fvector U, V, T;
    Fvector::generate_orthonormal_basis(src_dir, U, V);
    U.mul(r * std::sin(theta));
    V.mul(r * std::cos(theta));
    T.add(U, V);
    tgt_dir.add(src_dir, T).normalize();
}

float CWeapon::GetWeaponDeterioration() { return conditionDecreasePerShot; };

bool CWeapon::TryGetGunslingerShotParams(CEntity& shooter, Fvector& position, Fvector& direction) {
    if (!IsGameTypeSingle() || &shooter != Level().CurrentControlEntity() ||
        !smart_cast<CActor*>(&shooter))
        return false;

    const LPCSTR hud_section = HudSection().c_str();
    const LPCSTR weapon_section = cNameSect().c_str();
    const bool real_shooting = g_gunslinger_real_shooting.test(1);
    if (!real_shooting && !pSettings->line_exist(hud_section, "hud_recalc_koef") &&
        !pSettings->line_exist(weapon_section, "bullet_point_offset_hud") &&
        !pSettings->line_exist(weapon_section, "bullet_point_offset_world"))
        return false;

    const bool hud_mode = GetHUDmode();
    if (hud_mode && IsZoomed()) {
        position = Device.vCameraPosition;
        direction = Device.vCameraDirection;
        return true;
    }

    Fvector view_position = get_LastFP();
    Fvector view_direction = get_LastFD();
    shooter.g_fireParams(this, view_position, view_direction);

    Fvector muzzle_direction = get_LastFD();
    muzzle_direction.normalize_safe();
    if (!xr::valid(muzzle_direction) || muzzle_direction.square_magnitude() < EPS_S)
        return false;

    const float offset = hud_mode
        ? READ_IF_EXISTS(pSettings, r_float, weapon_section, "bullet_point_offset_hud", -1.f)
        : READ_IF_EXISTS(pSettings, r_float, weapon_section, "bullet_point_offset_world", -0.3f);
    position.mad(get_LastFP(), muzzle_direction, std::isfinite(offset) ? offset : 0.f);

    // Keep the launch point on the shooter's side of nearby solid geometry.
    Fvector to_origin;
    to_origin.sub(position, view_position);
    const float origin_distance = to_origin.magnitude();
    if (origin_distance > EPS_S) {
        to_origin.div(origin_distance);
        collide::rq_result obstacle;
        if (Level().ObjectSpace.RayPick(view_position, to_origin, origin_distance,
                                       collide::rqtStatic, obstacle, &shooter))
            position.mad(view_position, to_origin, obstacle.range > 0.01f ? obstacle.range - 0.01f : 0.f);
    }

    const bool hip_laser = hud_mode && GetZoomRotationFactor() <= EPS_S && HasActiveHudLaser();
    if (hip_laser || real_shooting || g_SingleGameDifficulty >= egdVeteran) {
        direction = muzzle_direction;
        if (hud_mode) {
            const float hud_recalc = READ_IF_EXISTS(pSettings, r_float, hud_section, "hud_recalc_koef", 1.f);
            const float fov = READ_IF_EXISTS(pSettings, r_float, "gunslinger_base", "fov", 65.f);
            const float hud_fov = READ_IF_EXISTS(pSettings, r_float, "gunslinger_base", "hud_fov", 40.f);
            if (std::isfinite(hud_recalc) && std::isfinite(fov) && std::isfinite(hud_fov) &&
                hud_recalc > 0.f && fov > 0.f && hud_fov > EPS_S) {
                Fvector corrected;
                corrected.sub(muzzle_direction, view_direction);
                corrected.mad(view_direction, corrected, hud_recalc * fov / hud_fov).normalize_safe();
                if (corrected.square_magnitude() >= EPS_S)
                    direction = corrected;
            }
        }
        return true;
    }

    constexpr float aim_range = 1000.f;
    const float distance = CHUDTarget::TraceAimDistance(view_position, view_direction, aim_range, &shooter);
    Fvector aim_point;
    aim_point.mad(view_position, view_direction, distance);
    direction.sub(aim_point, position).normalize_safe();
    if (direction.square_magnitude() < EPS_S)
        direction = view_direction;
    return true;
}

void CWeapon::FireTrace(const Fvector& P, const Fvector& D) {
    FireTraceFrom(*H_Parent(), P, D);
}

void CWeapon::FireTraceFrom(CObject& shooter, const Fvector& P, const Fvector& D) {
    VERIFY(m_magazine.size());

    CCartridge& l_cartridge = m_magazine.back();
    UpdateHudAmmoBones(&l_cartridge);
    //	Msg("ammo - %s", l_cartridge.m_ammoSect.c_str());
    VERIFY(u16(-1) != l_cartridge.bullet_material_idx);
    //-------------------------------------------------------------
    bool is_tracer = m_bHasTracers && !!l_cartridge.m_flags.test(CCartridge::cfTracer);
    if (is_tracer && !IsGameTypeSingle())
        is_tracer = is_tracer /*&& (m_magazine.size() % 3 == 0)*/ && !IsSilencerAttached();

    l_cartridge.m_flags.set(CCartridge::cfTracer, is_tracer);
    if (m_u8TracerColorID != u8(-1))
        l_cartridge.param_s.u8ColorID = m_u8TracerColorID;
    //-------------------------------------------------------------
    //повысить изношенность оружия с учетом влияния конкретного патрона
    //	float Deterioration = GetWeaponDeterioration();
    //	Msg("Deterioration = %f", Deterioration);
    ChangeCondition(-GetWeaponDeterioration() * l_cartridge.param_s.impair);

    float fire_disp = 0.f;
    CActor* tmp_actor = NULL;
    if (!IsGameTypeSingle()) {
        tmp_actor = smart_cast<CActor*>(Level().CurrentControlEntity());
        if (tmp_actor) {
            CEntity::SEntityState state;
            tmp_actor->g_State(state);
            if (m_first_bullet_controller.is_bullet_first(state.fVelocity)) {
                fire_disp = m_first_bullet_controller.get_fire_dispertion();
                m_first_bullet_controller.make_shot();
            }
        }
    }
    if (fsimilar(fire_disp, 0.f)) {
        // CActor* tmp_actor = smart_cast<CActor*>(Level().CurrentControlEntity());
        if (H_Parent() && (H_Parent() == tmp_actor)) {
            fire_disp = tmp_actor->GetFireDispertion();
        } else {
            fire_disp = GetFireDispersion(true);
        }
    }

    bool SendHit = SendHitAllowed(&shooter);
    //выстерлить пулю (с учетом возможной стрельбы дробью)
    for (int i = 0; i < l_cartridge.param_s.buckShot; ++i) {
        FireBullet(P, D, fire_disp, l_cartridge, shooter.ID(), ID(), SendHit);
    }

    if (!IsSilencerAttached()) {
        StartShotParticles();
        if (m_bLightShotEnabled)
            Light_Start();
    }

    // Interactive Grass FX
    Fvector ShotPos = Fvector().mad(P, D, 1.5f);
    g_pGamePersistent->GrassBendersAddShot(cast_game_object()->ID(), ShotPos, D, 3.0f, 20.0f,
                                           ps_ssfx_int_grass_params_2.z,
                                           ps_ssfx_int_grass_params_2.w);

    // Ammo
    m_magazine.pop_back();
    --iAmmoElapsed;

    VERIFY((u32)iAmmoElapsed == m_magazine.size());
}

void CWeapon::StopShooting() {
    //	SetPending			(TRUE);

    //принудительно останавливать зацикленные партиклы
    if (m_pFlameParticles && m_pFlameParticles->IsLooped())
        StopFlameParticles();

    SwitchState(eIdle);

    bWorking = false;
}

void CWeapon::FireEnd() {
    CShootingObject::FireEnd();
    StopShotEffector();
}

void CWeapon::StartFlameParticles2() {
    CShootingObject::StartParticles(m_pFlameParticles2, *m_sFlameParticles2, get_LastFP2());
}
void CWeapon::StopFlameParticles2() { CShootingObject::StopParticles(m_pFlameParticles2); }
void CWeapon::UpdateFlameParticles2() {
    if (m_pFlameParticles2)
        CShootingObject::UpdateParticles(m_pFlameParticles2, get_LastFP2());
}
