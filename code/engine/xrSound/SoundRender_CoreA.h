#ifndef SoundRender_CoreAH
#define SoundRender_CoreAH
#pragma once

#include "SoundRender_Core.h"
#define AL_ALEXT_PROTOTYPES 1
#include <openal/al.h>
#include <openal/alc.h>
#include <openal/efx.h>

#ifndef ALC_HRTF_SOFT
#define ALC_HRTF_SOFT 0x1992
#endif

#ifdef DEBUG
#define A_CHK(expr) { alGetError(); expr; ALenum error = alGetError(); VERIFY2(error == AL_NO_ERROR, (LPCSTR)alGetString(error)); }
#define AC_CHK(expr) { alcGetError(pDevice); expr; ALCenum error = alcGetError(pDevice); VERIFY2(error == ALC_NO_ERROR, (LPCSTR)alcGetString(pDevice, error)); }
#else
#define A_CHK(expr) { expr; }
#define AC_CHK(expr) { expr; }
#endif

class CSoundRender_CoreA : public CSoundRender_Core {
    typedef CSoundRender_Core inherited;
    ALCdevice* pDevice;
    ALCcontext* pContext;

    struct SListener {
        Fvector position;
        Fvector orientation[2];
    };
    SListener Listener;

public:
    // Змінні для слоту реверберації EFX
    ALuint effect_slot;
    ALuint reverb_effect;

    // БУФЕРИ ДЛЯ ПЛАВНОГО ПЕРЕХОДУ (LERP) 
    bool  bEFX_Initialized;
    float fTimeDelta;
    
    float env_density;
    float env_room;
    float env_room_hf;
    float env_decay_time;
    float env_decay_hf_ratio;
    float env_reflections_delay;
    float env_reverb_delay;
    float env_room_rolloff_factor;
    float env_reflections;
    float env_reverb;
    float env_air_absorption_hf;

    // СКРИПТОВИЙ OVERRIDE EFX 
    bool  bEFX_ScriptOverride;
    float s_env_density;
    float s_env_room;
    float s_env_room_hf;
    float s_env_decay_time;
    float s_env_decay_hf_ratio;
    float s_env_reflections_delay;
    float s_env_reverb_delay;
    float s_env_room_rolloff_factor;
    float s_env_reflections;
    float s_env_reverb;
    float s_env_air_absorption_hf;

    // Ручне введення ефектів
    virtual void set_efx_override(bool bEnable, float room = -10000.0f, float room_hf = 0.0f, float decay_time = 1.0f, float decay_hf_ratio = 0.5f, float reflections_delay = 0.02f, float reverb_delay = 0.04f, float room_rolloff_factor = 0.0f, float diffusion = 1.0f, float reflections = -2602.0f, float reverb = 200.0f, float air_absorption_hf = -5.0f);
    // Читання готового пресета з LTX
    virtual void set_efx_override(LPCSTR preset_name);

protected:
    virtual void update_listener(const Fvector& P, const Fvector& D, const Fvector& N, float dt);
    virtual void update_environment(CSound_environment* E);

public:
    CSoundRender_CoreA();
    virtual ~CSoundRender_CoreA();

    virtual void _initialize(int stage);
    virtual void _clear();
    virtual void _restart();

    virtual void set_master_volume(float f);
    virtual const Fvector& listener_position() { return Listener.position; }
};

extern CSoundRender_CoreA* SoundRenderA;
#endif