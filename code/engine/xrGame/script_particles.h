////////////////////////////////////////////////////////////////////////////
//	Module 		: script_particles.h
//	Created 	: 27.07.2004
//  Modified 	: 27.07.2004
//	Author		: Alexander Maximchuk
//	Description : XRay Script particles class
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "script_export_space.h"
#include "particlesobject.h"

// refs
class CObjectAnimator;
class CScriptParticles;

class CScriptParticlesCustom : public CParticlesObject {
    CObjectAnimator* m_animator;
    virtual ~CScriptParticlesCustom();
    CScriptParticles* m_owner;

public:
    CScriptParticlesCustom(CScriptParticles* owner, LPCSTR caParticlesName);
    virtual void shedule_Update(u32 dt);

    void LoadPath(LPCSTR caPathName);
    void StartPath(bool looped);
    void StopPath();
    void PausePath(bool val);
    virtual void PSI_internal_delete();
    virtual void PSI_destroy();
    void remove_owner();
};

class CScriptParticles {
    // Ported by Ivan Danko, 2026-11-17, from Monolith (author themrdemonized): FPS plan step 2 - the transform of the effect, as in Anomaly
    // (Monolith script_particles.h): set_position / set_direction / set_orientation / move_to /
    // play_at_pos keep the position and the turn of the effect here, last_position reads it.
    // Перенесення з Monolith (автор themrdemonized) - Ivan Danko, 2026-11-17: крок 2 плану FPS - матриця ефекту, як в Anomaly
    // (Monolith script_particles.h): set_position / set_direction / set_orientation / move_to /
    // play_at_pos зберігають тут положення і поворот ефекту, last_position читає його.
private:
    Fmatrix m_transform;

public:
    CScriptParticlesCustom* m_particles;
    CScriptParticles(LPCSTR caParticlesName);
    virtual ~CScriptParticles();

    // Ported by Ivan Danko, 2026-11-17, from Monolith (author themrdemonized): FPS plan step 2 - play / play_at_pos with the HUD mode flag, as in
    // Anomaly (Monolith: Play(bool bHudMode = false), PlayAtPos(pos, bool bHudMode = false)). Adapted: two
    // functions each instead of a default argument - Lua does not see C++ default arguments, so the old
    // calls play() / play_at_pos(pos) keep working next to play(true) / play_at_pos(pos, true).
    // Перенесення з Monolith (автор themrdemonized) - Ivan Danko, 2026-11-17: крок 2 плану FPS - play / play_at_pos з прапорцем режиму HUD,
    // як в Anomaly (Monolith: Play(bool bHudMode = false), PlayAtPos(pos, bool bHudMode = false)).
    // Адаптовано: по дві функції замість типового аргументу - Lua не бачить типових аргументів C++, тож
    // старі виклики play() / play_at_pos(pos) працюють і далі поряд із play(true) / play_at_pos(pos, true).
    void Play();
    void Play(bool bHudMode);
    void PlayAtPos(const Fvector& pos);
    void PlayAtPos(const Fvector& pos, bool bHudMode);
    void Stop();
    void StopDeffered();

    bool IsPlaying() const;
    bool IsLooped() const;

    void MoveTo(const Fvector& pos, const Fvector& vel);
    // Ported by Ivan Danko, 2026-11-17, from Monolith (author themrdemonized): FPS plan step 2 - the Lua functions set_position, set_direction,
    // set_orientation, set_hud_mode and last_position of Anomaly (Monolith script_particles.h).
    // Перенесення з Monolith (автор themrdemonized) - Ivan Danko, 2026-11-17: крок 2 плану FPS - функції Lua set_position, set_direction,
    // set_orientation, set_hud_mode і last_position з Anomaly (Monolith script_particles.h).
    void XFORMMoveTo(const Fvector& pos);
    void SetDirection(const Fvector& dir);
    void SetOrientation(float yaw, float pitch, float roll);
    void SetHudMode(bool bHudMode);
    Fvector LastPosition() const { return m_transform.c; }

    void LoadPath(LPCSTR caPathName);
    void StartPath(bool looped);
    void StopPath();
    void PausePath(bool val);

    DECLARE_SCRIPT_REGISTER_FUNCTION
};
add_to_type_list(CScriptParticles)
#undef script_type_list
#define script_type_list save_type_list(CScriptParticles)

#include "script_particles_inline.h"