#pragma once
class CCar;
using BIDS = xr_vector<u16>;
struct CCarDamageParticles {
    BIDS bones1;
    BIDS bones2;
    shared_str m_wheels_damage_particles1;
    shared_str m_wheels_damage_particles2;
    shared_str m_car_damage_particles1;
    shared_str m_car_damage_particles2;

public:
    void Init(CCar* car);
    void Clear();
    void Play1(CCar* car);
    void Play2(CCar* car);
    // Ported by Ivan Danko, 2026-11-17, from Monolith (author Ray Twitty (aka Shadows)): FPS plan step 2 - Stop1 / Stop2 switch the car damage smoke off
    // (Monolith CarDamageParticles.h, added by Ray Twitty (aka Shadows); for the Lua function
    // StopDamageParticles).
    // Перенесення з Monolith (автор Ray Twitty (aka Shadows)) - Ivan Danko, 2026-11-17: крок 2 плану FPS - Stop1 / Stop2 вимикають дим пошкоджень
    // машини (Monolith CarDamageParticles.h, додав Ray Twitty (aka Shadows); для функції Lua
    // StopDamageParticles).
    void Stop1(CCar* car);
    void Stop2(CCar* car);
    void PlayWheel1(CCar* car, u16 bone_id) const;
    void PlayWheel2(CCar* car, u16 bone_id) const;
};