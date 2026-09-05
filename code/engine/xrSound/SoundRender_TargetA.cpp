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
        
        // === Генерація Low-Pass фільтру ===
        A_CHK(alGenFilters(1, &filter_lowpass));
        A_CHK(alFilteri(filter_lowpass, AL_FILTER_TYPE, AL_FILTER_LOWPASS));
        // ==========================================

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

    // Видалення фільтру з пам'яті 
    if (alIsFilter(filter_lowpass)) {
        alDeleteFilters(1, &filter_lowpass);
    }
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
    } else {
        ALint state;
        A_CHK(alGetSourcei(pSource, AL_SOURCE_STATE, &state));
        if (state != AL_PLAYING) {
            A_CHK(alSourcePlay(pSource));
        }
    }
}

void CSoundRender_TargetA::fill_parameters() {
    CSoundRender_Emitter* SE = m_pEmitter;
    VERIFY(SE);

    inherited::fill_parameters();

    A_CHK(alSourcef(pSource, AL_REFERENCE_DISTANCE, m_pEmitter->p_source.min_distance));
    A_CHK(alSourcef(pSource, AL_MAX_DISTANCE, m_pEmitter->p_source.max_distance));
    A_CHK(alSource3f(pSource, AL_POSITION, m_pEmitter->p_source.position.x, m_pEmitter->p_source.position.y, -m_pEmitter->p_source.position.z));
    A_CHK(alSourcei(pSource, AL_SOURCE_RELATIVE, m_pEmitter->b2D));
    A_CHK(alSourcef(pSource, AL_ROLLOFF_FACTOR, psSoundRolloff));

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

    // ФІКС ЗНИКНЕННЯ ЗВУКУ ПІСЛЯ ФРІЗІВ (BUFFER UNDERRUN)
    ALint state;
    alGetSourcei(pSource, AL_SOURCE_STATE, &state);
    
    if (state == AL_STOPPED) 
    {
        alSourcePlay(pSource);
    }
 
	// EFX РЕФАКТОРИНГ: Підключення джерела до слоту реверберації та фільтрів 
    if (SoundRenderA->bEFX) {
        // Дістаємо тип звуку: Музика чи Ефекти (зброя, кроки тощо)
        esound_type soundType = m_pEmitter->owner_data->s_type;

        // Вимикаємо луну ТІЛЬКИ якщо це 2D звук І це фонова музика
        if (m_pEmitter->b2D && soundType == st_Music) {
            // Музику і UI звуки не пропускаємо через реверберацію Зони
            A_CHK(alSource3i(pSource, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL));
            // Також вимикаємо будь-які Low-Pass фільтри для 2D звуків
            A_CHK(alSourcei(pSource, AL_DIRECT_FILTER, AL_FILTER_NULL));
        } else {
            // 3D звуки АБО 2D-ефекти (зброя в руках актора) підключаємо до слоту луни
            A_CHK(alSource3i(pSource, AL_AUXILIARY_SEND_FILTER, SoundRenderA->effect_slot, 0, AL_FILTER_NULL));

            // ДИНАМІЧНИЙ LOW-PASS FILTER (ОКЛЮЗІЯ ЗА СТІНАМИ)
            float occ = m_pEmitter->occluder_volume; 
            clamp(occ, 0.05f, 1.0f); // 0.05 - щоб звук за дуже товстою стіною не зникав повністю

            A_CHK(alFilterf(filter_lowpass, AL_LOWPASS_GAIN, 1.0f)); // Загальну гучність рушій і так ріже сам
            A_CHK(alFilterf(filter_lowpass, AL_LOWPASS_GAINHF, occ)); // Чим менше occ (товща стіна), тим більше зрізаються дзвінкі частоти

            // Застосовуємо налаштований фільтр до джерела звуку
            A_CHK(alSourcei(pSource, AL_DIRECT_FILTER, filter_lowpass));
        }
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