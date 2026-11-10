#include "stdafx.h"
#include "weaponpistol.h"
#include "ParticlesObject.h"
#include "actor.h"

CWeaponPistol::CWeaponPistol() {
    m_eSoundClose = ESoundTypes(SOUND_TYPE_WEAPON_RECHARGING);
    SetPending(FALSE);
}

CWeaponPistol::~CWeaponPistol(void) {}

void CWeaponPistol::net_Destroy() { inherited::net_Destroy(); }

void CWeaponPistol::Load(LPCSTR section) {
    inherited::Load(section);

    m_sounds.LoadSound(section, "snd_close", "sndClose", false, m_eSoundClose);
}

void CWeaponPistol::OnH_B_Chield() { inherited::OnH_B_Chield(); }

void CWeaponPistol::PlayAnimIdleSprint() {
    if (IsMisfire()) {
        inherited::PlayAnimIdleSprint();
        return;
    }
    if (iAmmoElapsed == 0) {
        PlayHUDMotion(OptionalHudMotion("anm_idle_sprint_empty", "anm_idle_sprint"), TRUE, NULL, GetState());
    } else {
        inherited::PlayAnimIdleSprint();
    }
}

void CWeaponPistol::PlayAnimIdleMoving() {
    if (IsMisfire()) {
        inherited::PlayAnimIdleMoving();
        return;
    }
    if (iAmmoElapsed == 0) {
        PlayHUDMotion(OptionalHudMotion("anm_idle_moving_empty", "anm_idle_moving"), TRUE, NULL, GetState());
    } else {
        inherited::PlayAnimIdleMoving();
    }
}

void CWeaponPistol::PlayAnimAim() {
    if (IsMisfire()) {
        inherited::PlayAnimAim();
        return;
    }
    if (iAmmoElapsed == 0)
        PlayHUDMotion(OptionalHudMotion("anm_idle_aim_empty", "anm_idle_aim"), TRUE, NULL, GetState());
    else
        inherited::PlayAnimAim();
}

void CWeaponPistol::PlayAnimReload() {
    VERIFY(GetState() == eReload);
    inherited::PlayAnimReload();
}

void CWeaponPistol::PlayAnimHide() {
    VERIFY(GetState() == eHiding);
    if (iAmmoElapsed == 0 && pSettings->line_exist(cNameSect().c_str(), "snd_close"))
        PlaySound("sndClose", get_LastFP());
    inherited::PlayAnimHide();
}

void CWeaponPistol::PlayAnimShoot() {
    VERIFY(GetState() == eFire);
    LPCSTR fallback = OptionalHudMotion("anm_shots", "anm_shoot");
    if (iAmmoElapsed <= 1)
        fallback = OptionalHudMotion("anm_shot_l", fallback);
    PlayHUDMotion(ShootMotion(fallback), FALSE, this, GetState());
}

void CWeaponPistol::switch2_Reload() { inherited::switch2_Reload(); }

void CWeaponPistol::OnAnimationEnd(u32 state) { inherited::OnAnimationEnd(state); }

void CWeaponPistol::OnShot() {
    PlaySound(m_sSndShotCurrent.c_str(), get_LastFP());

    AddShotEffector();

    PlayAnimShoot();

    // Shell Drop
    Fvector vel;
    PHGetLinearVell(vel);
    OnShellDrop(get_LastSP(), vel);

    // ќгонь из ствола

    StartFlameParticles();
    R_ASSERT2(!m_pFlameParticles || !m_pFlameParticles->IsLooped(),
              "can't set looped particles system for shoting with pistol");

    //дым из ствола
    StartSmokeParticles(get_LastFP(), vel);
}

void CWeaponPistol::UpdateSounds() {
    inherited::UpdateSounds();
    m_sounds.SetPosition("sndClose", get_LastFP());
}