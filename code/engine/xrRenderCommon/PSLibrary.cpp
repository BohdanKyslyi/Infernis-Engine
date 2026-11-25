//----------------------------------------------------
// file: PSLibrary.cpp
//----------------------------------------------------
#include "stdafx.h"
#pragma hdrstop

#include "PSLibrary.h"
#include "ParticleEffect.h"
#include "ParticleGroup.h"

#ifdef _EDITOR
#include "ParticleEffectActions.h"
#include "../ECore/Editor/ui_main.h"
#endif

#define _game_data_ "$game_data$"
#if RENDER == R_R4
// Written by Ivan Danko, 2026-11-17: FPS plan step 2 - the DX11 console value particle_update_mod
// (xrRender_console.cpp), handed to the particle library at the start (psystem.h).
// Створено (Ivan Danko, 2026-11-17): крок 2 плану FPS - консольне значення DX11 particle_update_mod
// (xrRender_console.cpp), яке передається бібліотеці частинок під час старту (psystem.h).
extern float ps_particle_update_coeff;
#endif

bool ped_sort_pred(const PS::CPEDef* a, const PS::CPEDef* b) {
    return xr_strcmp(a->Name(), b->Name()) < 0;
}
bool pgd_sort_pred(const PS::CPGDef* a, const PS::CPGDef* b) {
    return xr_strcmp(a->m_Name, b->m_Name) < 0;
}

bool ped_find_pred(const PS::CPEDef* a, LPCSTR b) { return xr_strcmp(a->Name(), b) < 0; }
bool pgd_find_pred(const PS::CPGDef* a, LPCSTR b) { return xr_strcmp(a->m_Name, b) < 0; }
//----------------------------------------------------
void CPSLibrary::OnCreate() {
#ifdef _EDITOR
    if (pCreateEAction) {
        Load2();
    } else
#endif
    {
        string_path fn;
        FS.update_path(fn, _game_data_, "particles.xr");
#if RENDER == R_R4
        // Edited by Ivan Danko, 2026-11-17: FPS plan step 2 - the particles of Anomaly: the Anomaly
        // behaviour of the particle library is switched on (only DX11 does it) with the update
        // coefficient particle_update_mod, and Load reads the loose .pe / .pg files first and then
        // particles.xr, as Monolith's OnCreate (Load2 is not called separately any more).
        // Змінено (Ivan Danko, 2026-11-17): крок 2 плану FPS - частинки Anomaly: вмикається поведінка
        // Anomaly бібліотеки частинок (це робить лише DX11) з коефіцієнтом оновлення
        // particle_update_mod, а Load читає спершу окремі файли .pe / .pg, потім particles.xr, як
        // OnCreate у Monolith (окремо Load2 більше не викликається).
        PAPI::SetAnomalyMode(true);
        PAPI::SetUpdateCoeff(&ps_particle_update_coeff);
        Load(fn);
#else
        Load(fn);
        Load2();
#endif
    }
}

void CPSLibrary::OnDestroy() {
    for (PS::PEDIt e_it = m_PEDs.begin(); e_it != m_PEDs.end(); e_it++)
        (*e_it)->DestroyShader();

    for (auto e_it = m_PEDs.begin(); e_it != m_PEDs.end(); e_it++)
        xr_delete(*e_it);
    m_PEDs.clear();

    for (PS::PGDIt g_it = m_PGDs.begin(); g_it != m_PGDs.end(); g_it++)
        xr_delete(*g_it);
    m_PGDs.clear();
}
//----------------------------------------------------
PS::PEDIt CPSLibrary::FindPEDIt(LPCSTR Name) {
    if (!Name)
        return m_PEDs.end();
#ifdef _EDITOR
    for (PS::PEDIt it = m_PEDs.begin(); it != m_PEDs.end(); it++)
        if (0 == xr_strcmp((*it)->Name(), Name))
            return it;
    return m_PEDs.end();
#else
    PS::PEDIt I = std::lower_bound(m_PEDs.begin(), m_PEDs.end(), Name, ped_find_pred);
    if (I == m_PEDs.end() || (0 != xr_strcmp((*I)->m_Name, Name)))
        return m_PEDs.end();
    else
        return I;
#endif
}

PS::CPEDef* CPSLibrary::FindPED(LPCSTR Name) {
    PS::PEDIt it = FindPEDIt(Name);
    return (it == m_PEDs.end()) ? 0 : *it;
}

PS::PGDIt CPSLibrary::FindPGDIt(LPCSTR Name) {
    if (!Name)
        return m_PGDs.end();
#ifdef _EDITOR
    for (PS::PGDIt it = m_PGDs.begin(); it != m_PGDs.end(); it++)
        if (0 == xr_strcmp((*it)->m_Name, Name))
            return it;
    return m_PGDs.end();
#else
    PS::PGDIt I = std::lower_bound(m_PGDs.begin(), m_PGDs.end(), Name, pgd_find_pred);
    if (I == m_PGDs.end() || (0 != xr_strcmp((*I)->m_Name, Name)))
        return m_PGDs.end();
    else
        return I;
#endif
}

PS::CPGDef* CPSLibrary::FindPGD(LPCSTR Name) {
    PS::PGDIt it = FindPGDIt(Name);
    return (it == m_PGDs.end()) ? 0 : *it;
}

void CPSLibrary::RenamePED(PS::CPEDef* src, LPCSTR new_name) {
    R_ASSERT(src && new_name && new_name[0]);
    src->SetName(new_name);
}

void CPSLibrary::RenamePGD(PS::CPGDef* src, LPCSTR new_name) {
    R_ASSERT(src && new_name && new_name[0]);
    src->SetName(new_name);
}

void CPSLibrary::Remove(const char* nm) {
    PS::PEDIt it = FindPEDIt(nm);
    if (it != m_PEDs.end()) {
        (*it)->DestroyShader();
        xr_delete(*it);
        m_PEDs.erase(it);
    } else {
        PS::PGDIt it = FindPGDIt(nm);
        if (it != m_PGDs.end()) {
            xr_delete(*it);
            m_PGDs.erase(it);
        }
    }
}
//----------------------------------------------------
bool CPSLibrary::Load2() {
    FS_FileSet files;
    string_path _path;
    FS.update_path(_path, "$game_particles$", "");

    FS.file_list(files, _path, FS_ListFiles, "*.pe,*.pg");

#ifdef _EDITOR
    SPBItem* pb = NULL;
    if (UI->m_bReady)
        pb = UI->ProgressStart(files.size(), "Loading particles...");
#endif
    FS_FileSet::iterator it = files.begin();
    FS_FileSet::iterator it_e = files.end();

    string_path p_path, p_name, p_ext;
    for (; it != it_e; ++it) {
        const FS_File& f = (*it);
        _splitpath(f.name.c_str(), 0, p_path, p_name, p_ext);
        FS.update_path(_path, "$game_particles$", f.name.c_str());
        CInifile ini(_path, TRUE, TRUE, FALSE);

#ifdef _EDITOR
        if (pb)
            pb->Inc();
#endif

        xr_sprintf(_path, sizeof(_path), "%s%s", p_path, p_name);
        if (0 == stricmp(p_ext, ".pe")) {
            PS::CPEDef* def = xr_new<PS::CPEDef>();
            def->m_Name = _path;
            if (def->Load2(ini))
                m_PEDs.push_back(def);
            else
                xr_delete(def);
        } else if (0 == stricmp(p_ext, ".pg")) {
            PS::CPGDef* def = xr_new<PS::CPGDef>();
            def->m_Name = _path;
            if (def->Load2(ini))
                m_PGDs.push_back(def);
            else
                xr_delete(def);
        } else {
            R_ASSERT(0);
        }
    }

    std::sort(m_PEDs.begin(), m_PEDs.end(), ped_sort_pred);
    std::sort(m_PGDs.begin(), m_PGDs.end(), pgd_sort_pred);

    for (PS::PEDIt e_it = m_PEDs.begin(); e_it != m_PEDs.end(); e_it++)
        (*e_it)->CreateShader();

#ifdef _EDITOR
    if (pb)
        UI->ProgressEnd(pb);
#endif
    Msg("Loaded particles :%d", files.size());
    return true;
}

#if RENDER == R_R4
// Ported by Ivan Danko, 2026-11-17, from Monolith (author themrdemonized): FPS plan step 2 - Monolith's CPSLibrary::Load: the loose .pe / .pg
// files ($game_particles$, with their actions) are read first, then particles.xr; an effect or a group
// of particles.xr with the name of a loose one is skipped, so the loose files win.
// Перенесення з Monolith (автор themrdemonized) - Ivan Danko, 2026-11-17: крок 2 плану FPS - CPSLibrary::Load з Monolith: спершу
// читаються окремі файли .pe / .pg ($game_particles$, з їхніми діями), потім particles.xr; ефект або
// група з particles.xr з іменем окремого файлу пропускається, тож окремі файли важливіші.
bool CPSLibrary::Load(const char* nm) {
    FS_FileSet files;
    string_path _path;

    FS.update_path(_path, "$game_particles$", "");
    FS.file_list(files, _path, FS_ListFiles, "*.pe,*.pg");

    FS_FileSet::iterator it = files.begin();
    FS_FileSet::iterator it_e = files.end();

    string_path p_path, p_name, p_ext;
    for (; it != it_e; ++it) {
        const FS_File& f = (*it);
        _splitpath(f.name.c_str(), 0, p_path, p_name, p_ext);
        FS.update_path(_path, "$game_particles$", f.name.c_str());
        CInifile ini(_path, TRUE, TRUE, FALSE);

        xr_sprintf(_path, sizeof(_path), "%s%s", p_path, p_name);
        if (0 == stricmp(p_ext, ".pe")) {
            PS::CPEDef* def = xr_new<PS::CPEDef>();
            def->m_Name = _path;
            if (def->Load2(ini))
                m_PEDs.push_back(def);
            else
                xr_delete(def);
        } else if (0 == stricmp(p_ext, ".pg")) {
            PS::CPGDef* def = xr_new<PS::CPGDef>();
            def->m_Name = _path;
            if (def->Load2(ini))
                m_PGDs.push_back(def);
            else
                xr_delete(def);
        } else {
            R_ASSERT(0);
        }
    }

    bool bRes = true;
    if (FS.exist(nm)) {
        IReader* F = FS.r_open(nm);
        R_ASSERT(F->find_chunk(PS_CHUNK_VERSION));
        u16 ver = F->r_u16();
        if (ver != PS_VERSION)
            return false;
        // second generation
        IReader* OBJ;
        OBJ = F->open_chunk(PS_CHUNK_SECONDGEN);
        if (OBJ) {
            IReader* O = OBJ->open_chunk(0);
            for (int count = 1; O; count++) {
                PS::CPEDef* def = xr_new<PS::CPEDef>();
                if (def->Load(*O)) {
                    bool exist = false;
                    for (PS::CPEDef* pdef : m_PEDs) {
                        if (pdef->m_Name == def->m_Name) {
                            exist = true;
                            xr_delete(def);
                            break;
                        }
                    }

                    if (!exist)
                        m_PEDs.push_back(def);
                } else {
                    bRes = false;
                    xr_delete(def);
                }
                O->close();
                if (!bRes)
                    break;
                O = OBJ->open_chunk(count);
            }
            OBJ->close();
        }
        // second generation
        OBJ = F->open_chunk(PS_CHUNK_THIRDGEN);
        if (OBJ) {
            IReader* O = OBJ->open_chunk(0);
            for (int count = 1; O; count++) {
                PS::CPGDef* def = xr_new<PS::CPGDef>();
                if (def->Load(*O)) {
                    bool exist = false;
                    for (PS::CPGDef* pdef : m_PGDs) {
                        if (pdef->m_Name == def->m_Name) {
                            exist = true;
                            xr_delete(def);
                            break;
                        }
                    }

                    if (!exist)
                        m_PGDs.push_back(def);
                } else {
                    bRes = false;
                    xr_delete(def);
                }
                O->close();
                if (!bRes)
                    break;
                O = OBJ->open_chunk(count);
            }
            OBJ->close();
        }

        FS.r_close(F);
    }

    std::sort(m_PEDs.begin(), m_PEDs.end(), ped_sort_pred);
    std::sort(m_PGDs.begin(), m_PGDs.end(), pgd_sort_pred);

    for (PS::PEDIt e_it = m_PEDs.begin(); e_it != m_PEDs.end(); ++e_it)
        (*e_it)->CreateShader();

    // Written by Ivan Danko, 2026-11-17: FPS plan step 2 - one line in the log for the check: how many
    // loose files were read and how many effects and groups there are in all.
    // Створено (Ivan Danko, 2026-11-17): крок 2 плану FPS - один рядок у журналі для перевірки: скільки
    // окремих файлів прочитано і скільки всього ефектів і груп.
    Msg("* Particles: %u loose files, %u effects and %u groups in all", u32(files.size()),
        u32(m_PEDs.size()), u32(m_PGDs.size()));

    return bRes;
}
#else
bool CPSLibrary::Load(const char* nm) {
    if (!FS.exist(nm)) {
        Msg("Can't find file: '%s'", nm);
        return false;
    }

    IReader* F = FS.r_open(nm);
    bool bRes = true;
    R_ASSERT(F->find_chunk(PS_CHUNK_VERSION));
    u16 ver = F->r_u16();
    if (ver != PS_VERSION)
        return false;
    // second generation
    IReader* OBJ;
    OBJ = F->open_chunk(PS_CHUNK_SECONDGEN);
    if (OBJ) {
        IReader* O = OBJ->open_chunk(0);
        for (int count = 1; O; count++) {
            PS::CPEDef* def = xr_new<PS::CPEDef>();
            if (def->Load(*O))
                m_PEDs.push_back(def);
            else {
                bRes = false;
                xr_delete(def);
            }
            O->close();
            if (!bRes)
                break;
            O = OBJ->open_chunk(count);
        }
        OBJ->close();
    }
    // second generation
    OBJ = F->open_chunk(PS_CHUNK_THIRDGEN);
    if (OBJ) {
        IReader* O = OBJ->open_chunk(0);
        for (int count = 1; O; count++) {
            PS::CPGDef* def = xr_new<PS::CPGDef>();
            if (def->Load(*O))
                m_PGDs.push_back(def);
            else {
                bRes = false;
                xr_delete(def);
            }
            O->close();
            if (!bRes)
                break;
            O = OBJ->open_chunk(count);
        }
        OBJ->close();
    }

    // final
    FS.r_close(F);

    std::sort(m_PEDs.begin(), m_PEDs.end(), ped_sort_pred);
    std::sort(m_PGDs.begin(), m_PGDs.end(), pgd_sort_pred);

    for (PS::PEDIt e_it = m_PEDs.begin(); e_it != m_PEDs.end(); e_it++)
        (*e_it)->CreateShader();

    return bRes;
}
#endif
//----------------------------------------------------
void CPSLibrary::Reload() {
    OnDestroy();
    OnCreate();
    Msg("PS Library was succesfully reloaded.");
}
//----------------------------------------------------

using PS::CPGDef;

CPGDef const* const* CPSLibrary::particles_group_begin() const {
    return (m_PGDs.size() ? &*m_PGDs.begin() : 0);
}

CPGDef const* const* CPSLibrary::particles_group_end() const {
    return (m_PGDs.size() ? &*m_PGDs.end() : 0);
}

void CPSLibrary::particles_group_next(PS::CPGDef const* const*& iterator) const {
    VERIFY(iterator);
    VERIFY(iterator >= particles_group_begin());
    VERIFY(iterator < particles_group_end());
    ++iterator;
}

shared_str const& CPSLibrary::particles_group_id(CPGDef const& particles_group) const {
    return (particles_group.m_Name);
}
#if RENDER == R_R4

//------------------------------------------------------------------------------
// Ported by Ivan Danko, 2026-11-17, from Monolith (author themrdemonized): FPS plan step 2 - Monolith's CPSLibrary::Save / Save2 for the DX11
// console commands part_export / part_import (xrRender_console.cpp). Save2 empties the folder of loose
// particle files of gamedata ($game_particles$; files of the addons stay on the disk) and writes every
// effect and group there as .pe / .pg; Save writes gamedata\particles.xr.
// Перенесення з Monolith (автор themrdemonized) - Ivan Danko, 2026-11-17: крок 2 плану FPS - CPSLibrary::Save / Save2 з Monolith для
// консольних команд DX11 part_export / part_import (xrRender_console.cpp). Save2 очищає теку окремих
// файлів частинок gamedata ($game_particles$; файли аддонів лишаються на диску) і записує туди кожен
// ефект і групу як .pe / .pg; Save записує gamedata\particles.xr.
bool CPSLibrary::Save() {
    string_path fn;
    FS.update_path(fn, _game_data_, "particles.xr");
    Save(fn);
    return true;
}

bool CPSLibrary::Save2() {
    FS.dir_delete("$game_particles$", "", TRUE);
    string_path fn;
    for (PS::PEDIt it = m_PEDs.begin(); it != m_PEDs.end(); ++it) {
        PS::CPEDef* pe = (*it);
        FS.update_path(fn, "$game_particles$", pe->m_Name.c_str());
        strcat(fn, ".pe");
        CInifile ini(fn, FALSE, FALSE, FALSE);
        pe->Save2(ini);
        ini.save_as(fn);
    }

    for (PS::PGDIt g_it = m_PGDs.begin(); g_it != m_PGDs.end(); ++g_it) {
        PS::CPGDef* pg = (*g_it);
        FS.update_path(fn, "$game_particles$", pg->m_Name.c_str());
        strcat(fn, ".pg");
        CInifile ini(fn, FALSE, FALSE, FALSE);
        pg->Save2(ini);
        ini.save_as(fn);
    }
    return true;
}

bool CPSLibrary::Save(const char* nm) {
    CMemoryWriter F;

    F.open_chunk(PS_CHUNK_VERSION);
    F.w_u16(PS_VERSION);
    F.close_chunk();

    F.open_chunk(PS_CHUNK_SECONDGEN);
    u32 chunk_id = 0;
    for (PS::PEDIt it = m_PEDs.begin(); it != m_PEDs.end(); ++it, ++chunk_id) {
        F.open_chunk(chunk_id);
        (*it)->Save(F);
        F.close_chunk();
    }
    F.close_chunk();

    F.open_chunk(PS_CHUNK_THIRDGEN);
    chunk_id = 0;
    for (PS::PGDIt g_it = m_PGDs.begin(); g_it != m_PGDs.end(); ++g_it, ++chunk_id) {
        F.open_chunk(chunk_id);
        (*g_it)->Save(F);
        F.close_chunk();
    }
    F.close_chunk();

    return F.save_to(nm);
}
#endif
