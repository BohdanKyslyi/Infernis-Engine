#include "stdafx.h"
#pragma once

#ifndef _EDITOR
#include "render.h"
#endif
#include "Thunderbolt.h"
#include "igame_persistent.h"
#include "LightAnimLibrary.h"
#include "Environment.h"
#include "WeatherEffectConfig.h"
#include "xrRender/particles_systems_library_interface.hpp"

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
    if (pSettings && pSettings->section_exist("environment") &&
        pSettings->line_exist("environment", "enable_custom_lightning"))
        m_bEnableCustomLightning =
            pSettings->r_bool("environment", "enable_custom_lightning");

    string_path path;
    FS.update_path(path, "$game_config$", "environment\\noirWeatherEffect.ltx");
    std::string bytes;
    bool exists=false;
    if (weather_effect_config::read(path,bytes,exists) && exists) {
        IReader reader(bytes.data(),static_cast<int>(bytes.size()));
        string_path directory;
        FS.update_path(directory,"$game_config$","environment\\");
        CInifile config(&reader,directory);
        auto settings=GetEditorSettings();
        if(config.section_exist("custom_lightning")) {
            for(unsigned i=0;i<lightning_editor::count;++i) {
                const auto& p=lightning_editor::parameters[i];
                if(config.line_exist("custom_lightning",p.key)) {
                    LPCSTR value=config.r_string("custom_lightning",p.key);
                    if(value && value[0]) settings.values[i]= i==0
                        ? float(config.r_bool("custom_lightning",p.key))
                        : config.r_float("custom_lightning",p.key);
                }
            }
            if(config.line_exist("custom_lightning","strike_particle")) {
                LPCSTR v=config.r_string("custom_lightning","strike_particle");
                settings.particle=v?v:"";
            }
            if(config.line_exist("custom_lightning","strike_sound")) {
                LPCSTR v=config.r_string("custom_lightning","strike_sound");
                settings.sound=v?v:"";
            }
            // Retain the configured particle name, including legacy single effects
            // which are not enumerated by the group-only particles library UI.
            m_sStrikeParticle=settings.particle.c_str();
            m_sConfiguredStrikeParticle=m_sStrikeParticle;
            if(!settings.sound.empty() && !snd_localized_exists((settings.sound+".ogg").c_str())) {
                Msg("! Lightning: strike sound is missing [%s]",settings.sound.c_str());
                settings.sound.clear();
            }
            if (!ApplyEditorSettings(settings)) {
                Msg("! Lightning: invalid strike asset; keeping numeric settings without strike assets");
                settings.particle.clear(); settings.sound.clear();
                ApplyEditorSettings(settings);
            }
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

lightning_editor::Settings CEffect_Thunderbolt::GetEditorSettings() const {
    lightning_editor::Settings s;
    s.values[0]=static_cast<float>(m_bEnableMultiStrikes);
    s.values[1]=static_cast<float>(m_iMultiStrikeChance);
    s.values[2]=static_cast<float>(m_iMaxSimultaneous);
    s.values[3]=static_cast<float>(m_iSameLocationChance);
    s.values[4]=static_cast<float>(m_iBranchingChance);
    s.values[5]=static_cast<float>(m_fHeightFactor);
    s.values[6]=static_cast<float>(m_fSizeMultiplier);
    s.values[7]=static_cast<float>(m_iProbNear);
    s.values[8]=static_cast<float>(m_iProbMedium);
    s.values[9]=static_cast<float>(m_fDistNearMin);
    s.values[10]=static_cast<float>(m_fDistNearMax);
    s.values[11]=static_cast<float>(m_fDistMediumMin);
    s.values[12]=static_cast<float>(m_fDistMediumMax);
    s.values[13]=static_cast<float>(m_fDistFarMin);
    s.values[14]=static_cast<float>(m_fDistFarMax);
    s.values[15]=static_cast<float>(m_fThunderRangeMin);
    s.values[16]=static_cast<float>(m_fThunderRangeMax);
    s.values[17]=static_cast<float>(m_fThunderFadeDist);
    s.values[18]=static_cast<float>(m_fThunderPitchDrop);
    s.values[19]=static_cast<float>(m_fThunderVolDrop);
    s.values[20]=static_cast<float>(m_fStrikeSndRangeMin);
    s.values[21]=static_cast<float>(m_fStrikeSndRangeMax);
    s.values[22]=static_cast<float>(m_fStrikeDamagePower);
    s.values[23]=static_cast<float>(m_fStrikeDamageRadius);
    s.particle=m_sStrikeParticle.size()?m_sStrikeParticle.c_str():"";
    s.sound=m_sStrikeSoundName.size()?m_sStrikeSoundName.c_str():"";
    return s;
}

bool CEffect_Thunderbolt::ApplyEditorSettings(const lightning_editor::Settings& requested) {
    const auto s=lightning_editor::normalize(requested);
    const auto clean=[](const std::string& value) {
        return value.size()<240 && value.find_first_of("\r\n;#=[]") == std::string::npos;
    };
    if(!clean(s.particle) || !clean(s.sound)) return false;
    const auto old=GetEditorSettings();
    if(!s.particle.empty() && s.particle!=old.particle &&
       s.particle!=(m_sConfiguredStrikeParticle.size()?m_sConfiguredStrikeParticle.c_str():"")) {
        bool found=false;
        const auto& library=g_pGamePersistent->Environment().m_pRender->particles_systems_library();
        auto i=library.particles_group_begin();
        const auto end=library.particles_group_end();
        for(;i!=end;library.particles_group_next(i))
            if(s.particle==library.particles_group_id(**i).c_str()) {found=true;break;}
        if(!found) return false;
    }
    if(!s.sound.empty() && s.sound!=old.sound) {
        const std::string name=s.sound+".ogg";
        if(!snd_localized_exists(name.c_str())) return false;
    }
    if(s.sound!=old.sound) {
        m_StrikeSound.destroy();
        m_sStrikeSoundName=s.sound.c_str();
        if(!s.sound.empty()) m_StrikeSound.create(s.sound.c_str(),st_Effect,sg_Undefined);
    }
    m_sStrikeParticle=s.particle.c_str();
    m_bEnableMultiStrikes=s.values[0]>.5f;
    m_iMultiStrikeChance=static_cast<int>(s.values[1]);
    m_iMaxSimultaneous=static_cast<int>(s.values[2]);
    m_iSameLocationChance=static_cast<int>(s.values[3]);
    m_iBranchingChance=static_cast<int>(s.values[4]);
    m_fHeightFactor=s.values[5];
    m_fSizeMultiplier=s.values[6];
    m_iProbNear=static_cast<int>(s.values[7]);
    m_iProbMedium=static_cast<int>(s.values[8]);
    m_fDistNearMin=s.values[9];
    m_fDistNearMax=s.values[10];
    m_fDistMediumMin=s.values[11];
    m_fDistMediumMax=s.values[12];
    m_fDistFarMin=s.values[13];
    m_fDistFarMax=s.values[14];
    m_fThunderRangeMin=s.values[15];
    m_fThunderRangeMax=s.values[16];
    m_fThunderFadeDist=s.values[17];
    m_fThunderPitchDrop=s.values[18];
    m_fThunderVolDrop=s.values[19];
    m_fStrikeSndRangeMin=s.values[20];
    m_fStrikeSndRangeMax=s.values[21];
    m_fStrikeDamagePower=s.values[22];
    m_fStrikeDamageRadius=s.values[23];
    return true;
}

bool CEffect_Thunderbolt::SaveEditorSettings() {
    string_path path;
    FS.update_path(path,"$game_config$","environment\\noirWeatherEffect.ltx");
    std::string text; bool exists=false;
    if(!weather_effect_config::read(path,text,exists)) return false;
    const std::string original=text;
    const auto s=GetEditorSettings();
    for(unsigned i=0;i<lightning_editor::count;++i) {
        const auto& p=lightning_editor::parameters[i];
        string64 value;
        if(i==0) xr_strcpy(value,s.values[i]>.5f?"true":"false");
        else if(p.integral) xr_sprintf(value,"%d",static_cast<int>(s.values[i]));
        else xr_sprintf(value,"%.6f",s.values[i]);
        text=rain_config::set_section_value(text,"custom_lightning",p.key,value);
    }
    text=rain_config::set_section_value(text,"custom_lightning","strike_particle",s.particle);
    text=rain_config::set_section_value(text,"custom_lightning","strike_sound",s.sound);
    return weather_effect_config::write(path,text,original,exists);
}

bool CEffect_Thunderbolt::PreviewEditor(LPCSTR id,float duration) {
    CEnvironment& environment=g_pGamePersistent->Environment();
    if(!m_bEnableCustomLightning || !id || !id[0] ||
       !environment.m_thunderbolt_collections_config ||
       !environment.m_thunderbolt_collections_config->section_exist(id) || state!=stIdle) return false;
    // A collection no loaded weather uses is not in "collection" yet, and thunderbolt_collection()
    // has no "not found" result in a release build ( NODEFAULT = __assume(0) ): the test crashed
    // here. It is loaded first, as CEnvDescriptor::set_thunderbolt_collection does; a loaded one
    // comes back as it is. The weather keeps its own collection.
    if(AppendDef(environment,environment.m_thunderbolt_collections_config,
                 environment.m_thunderbolts_config,id).empty()) return false;
    const auto* selected=environment.thunderbolt_collection(collection,id);
    if(!selected || selected->palette.empty()) return false;
    if(!std::isfinite(duration)) duration=.5f;
    m_editor_safe_preview=true;
    Bolt(id,std::max(1.f,environment.CurrentEnv->bolt_period),std::clamp(duration,.1f,5.f));
    m_editor_safe_preview=false;
    return true;
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
    const float thunder_min=m_bEnableCustomLightning?m_fThunderRangeMin:120.f;
    const float thunder_max=m_bEnableCustomLightning?m_fThunderRangeMax:1000.f;
    const float fade_distance=m_bEnableCustomLightning?m_fThunderFadeDist:300.f;
    const float pitch_drop=m_bEnableCustomLightning?m_fThunderPitchDrop:.35f;
    const float volume_drop=m_bEnableCustomLightning?m_fThunderVolDrop:.25f;
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
                float lng = m_editor_safe_preview
                    ? Device.vCameraDirection.getH()
                    : Random.randF(0.f, PI_MUL_2);

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
            snd_range.set(thunder_min, thunder_max);

            if (strike_dist > thunder_min) {
                float dist_factor = std::clamp((strike_dist - thunder_min) / fade_distance, 0.0f, 1.0f);
                snd_freq = std::max(0.1f, 1.0f - (dist_factor * pitch_drop));
                snd_vol = std::max(0.1f, 1.0f - (dist_factor * volume_drop));
            }

            strike->desc->snd.play_no_feedback(0, 0, strike_dist / 300.f, &pos, &snd_vol, &snd_freq, &snd_range);
        }

#ifndef _EDITOR
        if (bHit && (!use_base || i == 0 || is_branch)) { // Гілки також створюють спалахи на землі
            if (m_bEnableCustomLightning && m_sStrikeParticle.size()) g_pGamePersistent->PlayParticle(m_sStrikeParticle.c_str(), hit_pos);
            
            if (m_bEnableCustomLightning && m_StrikeSound._handle()) {
                Fvector2 strike_range;
                strike_range.set(m_fStrikeSndRangeMin, m_fStrikeSndRangeMax);
                m_StrikeSound.play_no_feedback(0, 0, strike_dist / 300.f, &hit_pos, 0, 0, &strike_range);
            }

            if (m_bEnableCustomLightning && !m_editor_safe_preview && m_fStrikeDamagePower > 0.f && m_fStrikeDamageRadius > 0.f) {
                g_pGamePersistent->ApplyLightningDamage(hit_pos, m_fStrikeDamageRadius, m_fStrikeDamagePower);
            }
        }

        if (strike->light && strike_dist < (m_bEnableCustomLightning?m_fDistMediumMax:40.f) && (!use_base || is_branch)) {
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

    // The collection is checked before a strike slot is taken: thunderbolt_collection() has no
    // "not found" result in a release build ( NODEFAULT = __assume(0) ), GetRandomDesc() divides by
    // zero on an empty palette, and a slot left active without a description crashes OnFrame. A
    // collection no loaded weather uses is loaded first, as in PreviewEditor.
    CEnvironment& environment = g_pGamePersistent->Environment();
    if (!environment.m_thunderbolt_collections_config ||
        !environment.m_thunderbolt_collections_config->section_exist(id) ||
        AppendDef(environment, environment.m_thunderbolt_collections_config,
                  environment.m_thunderbolts_config, id).empty())
        return;
    const SThunderboltCollection* selected = environment.thunderbolt_collection(collection, id);
    if (selected->palette.empty()) return;
    SThunderboltDesc* desc = selected->GetRandomDesc();
    if (!desc) return;
    
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

    strike->desc = desc;

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

    if (!m_editor_safe_preview && m_fStrikeDamagePower > 0.f && m_fStrikeDamageRadius > 0.f) {
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
