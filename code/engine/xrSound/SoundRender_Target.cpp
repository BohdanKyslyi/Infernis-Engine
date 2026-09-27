#include "stdafx.h"
#pragma hdrstop

#include "soundrender_target.h"
#include "soundrender_core.h"
#include "soundrender_emitter.h"
#include "soundrender_source.h"

CSoundRender_Target::CSoundRender_Target(void) {
    m_pEmitter = 0;
    rendering = FALSE;
    wave = 0;
    warned_missing_wave = false;
}

CSoundRender_Target::~CSoundRender_Target(void) { VERIFY(wave == 0); }

BOOL CSoundRender_Target::_initialize() {
    return TRUE; // Застарілий код ініціалізації PCM вирізано
}

void CSoundRender_Target::start(CSoundRender_Emitter* E) {
    R_ASSERT(E);
    m_pEmitter = E;
    rendering = FALSE;
    warned_missing_wave = false;
}

void CSoundRender_Target::render() { rendering = TRUE; }

void CSoundRender_Target::stop() {
    dettach();
    m_pEmitter = NULL;
    rendering = FALSE;
}

void CSoundRender_Target::rewind() { R_ASSERT(rendering); }

void CSoundRender_Target::update() { R_ASSERT(m_pEmitter); }

void CSoundRender_Target::fill_parameters() {
    VERIFY(m_pEmitter);
}

extern int ov_seek_func(void* datasource, s64 offset, int whence);
extern size_t ov_read_func(void* ptr, size_t size, size_t nmemb, void* datasource);
extern int ov_close_func(void* datasource);
extern long ov_tell_func(void* datasource);

bool CSoundRender_Target::attach() {
    VERIFY(0 == wave);
    VERIFY(m_pEmitter);
    ov_callbacks ovc = { ov_read_func, ov_seek_func, ov_close_func, ov_tell_func };
    wave = FS.r_open(m_pEmitter->source()->pname.c_str());
    if (!wave || !wave->length()) {
        if (!warned_missing_wave)
            Msg("! [Sound] Cannot open wave file during playback: %s", m_pEmitter->source()->pname.c_str());
        warned_missing_wave = true;
        if (wave) FS.r_close(wave);
        wave = nullptr;
        return false;
    }
    const int result = ov_open_callbacks(wave, &ovf, NULL, 0, ovc);
    if (result != 0) {
        if (!warned_missing_wave)
            Msg("! [Sound] Invalid wave file during playback: %s (Vorbis: %d)",
                m_pEmitter->source()->pname.c_str(), result);
        warned_missing_wave = true;
        FS.r_close(wave);
        wave = nullptr;
        return false;
    }
    warned_missing_wave = false;
    return true;
}

void CSoundRender_Target::dettach() {
    if (wave) {
        ov_clear(&ovf);
        FS.r_close(wave);
        wave = 0; // ВАЖЛИВО: Виправлення крашу. В оригіналі цього рядка не було!
    }
}
