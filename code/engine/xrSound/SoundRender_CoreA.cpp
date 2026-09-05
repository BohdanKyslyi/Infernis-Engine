#include "stdafx.h"
#pragma hdrstop

#include "soundrender_coreA.h"
#include "soundrender_targetA.h"

CSoundRender_CoreA* SoundRenderA = 0;

__declspec(dllexport) u32 snd_device_id = 0;
__declspec(dllexport) xr_token* snd_devices_token = NULL;
__declspec(dllexport) int snd_hrtf = 1;

CSoundRender_CoreA::CSoundRender_CoreA() : CSoundRender_Core() {
    pDevice = 0;
    pContext = 0;
    effect_slot = 0;
    reverb_effect = 0;

    bEFX_Initialized = false;
    bEFX_ScriptOverride = false;
    fTimeDelta = 0.033f;
    
    env_density = 0.0f;
    env_room = 0.0f;
    env_room_hf = 0.0f;
    env_decay_time = 0.0f;
    env_decay_hf_ratio = 0.0f;
    env_reflections_delay = 0.0f;
    env_reverb_delay = 0.0f;
    env_room_rolloff_factor = 0.0f;
    env_reflections = 0.0f;
    env_reverb = 0.0f;
    env_air_absorption_hf = 0.0f;
}

CSoundRender_CoreA::~CSoundRender_CoreA() {}

void CSoundRender_CoreA::_restart() {
    inherited::_restart();
}

void CSoundRender_CoreA::_initialize(int stage) {
    if (stage == 0) {
        if (!snd_devices_token) {
            xr_vector<xr_token> tokens;
            tokens.push_back({ xr_strdup("Default"), 0 });
            
            if (alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT")) {
                const ALCchar* devices = alcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER);
                int id = 1;
                while (devices && *devices != '\0') {
                    tokens.push_back({ xr_strdup(devices), id++ });
                    devices += xr_strlen(devices) + 1; 
                }
            }
            tokens.push_back({ NULL, -1 }); 
            
            snd_devices_token = xr_alloc<xr_token>(tokens.size());
            for (size_t i = 0; i < tokens.size(); ++i) {
                snd_devices_token[i] = tokens[i];
            }
        }
        return;
    }

    LPCSTR device_to_open = nullptr;
    if (snd_devices_token && snd_device_id != 0) {
        xr_token* tok = snd_devices_token;
        while (tok->name) {
            if (tok->id == (int)snd_device_id) {
                device_to_open = tok->name;
                break;
            }
            tok++;
        }
    }

    pDevice = alcOpenDevice(device_to_open); 
    if (pDevice == NULL) {
        Msg("! [Noir Engine] OpenAL: Failed to open device '%s'. Falling back to default.", device_to_open ? device_to_open : "Default");
        pDevice = alcOpenDevice(nullptr); 
        if (pDevice == NULL) {
            CHECK_OR_EXIT(0, "! [Noir Engine] OpenAL: Failed to create device.");
            bPresent = FALSE;
            return;
        }
    }

    ALCint contextAttr[] = { ALC_HRTF_SOFT, (snd_hrtf ? ALC_TRUE : ALC_FALSE), 0 };
    pContext = alcCreateContext(pDevice, contextAttr);
    if (0 == pContext) {
        pContext = alcCreateContext(pDevice, nullptr); 
        if (0 == pContext) {
            CHECK_OR_EXIT(0, "! [Noir Engine] OpenAL: Failed to create context.");
            bPresent = FALSE;
            alcCloseDevice(pDevice);
            pDevice = 0;
            return;
        }
    }

    alGetError();
    alcGetError(pDevice);
    AC_CHK(alcMakeContextCurrent(pContext));

    ALCint hrtf_state;
    alcGetIntegerv(pDevice, ALC_HRTF_SOFT, 1, &hrtf_state);
    Msg("* [Noir Engine] OpenAL Soft: HRTF is %s", hrtf_state ? "ENABLED" : "DISABLED");
    
    alDistanceModel(AL_EXPONENT_DISTANCE_CLAMPED);
    
    A_CHK(alListener3f(AL_POSITION, 0.f, 0.f, 0.f));
    A_CHK(alListener3f(AL_VELOCITY, 0.f, 0.f, 0.f));
    Fvector orient[2] = { { 0.f, 0.f, 1.f }, { 0.f, 1.f, 0.f } };
    A_CHK(alListenerfv(AL_ORIENTATION, &orient[0].x));
    A_CHK(alListenerf(AL_GAIN, 1.f));

    // ПЕРЕМИКАЧ EFX (Апаратний рівень)
    bEFX = false;
    bEFX_Initialized = false; 
    
    if (alcIsExtensionPresent(pDevice, "ALC_EXT_EFX")) {
        Msg("* [Noir Engine] OpenAL: Hardware supports EFX. Ready for dynamic toggling.");
        bEFX = true;
        
        alGenAuxiliaryEffectSlots(1, &effect_slot);
        alGenEffects(1, &reverb_effect);
        alEffecti(reverb_effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
    } else {
        Msg("* [Noir Engine] OpenAL: EFX is NOT supported by hardware.");
    }

    inherited::_initialize(stage);

    if (stage == 1) {
        CSoundRender_Target* T = 0;
        for (u32 tit = 0; tit < u32(psSoundTargets); tit++) {
            T = xr_new<CSoundRender_TargetA>();
            if (T->_initialize()) {
                s_targets.push_back(T);
            } else {
                Log("! [Noir Engine] OpenAL: Max targets - ", tit);
                T->_destroy();
                xr_delete(T);
                break;
            }
        }
    }
}

void CSoundRender_CoreA::set_master_volume(float f) {
    if (bPresent) A_CHK(alListenerf(AL_GAIN, f));
}

void CSoundRender_CoreA::_clear() {
    inherited::_clear();
    CSoundRender_Target* T = 0;
    for (u32 tit = 0; tit < s_targets.size(); tit++) {
        T = s_targets[tit];
        T->_destroy();
        xr_delete(T);
    }

    if (bEFX) {
        alDeleteEffects(1, &reverb_effect);
        alDeleteAuxiliaryEffectSlots(1, &effect_slot);
    }

    alcMakeContextCurrent(NULL);
    if (pContext) alcDestroyContext(pContext);
    pContext = 0;
    if (pDevice) alcCloseDevice(pDevice);
    pDevice = 0;
}

// СКРИПТОВИЙ КОНТРОЛЬ EFX
void CSoundRender_CoreA::set_efx_override(bool bEnable, float room, float room_hf, float decay_time, float decay_hf_ratio, float reflections_delay, float reverb_delay, float room_rolloff_factor, float diffusion, float reflections, float reverb, float air_absorption_hf) {
    bEFX_ScriptOverride = bEnable;
    if (bEnable) {
        s_env_room = room;
        s_env_room_hf = room_hf;
        s_env_decay_time = decay_time;
        s_env_decay_hf_ratio = decay_hf_ratio;
        s_env_reflections_delay = reflections_delay;
        s_env_reverb_delay = reverb_delay;
        s_env_room_rolloff_factor = room_rolloff_factor;
        s_env_density = diffusion;
        s_env_reflections = reflections;
        s_env_reverb = reverb;
        s_env_air_absorption_hf = air_absorption_hf;
    }
}

void CSoundRender_CoreA::set_efx_override(LPCSTR preset_name) {
    if (!preset_name || xr_strlen(preset_name) == 0) {
        bEFX_ScriptOverride = false;
        return;
    }

    string_path env_path;
    if (FS.exist(env_path, "$game_config$", "environment\\noirEnvZone.ltx")) {
        CInifile env_ini(env_path);
        
        if (env_ini.section_exist(preset_name)) {
            float t_room                = -10000.0f;
            float t_room_hf             = 0.0f;
            float t_decay_time          = 1.0f;
            float t_decay_hf_ratio      = 0.5f;
            float t_reflections_delay   = 0.02f;
            float t_reverb_delay        = 0.04f;
            float t_room_rolloff_factor = 0.0f;
            float t_density             = 1.0f;
            float t_reflections         = -2602.0f;
            float t_reverb              = 200.0f;
            float t_air_absorption_hf   = -5.0f;

			if (env_ini.line_exist(preset_name, "room")) {
                t_room = env_ini.r_float(preset_name, "room");
                clamp(t_room, -10000.0f, 0.0f);
            }
            if (env_ini.line_exist(preset_name, "room_hf")) {
                t_room_hf = env_ini.r_float(preset_name, "room_hf");
                clamp(t_room_hf, -10000.0f, 0.0f);
            }
            if (env_ini.line_exist(preset_name, "decay_time")) {
                t_decay_time = env_ini.r_float(preset_name, "decay_time");
                clamp(t_decay_time, 0.1f, 20.0f);
            }
            if (env_ini.line_exist(preset_name, "decay_hf_ratio")) {
                t_decay_hf_ratio = env_ini.r_float(preset_name, "decay_hf_ratio");
                clamp(t_decay_hf_ratio, 0.1f, 2.0f);
            }
            if (env_ini.line_exist(preset_name, "reflections_delay")) {
                t_reflections_delay = env_ini.r_float(preset_name, "reflections_delay");
                clamp(t_reflections_delay, 0.0f, 0.3f);
            }
            if (env_ini.line_exist(preset_name, "reverb_delay")) {
                t_reverb_delay = env_ini.r_float(preset_name, "reverb_delay");
                clamp(t_reverb_delay, 0.0f, 0.1f);
            }
            if (env_ini.line_exist(preset_name, "room_rolloff_factor")) {
                t_room_rolloff_factor = env_ini.r_float(preset_name, "room_rolloff_factor");
                clamp(t_room_rolloff_factor, 0.0f, 10.0f);
            }
            
            if (env_ini.line_exist(preset_name, "reflections")) {
                t_reflections = env_ini.r_float(preset_name, "reflections");
                clamp(t_reflections, -10000.0f, 1000.0f);
            }
            if (env_ini.line_exist(preset_name, "reverb")) {
                t_reverb = env_ini.r_float(preset_name, "reverb");
                clamp(t_reverb, -10000.0f, 2000.0f);
            }
            if (env_ini.line_exist(preset_name, "air_absorption_hf")) {
                t_air_absorption_hf = env_ini.r_float(preset_name, "air_absorption_hf");
                clamp(t_air_absorption_hf, -100.0f, 0.0f);
            }

            if (env_ini.line_exist(preset_name, "diffusion")) {
                t_density = env_ini.r_float(preset_name, "diffusion");
                clamp(t_density, 0.0f, 1.0f);
            } else if (env_ini.line_exist(preset_name, "environment_diffusion")) {
                t_density = env_ini.r_float(preset_name, "environment_diffusion");
                clamp(t_density, 0.0f, 1.0f);
            }

            s_env_room                = t_room;
            s_env_room_hf             = t_room_hf;
            s_env_decay_time          = t_decay_time;
            s_env_decay_hf_ratio      = t_decay_hf_ratio;
            s_env_reflections_delay   = t_reflections_delay;
            s_env_reverb_delay        = t_reverb_delay;
            s_env_room_rolloff_factor = t_room_rolloff_factor;
            s_env_density             = t_density;
            s_env_reflections         = t_reflections;
            s_env_reverb              = t_reverb;
            s_env_air_absorption_hf   = t_air_absorption_hf;

            bEFX_ScriptOverride = true;
            Msg("- [Noir Engine] EFX Script: Successfully applied preset [%s] from noirEnvZone.ltx", preset_name);
        } else {
            Msg("! [Noir Engine] EFX Script: Error! Section [%s] not found in noirEnvZone.ltx!", preset_name);
        }
    } else {
        Msg("! [Noir Engine] EFX Script: Error! File environment\\noirEnvZone.ltx does not exist!");
    }
}
// ================================

void CSoundRender_CoreA::update_environment(CSound_environment* _E) {
    if (!bEFX) return;
    
    if (!psSoundFlags.test(ss_EFX)) {
        alEffectf(reverb_effect, AL_REVERB_GAIN, 0.0f);
        alAuxiliaryEffectSloti(effect_slot, AL_EFFECTSLOT_EFFECT, reverb_effect);
        bEFX_Initialized = false; 
        return;
    }

    CSoundRender_Environment* E = static_cast<CSoundRender_Environment*>(_E);
    
    // Вибір пріоритету: скрипт чи фізична зона
    float target_density             = bEFX_ScriptOverride ? s_env_density             : E->EnvironmentDiffusion;
    float target_room                = bEFX_ScriptOverride ? s_env_room                : E->Room;
    float target_room_hf             = bEFX_ScriptOverride ? s_env_room_hf             : E->RoomHF;
    float target_decay_time          = bEFX_ScriptOverride ? s_env_decay_time          : E->DecayTime;
    float target_decay_hf_ratio      = bEFX_ScriptOverride ? s_env_decay_hf_ratio      : E->DecayHFRatio;
    float target_room_rolloff_factor = bEFX_ScriptOverride ? s_env_room_rolloff_factor : E->RoomRolloffFactor;
    
    // ФІКС ЗЛАМАНИХ СДК ЗОН
    // X-Ray не інтерполює ці параметри, тому для базових зон ми жорстко ставимо ідеальні дефолти OpenAL.
    float target_reflections_delay   = bEFX_ScriptOverride ? s_env_reflections_delay   : 0.007f;
    float target_reverb_delay        = bEFX_ScriptOverride ? s_env_reverb_delay        : 0.011f;
    float target_reflections         = bEFX_ScriptOverride ? s_env_reflections         : -2602.0f;
    float target_reverb              = bEFX_ScriptOverride ? s_env_reverb              : 200.0f;
    float target_air_absorption_hf   = bEFX_ScriptOverride ? s_env_air_absorption_hf   : -5.0f;

    // Логіка лінійної інтерполяції (Lerp) 
    if (!bEFX_Initialized) {
        env_density             = target_density;
        env_room                = target_room;
        env_room_hf             = target_room_hf;
        env_decay_time          = target_decay_time;
        env_decay_hf_ratio      = target_decay_hf_ratio;
        env_reflections_delay   = target_reflections_delay;
        env_reverb_delay        = target_reverb_delay;
        env_room_rolloff_factor = target_room_rolloff_factor;
        env_reflections         = target_reflections;
        env_reverb              = target_reverb;
        env_air_absorption_hf   = target_air_absorption_hf;
        bEFX_Initialized = true;
    } else {
        float lerp_speed = 1.5f * fTimeDelta; 
        clamp(lerp_speed, 0.0f, 1.0f);

        env_density             += (target_density             - env_density)             * lerp_speed;
        env_room                += (target_room                - env_room)                * lerp_speed;
        env_room_hf             += (target_room_hf             - env_room_hf)             * lerp_speed;
        env_decay_time          += (target_decay_time          - env_decay_time)          * lerp_speed;
        env_decay_hf_ratio      += (target_decay_hf_ratio      - env_decay_hf_ratio)      * lerp_speed;
        env_reflections_delay   += (target_reflections_delay   - env_reflections_delay)   * lerp_speed;
        env_reverb_delay        += (target_reverb_delay        - env_reverb_delay)        * lerp_speed;
        env_room_rolloff_factor += (target_room_rolloff_factor - env_room_rolloff_factor) * lerp_speed;
        env_reflections         += (target_reflections         - env_reflections)         * lerp_speed;
        env_reverb              += (target_reverb              - env_reverb)              * lerp_speed;
        env_air_absorption_hf   += (target_air_absorption_hf   - env_air_absorption_hf)   * lerp_speed;
    }
    
    // КОНВЕРТАЦІЯ міліБелів (mB) у лінійний множник гучності (Gain)
    float gain             = powf(10.0f, env_room / 2000.0f);
    float gainHF           = powf(10.0f, env_room_hf / 2000.0f);
    float reflections_gain = powf(10.0f, env_reflections / 2000.0f);
    float reverb_gain      = powf(10.0f, env_reverb / 2000.0f);
    float air_gain_hf      = powf(10.0f, env_air_absorption_hf / 2000.0f);

    // ЖОРСТКИЙ ЗАХИСТ ВІД КРАШУ OPENAL
    clamp(env_density,             0.0f,   1.0f);
    clamp(gain,                    0.0f,   1.0f);
    clamp(gainHF,                  0.0f,   1.0f);
    clamp(env_decay_time,          0.1f,   20.0f);
    clamp(env_decay_hf_ratio,      0.1f,   2.0f);
    clamp(reflections_gain,        0.0f,   3.16f);
    clamp(env_reflections_delay,   0.0f,   0.3f);
    clamp(reverb_gain,             0.0f,   10.0f);
    clamp(env_reverb_delay,        0.0f,   0.1f);
    clamp(env_room_rolloff_factor, 0.0f,   10.0f);
    clamp(air_gain_hf,             0.892f, 1.0f); 
    
    // Передаємо параметри в OpenAL EFX
    alEffectf(reverb_effect, AL_REVERB_DENSITY,               1.0f);
    alEffectf(reverb_effect, AL_REVERB_DIFFUSION,             env_density);
    alEffectf(reverb_effect, AL_REVERB_GAIN,                  gain);
    alEffectf(reverb_effect, AL_REVERB_GAINHF,                gainHF);
    alEffectf(reverb_effect, AL_REVERB_DECAY_TIME,            env_decay_time);
    alEffectf(reverb_effect, AL_REVERB_DECAY_HFRATIO,         env_decay_hf_ratio);
    
    alEffectf(reverb_effect, AL_REVERB_REFLECTIONS_GAIN,      reflections_gain);
    alEffectf(reverb_effect, AL_REVERB_LATE_REVERB_GAIN,      reverb_gain);
    alEffectf(reverb_effect, AL_REVERB_AIR_ABSORPTION_GAINHF, air_gain_hf);
    
    alEffectf(reverb_effect, AL_REVERB_REFLECTIONS_DELAY,     env_reflections_delay);
    alEffectf(reverb_effect, AL_REVERB_LATE_REVERB_DELAY,     env_reverb_delay);
    alEffectf(reverb_effect, AL_REVERB_ROOM_ROLLOFF_FACTOR,   env_room_rolloff_factor);

    alAuxiliaryEffectSloti(effect_slot, AL_EFFECTSLOT_EFFECT, reverb_effect);
}

void CSoundRender_CoreA::update_listener(const Fvector& P, const Fvector& D, const Fvector& N, float dt) {
    fTimeDelta = dt; 

    inherited::update_listener(P, D, N, dt);

    if (!Listener.position.similar(P)) {
        Listener.position.set(P);
        bListenerMoved = TRUE;
    }
    Listener.orientation[0].set(D.x, D.y, -D.z);
    Listener.orientation[1].set(N.x, N.y, -N.z);

    A_CHK(alListener3f(AL_POSITION, Listener.position.x, Listener.position.y, -Listener.position.z));
    A_CHK(alListener3f(AL_VELOCITY, 0.f, 0.f, 0.f));
    A_CHK(alListenerfv(AL_ORIENTATION, &Listener.orientation[0].x));
}