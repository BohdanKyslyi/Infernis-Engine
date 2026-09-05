#include "stdafx.h"
#pragma once

#ifndef _EDITOR
#include "render.h"
#endif
#include "Thunderbolt.h"
#include "igame_persistent.h"
#include "LightAnimLibrary.h"

#ifdef _EDITOR
#include "ui_toolscustom.h"
#else
#include "igame_level.h"
#include "../xrcdb/xr_area.h"
#include "xr_object.h"
#endif

#include <cmath>
#include <algorithm>

SThunderboltDesc::~SThunderboltDesc() {
    m_pRender->DestroyModel();
    if (m_GradientTop) m_GradientTop->m_pFlare->DestroyShader();
    if (m_GradientCenter) m_GradientCenter->m_pFlare->DestroyShader();
    snd.destroy();

    xr_delete(m_GradientTop);
    xr_delete(m_GradientCenter);
}

void SThunderboltDesc::create_top_gradient(CInifile& pIni, shared_str const& sect) {
    m_GradientTop = xr_new<SFlare>();
    m_GradientTop->shader = pIni.r_string(sect, "gradient_top_shader");
    m_GradientTop->texture = pIni.r_string(sect, "gradient_top_texture");
    m_GradientTop->fRadius = pIni.r_fvector2(sect, "gradient_top_radius");
    m_GradientTop->fOpacity = pIni.r_float(sect, "gradient_top_opacity");
    m_GradientTop->m_pFlare->CreateShader(*m_GradientTop->shader, m_GradientTop->texture.c_str());
}

void SThunderboltDesc::create_center_gradient(CInifile& pIni, shared_str const& sect) {
    m_GradientCenter = xr_new<SFlare>();
    m_GradientCenter->shader = pIni.r_string(sect, "gradient_center_shader");
    m_GradientCenter->texture = pIni.r_string(sect, "gradient_center_texture");
    m_GradientCenter->fRadius = pIni.r_fvector2(sect, "gradient_center_radius");
    m_GradientCenter->fOpacity = pIni.r_float(sect, "gradient_center_opacity");
    m_GradientCenter->m_pFlare->CreateShader(*m_GradientCenter->shader, m_GradientCenter->texture.c_str());
}

void SThunderboltDesc::load(CInifile& pIni, shared_str const& sect) {
    create_top_gradient(pIni, sect);
    create_center_gradient(pIni, sect);

    name = sect;
    color_anim = LALib.FindItem(pIni.r_string(sect, "color_anim"));
    VERIFY(color_anim);
    color_anim->fFPS = static_cast<float>(color_anim->iFrameCount);

    LPCSTR m_name = pIni.r_string(sect, "lightning_model");
    m_pRender->CreateModel(m_name);

    m_name = pIni.r_string(sect, "sound");
    if (m_name && m_name[0])
        snd.create(m_name, st_Effect, sg_Undefined);
}

//----------------------------------------------------------------------------------------------

void SThunderboltCollection::load(CInifile* pIni, CInifile* thunderbolts, LPCSTR sect) {
    section = sect;
    const int tb_count = pIni->line_count(sect);
    palette.reserve(tb_count); 
    
    for (int tb_idx = 0; tb_idx < tb_count; ++tb_idx) {
        LPCSTR N, V;
        if (pIni->r_line(sect, tb_idx, &N, &V)) {
            palette.push_back(g_pGamePersistent->Environment().thunderbolt_description(*thunderbolts, N));
        }
    }
}

SThunderboltCollection::~SThunderboltCollection() {
    for (auto* desc : palette) {
        xr_delete(desc);
    }
    palette.clear();
}

//----------------------------------------------------------------------------------------------

CEffect_Thunderbolt::CEffect_Thunderbolt() {
    string_path ext_path;
    if (FS.exist(ext_path, "$game_config$", "noirEngineExtention.ltx")) {
        CInifile ext_ini(ext_path);
        if (ext_ini.section_exist("environment") && ext_ini.line_exist("environment", "enable_custom_lightning")) {
            m_bEnableCustomLightning = ext_ini.r_bool("environment", "enable_custom_lightning");
        }
    }

    if (m_bEnableCustomLightning) {
        string_path wex_path;
        if (FS.exist(wex_path, "$game_config$", "environment\\noirWeatherEffect.ltx")) {
            CInifile wex_ini(wex_path);
            if (wex_ini.section_exist("custom_lightning")) {
                
                // MULTI STRIKE CONFIG
                if (wex_ini.line_exist("custom_lightning", "enable_multi_strikes"))
                    m_bEnableMultiStrikes = wex_ini.r_bool("custom_lightning", "enable_multi_strikes");
                if (wex_ini.line_exist("custom_lightning", "multi_strike_chance"))
                    m_iMultiStrikeChance = wex_ini.r_s32("custom_lightning", "multi_strike_chance");
                if (wex_ini.line_exist("custom_lightning", "max_simultaneous"))
                    m_iMaxSimultaneous = wex_ini.r_s32("custom_lightning", "max_simultaneous");
                if (wex_ini.line_exist("custom_lightning", "same_location_chance"))
                    m_iSameLocationChance = wex_ini.r_s32("custom_lightning", "same_location_chance");
                if (wex_ini.line_exist("custom_lightning", "branching_chance"))
                    m_iBranchingChance = wex_ini.r_s32("custom_lightning", "branching_chance");

                m_fHeightFactor = wex_ini.r_float("custom_lightning", "height_factor");
                m_fSizeMultiplier = wex_ini.r_float("custom_lightning", "size_multiplier");
                m_iProbNear = wex_ini.r_s32("custom_lightning", "prob_near");
                m_iProbMedium = wex_ini.r_s32("custom_lightning", "prob_medium");
                m_fDistNearMin = wex_ini.r_float("custom_lightning", "dist_near_min");
                m_fDistNearMax = wex_ini.r_float("custom_lightning", "dist_near_max");
                m_fDistMediumMin = wex_ini.r_float("custom_lightning", "dist_medium_min");
                m_fDistMediumMax = wex_ini.r_float("custom_lightning", "dist_medium_max");
                m_fDistFarMin = wex_ini.r_float("custom_lightning", "dist_far_min");
                m_fDistFarMax = wex_ini.r_float("custom_lightning", "dist_far_max");
                
                if (wex_ini.line_exist("custom_lightning", "strike_particle")) {
                    m_sStrikeParticle = wex_ini.r_string("custom_lightning", "strike_particle");
                }

                if (wex_ini.line_exist("custom_lightning", "strike_sound")) {
                    LPCSTR snd_name = wex_ini.r_string("custom_lightning", "strike_sound");
                    if (snd_name && snd_name[0]) {
                        m_StrikeSound.create(snd_name, st_Effect, sg_Undefined); 
                    }
                }

                if (wex_ini.line_exist("custom_lightning", "thunder_range_min")) 
                    m_fThunderRangeMin = wex_ini.r_float("custom_lightning", "thunder_range_min");
                if (wex_ini.line_exist("custom_lightning", "thunder_range_max")) 
                    m_fThunderRangeMax = wex_ini.r_float("custom_lightning", "thunder_range_max");
                if (wex_ini.line_exist("custom_lightning", "thunder_fade_dist")) 
                    m_fThunderFadeDist = wex_ini.r_float("custom_lightning", "thunder_fade_dist");
                if (wex_ini.line_exist("custom_lightning", "thunder_pitch_drop")) 
                    m_fThunderPitchDrop = wex_ini.r_float("custom_lightning", "thunder_pitch_drop");
                if (wex_ini.line_exist("custom_lightning", "thunder_vol_drop")) 
                    m_fThunderVolDrop = wex_ini.r_float("custom_lightning", "thunder_vol_drop");

                if (wex_ini.line_exist("custom_lightning", "strike_snd_range_min")) 
                    m_fStrikeSndRangeMin = wex_ini.r_float("custom_lightning", "strike_snd_range_min");
                if (wex_ini.line_exist("custom_lightning", "strike_snd_range_max")) 
                    m_fStrikeSndRangeMax = wex_ini.r_float("custom_lightning", "strike_snd_range_max");

                if (wex_ini.line_exist("custom_lightning", "strike_damage_power")) 
                    m_fStrikeDamagePower = wex_ini.r_float("custom_lightning", "strike_damage_power");
                if (wex_ini.line_exist("custom_lightning", "strike_damage_radius")) 
                    m_fStrikeDamageRadius = wex_ini.r_float("custom_lightning", "strike_damage_radius");
            } else {
                m_bEnableCustomLightning = false;
            }
        } else {
            m_bEnableCustomLightning = false;
        }
    }

#ifndef _EDITOR
    for (int i = 0; i < MAX_STRIKES; ++i) {
        m_Strikes[i].light = ::Render->light_create();
        if (m_Strikes[i].light) {
            m_Strikes[i].light->set_type(IRender_Light::POINT);
            m_Strikes[i].light->set_shadow(true);
            m_Strikes[i].light->set_active(false);
        }
    }
#endif
}

CEffect_Thunderbolt::~CEffect_Thunderbolt() {
#ifndef _EDITOR
    for (int i = 0; i < MAX_STRIKES; ++i) {
        if (m_Strikes[i].light) {
            m_Strikes[i].light->set_active(false);
            m_Strikes[i].light.destroy();
        }
    }
#endif

    m_StrikeSound.destroy();

    for (auto* col : collection) {
        xr_delete(col);
    }
    collection.clear();
}

std::string CEffect_Thunderbolt::AppendDef(CEnvironment& environment, CInifile* pIni, CInifile* thunderbolts, LPCSTR sect) {
    if (!sect || (sect[0] == '\0'))
        return "";
        
    for (const auto* item : collection) {
        if (item->section == sect)
            return item->section;
    }
    
    collection.push_back(environment.thunderbolt_collection(pIni, thunderbolts, sect));
    return collection.back()->section;
}

BOOL CEffect_Thunderbolt::RayPick(const Fvector& s, const Fvector& d, float& dist) {
#ifdef _EDITOR
    return Tools->RayPick(s, d, dist, 0, 0);
#else
    collide::rq_result RQ;
    CObject* E = g_pGameLevel->CurrentViewEntity();
    if (g_pGameLevel->ObjectSpace.RayPick(s, d, dist, collide::rqtBoth, RQ, E)) {
        dist = RQ.range;
        return TRUE;
    } 
    
    Fvector N = { 0.f, -1.f, 0.f };
    Fvector P = { 0.f, 0.f, 0.f };
    Fplane PL;
    PL.build(P, N);
    float dst = dist;
    if (PL.intersectRayDist(s, d, dst) && (dst <= dist)) {
        dist = dst;
        return TRUE;
    }
    return FALSE;
#endif
}

#define FAR_DIST g_pGamePersistent->Environment().CurrentEnv->far_plane

void CEffect_Thunderbolt::Bolt(const std::string& id, const float period, const float lt) {
    VERIFY(!id.empty());
    state = stWorking;
    
    int strikes_to_spawn = 1;
    if (m_bEnableCustomLightning && m_bEnableMultiStrikes && Random.randI(0, 100) < m_iMultiStrikeChance) {
        strikes_to_spawn = Random.randI(2, m_iMaxSimultaneous + 1);
    }

    CEnvironment& environment = g_pGamePersistent->Environment();
    float sun_h, sun_p;
    environment.CurrentEnv->sun_dir.getHP(sun_h, sun_p);

    bool base_generated = false;
    Fvector base_pos, base_dev, base_light_dir, base_hit_pos, base_center;
    float base_strike_dist = 0.f;
    float base_lightning_size = 0.f;
    Fvector3 base_current_direction;
    BOOL base_bHit = FALSE;

    for (int i = 0; i < strikes_to_spawn; ++i) {
        SStrike* strike = nullptr;
        for (int j = 0; j < MAX_STRIKES; ++j) {
            if (!m_Strikes[j].active) {
                strike = &m_Strikes[j];
                break;
            }
        }
        if (!strike) break;

        strike->active = true;
        strike->desc = g_pGamePersistent->Environment().thunderbolt_collection(collection, id.c_str())->GetRandomDesc();
        strike->life_time = lt + Random.randF(-lt * 0.5f, lt * 0.5f);
        
        if (i == 0) strike->current_time = 0.f;
        else strike->current_time = Random.randF(-0.35f, -0.05f); 

        bool use_base = (i > 0 && Random.randI(0, 100) < m_iSameLocationChance && base_generated);
        bool is_branch = (use_base && Random.randI(0, 100) < m_iBranchingChance);

        Fmatrix XF, S;
        Fvector pos, dev, light_dir, hit_pos;
        float strike_dist = 0.f;
        BOOL bHit = FALSE;

        if (use_base) {
            if (is_branch) {
                pos = base_pos;
                strike->direction = base_current_direction;
                strike_dist = base_strike_dist;
                
                dev.set(Random.randF(-environment.p_tilt, environment.p_tilt),
                        Random.randF(0, PI_MUL_2),
                        Random.randF(-environment.p_tilt, environment.p_tilt));
                
                light_dir.set(0.f, -1.f, 0.f); 
                XF.setXYZi(dev);
                XF.transform_dir(light_dir);
                
                strike->size = base_lightning_size + Random.randF(-30.f, 30.f);
                bHit = RayPick(pos, light_dir, strike->size);
                
                strike->center.mad(pos, light_dir, strike->size * 0.5f);
                hit_pos.mad(pos, light_dir, strike->size);
            } else {
                pos = base_pos;
                dev = base_dev;
                light_dir = base_light_dir;
                strike_dist = base_strike_dist;
                strike->size = base_lightning_size;
                bHit = base_bHit;
                hit_pos = base_hit_pos;
                strike->center = base_center;
                strike->direction = base_current_direction;
            }
        } else {
            if (m_bEnableCustomLightning) {
                float height = FAR_DIST * m_fHeightFactor; 
                float R = 0.f;
                int strike_type = Random.randI(0, 100);

                if (strike_type < m_iProbNear) {
                    R = Random.randF(m_fDistNearMin, m_fDistNearMax);
                } else if (strike_type < (m_iProbNear + m_iProbMedium)) {
                    R = Random.randF(m_fDistMediumMin, m_fDistMediumMax);
                } else {
                    R = Random.randF(m_fDistFarMin, m_fDistFarMax);
                }

                strike_dist = R; 
                float dist = std::hypot(height, R);
                float alt = atan2(height, R); 
                float lng = Random.randF(0.f, PI_MUL_2); 

                strike->direction.setHP(lng, alt);
                pos.mad(Device.vCameraPosition, strike->direction, dist);
                dev.set(Random.randF(-environment.p_tilt, environment.p_tilt),
                        Random.randF(0, PI_MUL_2),
                        Random.randF(-environment.p_tilt, environment.p_tilt));
                
                strike->direction.invert(); 
                strike->size = std::max(dist * m_fSizeMultiplier, 50.f); 
            } else {
                float alt = environment.p_var_alt; 
                float lng = Random.randF(sun_h - environment.p_var_long + PI, sun_h + environment.p_var_long + PI);
                float dist = Random.randF(FAR_DIST * environment.p_min_dist, FAR_DIST * 0.95f);
                strike_dist = dist;
                
                strike->direction.setHP(lng, alt);
                pos.mad(Device.vCameraPosition, strike->direction, dist);
                dev.set(Random.randF(-environment.p_tilt, environment.p_tilt),
                        Random.randF(0, PI_MUL_2),
                        Random.randF(-environment.p_tilt, environment.p_tilt));
                
                strike->direction.invert(); 
                strike->size = FAR_DIST * 2.f;
            }

            light_dir.set(0.f, -1.f, 0.f); 
            XF.setXYZi(dev);
            XF.transform_dir(light_dir);
            bHit = RayPick(pos, light_dir, strike->size);
            
            strike->center.mad(pos, light_dir, strike->size * 0.5f);
            hit_pos.mad(pos, light_dir, strike->size);

            if (i == 0) {
                base_generated = true;
                base_pos = pos;
                base_dev = dev;
                base_light_dir = light_dir;
                base_strike_dist = strike_dist;
                base_lightning_size = strike->size;
                base_current_direction = strike->direction;
                base_bHit = bHit;
                base_hit_pos = hit_pos;
                base_center = strike->center;
            }
        }

        S.scale(strike->size, strike->size, strike->size);
        XF.setXYZi(dev);
        XF.translate_over(pos);
        strike->xform.mul_43(XF, S);

        if (!use_base || i == 0) {
            float snd_vol = 1.0f;
            float snd_freq = 1.0f; 
            Fvector2 snd_range;
            snd_range.set(m_fThunderRangeMin, m_fThunderRangeMax);

            if (strike_dist > m_fThunderRangeMin) {
                float dist_factor = std::clamp((strike_dist - m_fThunderRangeMin) / m_fThunderFadeDist, 0.0f, 1.0f);
                snd_freq = std::max(0.1f, 1.0f - (dist_factor * m_fThunderPitchDrop)); 
                snd_vol = std::max(0.1f, 1.0f - (dist_factor * m_fThunderVolDrop));  
            }

            strike->desc->snd.play_no_feedback(0, 0, strike_dist / 300.f, &pos, &snd_vol, &snd_freq, &snd_range);
        }

#ifndef _EDITOR
        if (bHit && (!use_base || i == 0 || is_branch)) { // Гілки також створюють спалахи на землі
            if (m_sStrikeParticle.size()) g_pGamePersistent->PlayParticle(m_sStrikeParticle.c_str(), hit_pos);
            
            if (m_StrikeSound._handle()) {
                Fvector2 strike_range;
                strike_range.set(m_fStrikeSndRangeMin, m_fStrikeSndRangeMax);
                m_StrikeSound.play_no_feedback(0, 0, strike_dist / 300.f, &hit_pos, 0, 0, &strike_range);
            }

            if (m_fStrikeDamagePower > 0.f && m_fStrikeDamageRadius > 0.f) {
                g_pGamePersistent->ApplyLightningDamage(hit_pos, m_fStrikeDamageRadius, m_fStrikeDamagePower);
            }
        }

        if (strike->light && strike_dist < m_fDistMediumMax && (!use_base || is_branch)) {
            strike->using_light = true;
            Fvector light_pos = hit_pos;
            light_pos.mad(light_dir, -10.f); 
            strike->light->set_position(light_pos);
            float safe_range = std::clamp(strike->size * 0.3f, 30.f, 100.f);
            strike->light->set_range(safe_range);
        } else {
            strike->using_light = false;
        }
#endif
    }

    if (Random.randF() < environment.p_second_prop) {
        next_lightning_time = Device.fTimeGlobal + lt + EPS_L;
    } else {
        next_lightning_time = Device.fTimeGlobal + period + Random.randF(-period * 0.3f, period * 0.3f);
    }
}

void CEffect_Thunderbolt::OnFrame(const std::string& id, const float period, const float duration) {
    const bool enabled = !id.empty();
    if (bEnabled != enabled) {
        bEnabled = enabled;
        next_lightning_time = Device.fTimeGlobal + period + Random.randF(-period * 0.5f, period * 0.5f);
    } else if (bEnabled && (Device.fTimeGlobal > next_lightning_time)) {
        if (state == stIdle && !id.empty())
            Bolt(id, period, duration);
    }
    
    if (state == stWorking) {
        bool any_active = false;
        Fvector max_fClr = {0.f, 0.f, 0.f};
        Fvector3 best_direction = {0.f, -1.f, 0.f};

        for (int i = 0; i < MAX_STRIKES; ++i) {
            if (!m_Strikes[i].active) continue;
            any_active = true;

            if (m_Strikes[i].current_time < 0.f) {
                m_Strikes[i].current_time += Device.fTimeDelta;
#ifndef _EDITOR
                if (m_Strikes[i].light && m_Strikes[i].using_light) m_Strikes[i].light->set_active(false);
#endif
                continue; 
            }

            m_Strikes[i].current_time += Device.fTimeDelta;

            if (m_Strikes[i].current_time > m_Strikes[i].life_time) {
                m_Strikes[i].active = false;
#ifndef _EDITOR
                if (m_Strikes[i].light) m_Strikes[i].light->set_active(false);
#endif
                continue;
            }

            int frame;
            const u32 uClr = m_Strikes[i].desc->color_anim->CalculateRGB(m_Strikes[i].current_time / m_Strikes[i].life_time, frame);
            Fvector fClr;
            fClr.set(clampr(float(color_get_R(uClr)) / 255.f, 0.f, 1.f),
                     clampr(float(color_get_G(uClr)) / 255.f, 0.f, 1.f),
                     clampr(float(color_get_B(uClr)) / 255.f, 0.f, 1.f));

            m_Strikes[i].phase = clampr(1.5f * (m_Strikes[i].current_time / m_Strikes[i].life_time), 0.f, 1.f);
            
            max_fClr.x = std::max(max_fClr.x, fClr.x);
            max_fClr.y = std::max(max_fClr.y, fClr.y);
            max_fClr.z = std::max(max_fClr.z, fClr.z);
            best_direction = m_Strikes[i].direction;

#ifndef _EDITOR
            if (m_Strikes[i].using_light && m_Strikes[i].light) {
                m_Strikes[i].light->set_active(true);
                m_Strikes[i].light->set_color(fClr.x * 3.f, fClr.y * 3.f, fClr.z * 3.f);
            }
#endif
        }

        if (!any_active) {
            state = stIdle;
        } else if (max_fClr.x > 0.f || max_fClr.y > 0.f || max_fClr.z > 0.f) {
            CEnvironment& environment = g_pGamePersistent->Environment();
            Fvector& sky_color = environment.CurrentEnv->sky_color;
            
            sky_color.mad(max_fClr, environment.p_sky_color);
            sky_color.set(clampr(sky_color.x, 0.f, 1.f), clampr(sky_color.y, 0.f, 1.f), clampr(sky_color.z, 0.f, 1.f));

            environment.CurrentEnv->sun_color.mad(max_fClr, environment.p_sun_color);
            environment.CurrentEnv->fog_color.mad(max_fClr, environment.p_fog_color);

            if (::Render->get_generation() >= IRender_interface::GENERATION_R2) {
                R_ASSERT(xr::valid(best_direction));
                g_pGamePersistent->Environment().CurrentEnv->sun_dir = best_direction;
            }
        }
    }
}

void CEffect_Thunderbolt::Render() {
    if (state == stWorking) {
        for (int i = 0; i < MAX_STRIKES; ++i) {
            if (m_Strikes[i].active && m_Strikes[i].current_time >= 0.f) {
                
                current = m_Strikes[i].desc;
                current_xform = m_Strikes[i].xform;
                current_direction = m_Strikes[i].direction;
                lightning_center = m_Strikes[i].center; 
                lightning_size = m_Strikes[i].size;
                lightning_phase = m_Strikes[i].phase;
                
                m_pRender->Render(*this);
            }
        }
    }
}

// --- SCRIPT‑DRIVEN LIGHTNING STRIKE ---
void CEffect_Thunderbolt::ForceStrike(LPCSTR id, const Fvector& target_pos) {
    if (!m_bEnableCustomLightning || !id || id[0] == '\0') return;
    
    SStrike* strike = nullptr;
    for (int j = 0; j < MAX_STRIKES; ++j) {
        if (!m_Strikes[j].active) {
            strike = &m_Strikes[j];
            break;
        }
    }
    if (!strike) return; 

    state = stWorking;
    strike->active = true;
    strike->life_time = 2.0f + Random.randF(-0.5f, 0.5f); 
    strike->current_time = 0.f;

    strike->desc = g_pGamePersistent->Environment().thunderbolt_collection(collection, id)->GetRandomDesc();
    if (!strike->desc) return;

    Fmatrix XF, S;
    float height = FAR_DIST * m_fHeightFactor;
    Fvector pos = target_pos;
    pos.y += height; 

    Fvector light_dir = { 0.f, -1.f, 0.f };
    strike->size = height + 10.f; 
    RayPick(pos, light_dir, strike->size);

    strike->center.mad(pos, light_dir, strike->size * 0.5f);

    S.scale(strike->size, strike->size, strike->size);
    XF.setXYZi(Random.randF(-0.05f, 0.05f), Random.randF(0, PI_MUL_2), Random.randF(-0.05f, 0.05f));
    XF.translate_over(pos);
    strike->xform.mul_43(XF, S);

    Fvector snd_pos = target_pos; 
    float dist_to_strike = Device.vCameraPosition.distance_to(target_pos);
    
    float snd_vol = 1.0f;
    float snd_freq = 1.0f; 
    Fvector2 snd_range;
    snd_range.set(m_fThunderRangeMin, m_fThunderRangeMax);

    if (dist_to_strike > m_fThunderRangeMin) {
        float dist_factor = std::clamp((dist_to_strike - m_fThunderRangeMin) / m_fThunderFadeDist, 0.0f, 1.0f);
        snd_freq = std::max(0.1f, 1.0f - (dist_factor * m_fThunderPitchDrop)); 
        snd_vol = std::max(0.1f, 1.0f - (dist_factor * m_fThunderVolDrop));  
    }

    strike->desc->snd.play_no_feedback(0, 0, dist_to_strike / 300.f, &snd_pos, &snd_vol, &snd_freq, &snd_range);

    strike->direction.sub(target_pos, Device.vCameraPosition).normalize_safe();

#ifndef _EDITOR
    if (m_sStrikeParticle.size()) {
        g_pGamePersistent->PlayParticle(m_sStrikeParticle.c_str(), target_pos);
    }

    if (m_StrikeSound._handle()) {
        Fvector2 strike_range;
        strike_range.set(m_fStrikeSndRangeMin, m_fStrikeSndRangeMax);
        m_StrikeSound.play_no_feedback(0, 0, dist_to_strike / 300.f, &snd_pos, 0, 0, &strike_range);
    }

    if (m_fStrikeDamagePower > 0.f && m_fStrikeDamageRadius > 0.f) {
        g_pGamePersistent->ApplyLightningDamage(target_pos, m_fStrikeDamageRadius, m_fStrikeDamagePower);
    }

    if (strike->light) {
        strike->using_light = true;
        Fvector light_pos = target_pos;
        light_pos.y += 15.f; 
        
        strike->light->set_position(light_pos);
        strike->light->set_range(80.f); 
    }
#endif
}