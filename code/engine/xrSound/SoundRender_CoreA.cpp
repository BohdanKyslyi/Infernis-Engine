#include "stdafx.h"
#pragma hdrstop

#include "soundrender_coreA.h"
#include "soundrender_targetA.h"

CSoundRender_CoreA* SoundRenderA = 0;

__declspec(dllexport) u32 snd_device_id = 0;
__declspec(dllexport) xr_token* snd_devices_token = NULL;
__declspec(dllexport) int snd_hrtf = 1;
__declspec(dllexport) u32 snd_output = SoundDevice::Auto;

static void free_device_tokens() {
    if (!snd_devices_token) return;
    for (xr_token* token = snd_devices_token; token->name; ++token)
        xr_free(token->name);
    xr_free(snd_devices_token);
}

__declspec(dllexport) void snd_refresh_devices() {
    if (SoundRenderA) SoundRenderA->refresh_devices();
}

__declspec(dllexport) void snd_get_status(char* text, u32 size) {
    if (SoundRenderA) SoundRenderA->get_status(text, size);
    else xr_strcpy(text, size, "Audio unavailable");
}

CSoundRender_CoreA::CSoundRender_CoreA() : CSoundRender_Core() {
    pDevice = 0;
    pContext = 0;
    active_efx = false;
    apply_failed = false;
    source_spatialize = false;
    Listener.position.set(0.f, 0.f, 0.f);
    Listener.orientation[0].set(0.f, 0.f, -1.f);
    Listener.orientation[1].set(0.f, 1.f, 0.f);
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

CSoundRender_CoreA::~CSoundRender_CoreA() {
    free_device_tokens();
    SoundRenderA = nullptr;
}

SoundDevice::Settings CSoundRender_CoreA::requested_settings() const {
    SoundDevice::Settings settings;
    settings.hrtf = snd_hrtf;
    settings.output = snd_output;
    if (snd_devices_token && snd_device_id != 0) {
        for (const xr_token* token = snd_devices_token; token->name; ++token) {
            if (token->id == (int)snd_device_id) {
                settings.device = token->name;
                break;
            }
        }
    }
    return settings;
}

void CSoundRender_CoreA::refresh_devices() {
    std::lock_guard<std::recursive_mutex> lock(runtime_mutex);
    apply_failed = false;
    const auto selected = requested_settings().device;
    xr_vector<xr_token> tokens;
    tokens.push_back({ xr_strdup("Default"), 0 });
    const ALCchar* devices = nullptr;
    if (SoundDevice::HasExtension(nullptr, "ALC_ENUMERATE_ALL_EXT"))
        devices = alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER);
    else if (SoundDevice::HasExtension(nullptr, "ALC_ENUMERATION_EXT"))
        devices = alcGetString(nullptr, ALC_DEVICE_SPECIFIER);
    while (devices && *devices) {
        bool duplicate = false;
        for (const auto& token : tokens)
            if (xr_strcmp(token.name, devices) == 0) duplicate = true;
        if (!duplicate)
            tokens.push_back({ xr_strdup(devices), (int)tokens.size() });
        devices += xr_strlen(devices) + 1;
    }
    // Keep the selected/active endpoint addressable even after unplugging it.
    // This also keeps Cancel and a failed switch from selecting another device.
    for (const auto& name : { selected, active_settings.device }) {
        if (name.empty()) continue;
        bool found = false;
        for (const auto& token : tokens)
            if (name == token.name) found = true;
        if (!found)
            tokens.push_back({ xr_strdup(name.c_str()), (int)tokens.size() });
    }
    free_device_tokens();
    tokens.push_back({ nullptr, -1 });
    snd_devices_token = xr_alloc<xr_token>(tokens.size());
    snd_device_id = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        snd_devices_token[i] = tokens[i];
        if (tokens[i].name && selected == tokens[i].name)
            snd_device_id = tokens[i].id;
    }
}

void CSoundRender_CoreA::get_status(char* text, u32 size) {
    std::lock_guard<std::recursive_mutex> lock(runtime_mutex);
    if (!pDevice || !pContext || !bReady) {
        xr_strcpy(text, size, "Audio unavailable");
        return;
    }
    ALCint hrtf = ALC_FALSE;
    ALCint output = ALC_ANY_SOFT;
    if (SoundDevice::HasExtension(pDevice, "ALC_SOFT_HRTF"))
        alcGetIntegerv(pDevice, ALC_HRTF_SOFT, 1, &hrtf);
    if (SoundDevice::HasExtension(pDevice, "ALC_SOFT_output_mode"))
        alcGetIntegerv(pDevice, ALC_OUTPUT_MODE_SOFT, 1, &output);
    xr_sprintf(text, size, "EFX: %s | HRTF: %s | %s%s",
        efx_enabled() ? "on" : "off", hrtf ? "on" : "off",
        SoundDevice::OutputName(output), apply_failed ? " | APPLY FAILED" : "");
}

void CSoundRender_CoreA::log_device_status() {
    string256 status;
    get_status(status, sizeof(status));
    const char* name = alcGetString(pDevice, SoundDevice::HasExtension(pDevice, "ALC_ENUMERATE_ALL_EXT")
        ? ALC_ALL_DEVICES_SPECIFIER : ALC_DEVICE_SPECIFIER);
    Msg("* [Noir Engine Audio] Device: %s; %s", name ? name : "Default", status);
    if (SoundDevice::HasExtension(pDevice, "ALC_SOFT_HRTF")) {
        ALCint state = ALC_HRTF_DISABLED_SOFT;
        alcGetIntegerv(pDevice, ALC_HRTF_STATUS_SOFT, 1, &state);
        Msg("* [Noir Engine Audio] HRTF status: %d (requested: %d)", state,
            SoundDevice::WantsHRTF(active_settings) ? 1 : 0);
    }
}

void CSoundRender_CoreA::_restart() {
    // Serialize with mtSound. The UI calls this once after saving the whole group.
    std::lock_guard<std::recursive_mutex> lock(runtime_mutex);
    if (!pDevice || !pContext || !bReady) return;
    const auto requested = requested_settings();
    const auto result = SoundDevice::Apply(pDevice, active_settings, requested);
    apply_failed = result == SoundDevice::ApplyResult::Failed ||
                   result == SoundDevice::ApplyResult::Unsupported;
    if (apply_failed) {
        Msg("! [Noir Engine Audio] Could not apply audio settings (%s, ALC error 0x%x). Previous selection restored.",
            result == SoundDevice::ApplyResult::Unsupported ? "OpenAL extension unavailable" : "device error",
            alcGetError(pDevice));
        snd_hrtf = active_settings.hrtf;
        snd_output = active_settings.output;
        snd_device_id = 0;
        for (const xr_token* token = snd_devices_token; token && token->name; ++token)
            if (active_settings.device == token->name) snd_device_id = token->id;
    } else {
        active_settings = requested;
    }
    // EFX can be changed independently even if an endpoint change failed.
    active_efx = psSoundFlags.test(ss_EFX) != 0;
    bEFX_Initialized = false;
    inherited::_restart();
    update_environment(&e_current);
    for (auto* target : s_targets)
        static_cast<CSoundRender_TargetA*>(target)->apply_effects();
    log_device_status();
}

void CSoundRender_CoreA::update(const Fvector& P, const Fvector& D, const Fvector& N) {
    std::lock_guard<std::recursive_mutex> lock(runtime_mutex);
    inherited::update(P, D, N);
}

void CSoundRender_CoreA::_initialize(int stage) {
    if (stage == 0) {
        refresh_devices();
        return;
    }
    active_settings = requested_settings();
    LPCSTR device_to_open = active_settings.device.empty() ? nullptr : active_settings.device.c_str();

    pDevice = alcOpenDevice(device_to_open); 
    if (pDevice == NULL) {
        Msg("! [Noir Engine] OpenAL: Failed to open device '%s'. Falling back to default.", device_to_open ? device_to_open : "Default");
        pDevice = alcOpenDevice(nullptr);
        active_settings.device.clear();
        snd_device_id = 0;
        if (pDevice == NULL) {
            CHECK_OR_EXIT(0, "! [Noir Engine] OpenAL: Failed to create device.");
            bPresent = FALSE;
            return;
        }
    }

    const auto contextAttr = SoundDevice::Attributes(pDevice, active_settings);
    pContext = alcCreateContext(pDevice, contextAttr.data());
    if (0 == pContext) {
        Msg("! [Noir Engine Audio] Requested output unavailable; trying default context attributes.");
        active_settings.hrtf = snd_hrtf = 0;
        active_settings.output = snd_output = SoundDevice::Auto;
        const auto fallbackAttr = SoundDevice::Attributes(pDevice, active_settings);
        pContext = alcCreateContext(pDevice, fallbackAttr.data());
        if (0 == pContext) {
            CHECK_OR_EXIT(0, "! [Noir Engine] OpenAL: Failed to create context.");
            bPresent = FALSE;
            alcCloseDevice(pDevice);
            pDevice = 0;
            return;
        }
    }

    if (!alcMakeContextCurrent(pContext)) {
        alcDestroyContext(pContext);
        pContext = nullptr;
        alcCloseDevice(pDevice);
        pDevice = nullptr;
        bPresent = FALSE;
        CHECK_OR_EXIT(0, "! [Noir Engine Audio] Failed to activate OpenAL context.");
        return;
    }
    alGetError();
    alcGetError(pDevice);
    source_spatialize = alIsExtensionPresent("AL_SOFT_source_spatialize") == AL_TRUE;

    alDistanceModel(AL_EXPONENT_DISTANCE_CLAMPED);
    
    A_CHK(alListener3f(AL_POSITION, 0.f, 0.f, 0.f));
    A_CHK(alListener3f(AL_VELOCITY, 0.f, 0.f, 0.f));
    Fvector orient[2] = { { 0.f, 0.f, -1.f }, { 0.f, 1.f, 0.f } };
    A_CHK(alListenerfv(AL_ORIENTATION, &orient[0].x));
    A_CHK(alListenerf(AL_GAIN, 1.f));

    bEFX = FALSE;
    bEFX_Initialized = false;
    active_efx = psSoundFlags.test(ss_EFX) != 0;
    if (SoundDevice::HasExtension(pDevice, "ALC_EXT_EFX")) {
        alGetError();
        alGenAuxiliaryEffectSlots(1, &effect_slot);
        alGenEffects(1, &reverb_effect);
        alEffecti(reverb_effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
        bEFX = alGetError() == AL_NO_ERROR && effect_slot && reverb_effect;
        if (!bEFX) {
            if (effect_slot) alDeleteAuxiliaryEffectSlots(1, &effect_slot);
            if (reverb_effect) alDeleteEffects(1, &reverb_effect);
            effect_slot = reverb_effect = 0;
            alGetError();
            Msg("! [Noir Engine Audio] EFX initialization failed; continuing with dry audio.");
        }
    }

    inherited::_initialize(stage);
    update_environment(&e_current);
    log_device_status();

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
    std::lock_guard<std::recursive_mutex> lock(runtime_mutex);
    // Release emitter/decoder references before the base destroys the sources.
    stop_emitters();
    inherited::_clear();
    for (auto* target : s_targets) {
        target->_destroy();
        xr_delete(target);
    }
    s_targets.clear();
    s_targets_defer.clear();
    if (effect_slot) alDeleteAuxiliaryEffectSlots(1, &effect_slot);
    if (reverb_effect) alDeleteEffects(1, &reverb_effect);
    effect_slot = reverb_effect = 0;
    bEFX = FALSE;
    bPresent = FALSE;
    active_efx = false;
    alcMakeContextCurrent(nullptr);
    if (pContext) alcDestroyContext(pContext);
    pContext = nullptr;
    if (pDevice) alcCloseDevice(pDevice);
    pDevice = nullptr;
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
    
    if (!efx_enabled()) {
        // Mute the wet bus immediately, including the existing reverb tail.
        alAuxiliaryEffectSlotf(effect_slot, AL_EFFECTSLOT_GAIN, 0.0f);
        alAuxiliaryEffectSloti(effect_slot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL);
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
    alAuxiliaryEffectSlotf(effect_slot, AL_EFFECTSLOT_GAIN, 1.0f);
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