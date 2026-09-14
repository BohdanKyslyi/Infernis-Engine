#include "stdafx.h"
#pragma hdrstop

#include "soundrender_TargetA.h"
#include "soundrender_emitter.h"
#include "soundrender_source.h"

xr_vector<u8> g_target_temp_data;

CSoundRender_TargetA::CSoundRender_TargetA() : CSoundRender_Target() {
    cache_gain = 0.f;
    cache_pitch = 1.f;
    pSource = 0;
    ZeroMemory(pBuffers, sizeof(pBuffers));
    filter_lowpass = 0;
}

CSoundRender_TargetA::~CSoundRender_TargetA() {}

BOOL CSoundRender_TargetA::_initialize() {
    inherited::_initialize();
    A_CHK(alGenBuffers(sdef_target_count, pBuffers));
    alGenSources(1, &pSource);
    ALenum error = alGetError();
    if (AL_NO_ERROR == error) {
        A_CHK(alSourcei(pSource, AL_LOOPING, AL_FALSE));
        A_CHK(alSourcef(pSource, AL_MIN_GAIN, 0.f));
        A_CHK(alSourcef(pSource, AL_MAX_GAIN, 1.f));
        A_CHK(alSourcef(pSource, AL_GAIN, cache_gain));
        A_CHK(alSourcef(pSource, AL_PITCH, cache_pitch));
        
        // Filter objects are optional even when the device supports EFX.
        if (SoundRenderA->bEFX) {
            alGetError();
            alGenFilters(1, &filter_lowpass);
            alFilteri(filter_lowpass, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
            if (alGetError() != AL_NO_ERROR) {
                if (filter_lowpass) alDeleteFilters(1, &filter_lowpass);
                filter_lowpass = 0;
                alGetError();
                Msg("! [Noir Engine Audio] Low-pass filter unavailable for this source.");
            }
        }

        return TRUE;
    } else {
        Msg("! [Noir Engine] OpenAL: Can't create source. Error: %s.", (LPCSTR)alGetString(error));
        return FALSE;
    }
}

void CSoundRender_TargetA::_destroy() {
    if (alIsSource(pSource))
        alDeleteSources(1, &pSource);
    A_CHK(alDeleteBuffers(sdef_target_count, pBuffers));

    if (filter_lowpass)
        alDeleteFilters(1, &filter_lowpass);
    filter_lowpass = 0;
    pSource = 0;
    ZeroMemory(pBuffers, sizeof(pBuffers));
}

void CSoundRender_TargetA::_restart() {
    _destroy();
    _initialize();
}

void CSoundRender_TargetA::start(CSoundRender_Emitter* E) {
    inherited::start(E);
    buf_block = sdef_target_block * E->source()->m_wformat.nAvgBytesPerSec / 1000;
    g_target_temp_data.resize(buf_block);
}

void CSoundRender_TargetA::render() {
    for (u32 buf_idx = 0; buf_idx < sdef_target_count; buf_idx++)
        fill_block(pBuffers[buf_idx]);

    A_CHK(alSourceQueueBuffers(pSource, sdef_target_count, pBuffers));
    A_CHK(alSourcePlay(pSource));

    inherited::render();
}

void CSoundRender_TargetA::stop() {
    if (rendering) {
        A_CHK(alSourceStop(pSource));
        A_CHK(alSourcei(pSource, AL_BUFFER, NULL));
        A_CHK(alSourcei(pSource, AL_SOURCE_RELATIVE, TRUE));
    }
    inherited::stop();
}

void CSoundRender_TargetA::rewind() {
    inherited::rewind();
    A_CHK(alSourceStop(pSource));
    A_CHK(alSourcei(pSource, AL_BUFFER, NULL));
    for (u32 buf_idx = 0; buf_idx < sdef_target_count; buf_idx++)
        fill_block(pBuffers[buf_idx]);
    A_CHK(alSourceQueueBuffers(pSource, sdef_target_count, pBuffers));
    A_CHK(alSourcePlay(pSource));
}

void CSoundRender_TargetA::update() {
    inherited::update();

    ALint processed;
    A_CHK(alGetSourcei(pSource, AL_BUFFERS_PROCESSED, &processed));

    if (processed > 0) {
        while (processed) {
            ALuint BufferID;
            A_CHK(alSourceUnqueueBuffers(pSource, 1, &BufferID));
            fill_block(BufferID);
            A_CHK(alSourceQueueBuffers(pSource, 1, &BufferID));
            --processed;
        }
    }
    // Recover only after refilling processed buffers. Restarting from
    // fill_parameters() could replay the old queue after an underrun.
    ALint state = AL_INITIAL;
    ALint queued = 0;
    A_CHK(alGetSourcei(pSource, AL_SOURCE_STATE, &state));
    A_CHK(alGetSourcei(pSource, AL_BUFFERS_QUEUED, &queued));
    if (rendering && queued > 0 && state == AL_STOPPED)
        A_CHK(alSourcePlay(pSource));
}

void CSoundRender_TargetA::fill_parameters() {
    CSoundRender_Emitter* SE = m_pEmitter;
    VERIFY(SE);

    inherited::fill_parameters();

    A_CHK(alSourcef(pSource, AL_REFERENCE_DISTANCE, m_pEmitter->p_source.min_distance));
    A_CHK(alSourcef(pSource, AL_MAX_DISTANCE, m_pEmitter->p_source.max_distance));
    const bool spatial = !m_pEmitter->b2D;
    const Fvector& position = m_pEmitter->p_source.position;
    A_CHK(alSource3f(pSource, AL_POSITION, spatial ? position.x : 0.f,
        spatial ? position.y : 0.f, spatial ? -position.z : 0.f));
    A_CHK(alSourcei(pSource, AL_SOURCE_RELATIVE, spatial ? AL_FALSE : AL_TRUE));
    A_CHK(alSourcef(pSource, AL_ROLLOFF_FACTOR, spatial ? psSoundRolloff : 0.f));
    if (SoundRenderA->has_source_spatialize())
        A_CHK(alSourcei(pSource, AL_SOURCE_SPATIALIZE_SOFT, spatial ? AL_TRUE : AL_FALSE));

    float _gain = m_pEmitter->smooth_volume;
    clamp(_gain, EPS_S, 1.f);
    if (!fsimilar(_gain, cache_gain, 0.01f)) {
        cache_gain = _gain;
        A_CHK(alSourcef(pSource, AL_GAIN, _gain));
    }

    float _pitch = m_pEmitter->p_source.freq;
    clamp(_pitch, EPS_L, 2.f);
    if (!fsimilar(_pitch, cache_pitch)) {
        cache_pitch = _pitch;
        A_CHK(alSourcef(pSource, AL_PITCH, _pitch));
    }

    apply_effects();
}

void CSoundRender_TargetA::apply_effects() {
    if (!SoundRenderA->bEFX || !pSource) return;
    const bool music = m_pEmitter && m_pEmitter->owner_data &&
        m_pEmitter->owner_data->s_type == st_Music;
    const bool wet = SoundRenderA->efx_enabled() && m_pEmitter && !music;
    A_CHK(alSource3i(pSource, AL_AUXILIARY_SEND_FILTER,
        wet ? SoundRenderA->effect_slot : AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL));
    // Screen-relative sounds have no wall occlusion; retain reverb for HUD effects.
    if (wet && !m_pEmitter->b2D && filter_lowpass) {
        float occ = m_pEmitter->occluder_volume;
        clamp(occ, 0.05f, 1.0f);
        A_CHK(alFilterf(filter_lowpass, AL_LOWPASS_GAIN, 1.0f));
        A_CHK(alFilterf(filter_lowpass, AL_LOWPASS_GAINHF, occ));
        A_CHK(alSourcei(pSource, AL_DIRECT_FILTER, filter_lowpass));
    } else {
        A_CHK(alSourcei(pSource, AL_DIRECT_FILTER, AL_FILTER_NULL));
    }
}

void CSoundRender_TargetA::fill_block(ALuint BufferID) {
    R_ASSERT(m_pEmitter);

    m_pEmitter->fill_block(&g_target_temp_data.front(), buf_block);
    ALuint format = (m_pEmitter->source()->m_wformat.nChannels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    A_CHK(alBufferData(BufferID, format, &g_target_temp_data.front(), buf_block, m_pEmitter->source()->m_wformat.nSamplesPerSec));
}

void CSoundRender_TargetA::source_changed() {
    dettach();
    attach();
}