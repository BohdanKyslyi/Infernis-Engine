#include "stdafx.h"
#include "Rain.h"
#include "igame_persistent.h"
#include "environment.h"
#include <algorithm>

#ifdef _EDITOR
#include "ui_toolscustom.h"
#else
#include "render.h"
#include "igame_level.h"
#include "../xrcdb/xr_area.h"
#include "xr_object.h"
#endif

// Warning: duplicated in dxRainRender
static const int max_desired_items = 6500;
static const float source_radius = 25.5f;
static const float source_offset = 40.f;
static const float max_distance = source_offset * 2.0f; 
static const float sink_offset = -(max_distance - source_offset);
static const float drop_length = 5.f;
static const float drop_width = 0.30f;
static const float drop_angle = 3.0f;
static const float drop_max_wind_vel = 20.0f;
static const float drop_speed_min = 40.f;
static const float drop_speed_max = 80.f;

const int max_particles = 1000;
const int particles_cache = 400;
const float particles_time = .3f;

//////////////////////////////////////////////////////////////////////
// SoA Container Implementation
//////////////////////////////////////////////////////////////////////

RainDropsSoA::RainDropsSoA(u32 max_items) : capacity(max_items), active_count(0) {
    size_t size_f = max_items * sizeof(float);
    size_t size_u32 = max_items * sizeof(u32);
    size_t size_u16 = max_items * sizeof(u16);

    px = static_cast<float*>(_mm_malloc(size_f, 32));
    py = static_cast<float*>(_mm_malloc(size_f, 32));
    pz = static_cast<float*>(_mm_malloc(size_f, 32));
    
    hit_x = static_cast<float*>(_mm_malloc(size_f, 32));
    hit_y = static_cast<float*>(_mm_malloc(size_f, 32));
    hit_z = static_cast<float*>(_mm_malloc(size_f, 32));
    
    dx = static_cast<float*>(_mm_malloc(size_f, 32));
    dy = static_cast<float*>(_mm_malloc(size_f, 32));
    dz = static_cast<float*>(_mm_malloc(size_f, 32));
    
    speed = static_cast<float*>(_mm_malloc(size_f, 32));
    
    dwTime_Life = static_cast<u32*>(_mm_malloc(size_u32, 32));
    dwTime_Hit = static_cast<u32*>(_mm_malloc(size_u32, 32));
    uv_set = static_cast<u32*>(_mm_malloc(size_u32, 32));
    
    material_idx = static_cast<u16*>(_mm_malloc(size_u16, 32));

    memset(px, 0, size_f);
    memset(py, 0, size_f);
    memset(pz, 0, size_f);
    memset(hit_x, 0, size_f);
    memset(hit_y, 0, size_f);
    memset(hit_z, 0, size_f);
    memset(dx, 0, size_f);
    memset(dy, 0, size_f);
    memset(dz, 0, size_f);
    memset(speed, 0, size_f);
    
    memset(dwTime_Life, 0, size_u32);
    memset(dwTime_Hit, 0, size_u32);
    memset(uv_set, 0, size_u32);
    memset(material_idx, 0, size_u16);
}

RainDropsSoA::~RainDropsSoA() {
    _mm_free(px);
    _mm_free(py);
    _mm_free(pz);
    _mm_free(hit_x);
    _mm_free(hit_y);
    _mm_free(hit_z);
    _mm_free(dx);
    _mm_free(dy);
    _mm_free(dz);
    _mm_free(speed);
    _mm_free(dwTime_Life);
    _mm_free(dwTime_Hit);
    _mm_free(uv_set);
    _mm_free(material_idx);
}

CEffect_Rain::CEffect_Rain() : drops(max_desired_items) {
    // Дефолтні значення на випадок, якщо конфігу немає або там помилка
    LPCSTR snd_name_2d = "ambient\\rain";
    LPCSTR snd_name_portal = "ambient\\rain";
    
    // === NOIR ENGINE MODULE INITIALIZATION ===
    string_path ext_path;
    if (FS.exist(ext_path, "$game_config$", "noirEngineExtention.ltx")) {
        CInifile ext_ini(ext_path);
        if (ext_ini.section_exist("environment")) {
            if (ext_ini.line_exist("environment", "enable_rain_material_sounds"))
                m_bEnableMaterialSounds = ext_ini.r_bool("environment", "enable_rain_material_sounds");
            
            if (ext_ini.line_exist("environment", "enable_dynamic_rain_wind"))
                m_bEnableDynamicWind = ext_ini.r_bool("environment", "enable_dynamic_rain_wind");
        }
    }

    string_path wex_path;
    if (FS.exist(wex_path, "$game_config$", "environment\\noirWeatherEffect.ltx")) {
        CInifile wex_ini(wex_path);
        
        // Зчитуємо шляхи до базових звуків дощу
        if (wex_ini.section_exist("rain_audio")) {
            if (wex_ini.line_exist("rain_audio", "sound_2d"))
                snd_name_2d = wex_ini.r_string("rain_audio", "sound_2d");
            if (wex_ini.line_exist("rain_audio", "sound_portal"))
                snd_name_portal = wex_ini.r_string("rain_audio", "sound_portal");
        }

        if (m_bEnableMaterialSounds && wex_ini.section_exist("rainmaterial")) {
            u32 line_count = wex_ini.line_count("rainmaterial");
            for (u32 i = 0; i < line_count; ++i) {
                LPCSTR key, value;
                wex_ini.r_line("rainmaterial", i, &key, &value);
                
                if (key && value && xr_strlen(value) > 0) {
                    RainHitSound rhs;
                    rhs.keyword = key;
                    rhs.snd.create(value, st_Effect, sg_Undefined);
                    float snd_length = rhs.snd.get_length_sec();
                    if (snd_length > 0.5f) { 
                        Msg("! [Noir Engine] ERROR: Rain sound '%s' is too long (%.2f sec). Max allowed is 0.5s! Sound disabled.", value, snd_length);
                        rhs.snd.destroy(); 
                    } else {
                        m_HitSounds.push_back(rhs);
                    }
                }
            }
        } else if (m_bEnableMaterialSounds) {
            m_bEnableMaterialSounds = false; 
        }

        if (m_bEnableDynamicWind && wex_ini.section_exist("dynamic_wind")) {
            m_fWindMaxAngle = wex_ini.r_float("dynamic_wind", "max_angle");
            m_fWindSpeedMultiplier = wex_ini.r_float("dynamic_wind", "speed_wind_multiplier");
        } else if (m_bEnableDynamicWind) {
            Msg("! [Noir Engine]: [Rain] Section [dynamic_wind] not found, using vanilla wind physics.");
            m_bEnableDynamicWind = false;
        }
    } else {
        Msg("! [Noir Engine]: [Rain] File noirWeatherEffect.ltx not found, using default values.");
        m_bEnableMaterialSounds = false;
        m_bEnableDynamicWind = false;
    }

    // Створюємо звуки ПІСЛЯ того, як зчитали їх імена з конфігу
    snd_Ambient2D.create(snd_name_2d, st_Effect, sg_Undefined);
    snd_AmbientPortal.create(snd_name_portal, st_Effect, sg_Undefined);

    p_create();
}

CEffect_Rain::~CEffect_Rain() {
    snd_Ambient2D.destroy();
    snd_AmbientPortal.destroy();
    for (auto& rhs : m_HitSounds) {
        rhs.snd.destroy();
    }
    m_HitSounds.clear();
    p_destroy();
}

void CEffect_Rain::Born(u32 id, float radius) {
    Fvector axis;
    axis.set(0, -1, 0);

    if (m_bEnableDynamicWind) {
        float gust = g_pGamePersistent->Environment().wind_strength_factor / 10.f;
        float k = std::clamp(g_pGamePersistent->Environment().CurrentEnv->wind_velocity * gust / drop_max_wind_vel, 0.f, 1.f);
        
        float pitch = deg2rad(m_fWindMaxAngle) * k - PI_DIV_2;
        axis.setHP(g_pGamePersistent->Environment().CurrentEnv->wind_direction, pitch);

        float base_speed = ::Random.randF(drop_speed_min, drop_speed_max);
        drops.speed[id] = base_speed + (base_speed * k * m_fWindSpeedMultiplier);
    } else {
        drops.speed[id] = ::Random.randF(drop_speed_min, drop_speed_max);
    }

    Fvector& view = Device.vCameraPosition;
    float angle = ::Random.randF(0.f, PI_MUL_2);
    float dist = std::sqrt(::Random.randF()) * radius;
    float x = dist * std::cos(angle);
    float z = dist * std::sin(angle);
    
    Fvector D;
    D.random_dir(axis, deg2rad(drop_angle));
    
    drops.dx[id] = D.x;
    drops.dy[id] = D.y;
    drops.dz[id] = D.z;

    drops.px[id] = x + view.x - D.x * source_offset;
    drops.py[id] = source_offset + view.y;
    drops.pz[id] = z + view.z - D.z * source_offset;
               
    float height = max_distance;
    u16 mat_idx = GAMEMTL_NONE_IDX; 
    
    Fvector s = { drops.px[id], drops.py[id], drops.pz[id] };
    BOOL bHit = RayPick(s, D, height, collide::rqtBoth, mat_idx);
    RenewItem(id, height, bHit, mat_idx);
}

BOOL CEffect_Rain::RayPick(const Fvector& s, const Fvector& d, float& range, collide::rq_target tgt, u16& material_idx) {
    BOOL bRes = TRUE;
    material_idx = GAMEMTL_NONE_IDX;
#ifdef _EDITOR
    Tools->RayPick(s, d, range);
#else
    collide::rq_result RQ;
    CObject* E = g_pGameLevel->CurrentViewEntity();
    bRes = g_pGameLevel->ObjectSpace.RayPick(s, d, range, tgt, RQ, E);
    if (bRes) {
        range = RQ.range;
        if (!RQ.O) {
            CDB::TRI* T = g_pGameLevel->ObjectSpace.GetStaticTris() + RQ.element;
            material_idx = T->material;
        }
    }
#endif
    return bRes;
}

void CEffect_Rain::RenewItem(u32 id, float height, BOOL bHit, u16 material_idx) {
    drops.uv_set[id] = Random.randI(2);
    drops.material_idx[id] = material_idx; 
    float speed = drops.speed[id];

    if (bHit) {
        drops.dwTime_Life[id] = Device.dwTimeGlobal + iFloor(1000.f * height / speed) - Device.dwTimeDelta;
        drops.dwTime_Hit[id] = drops.dwTime_Life[id];
        
        drops.hit_x[id] = drops.px[id] + drops.dx[id] * height;
        drops.hit_y[id] = drops.py[id] + drops.dy[id] * height;
        drops.hit_z[id] = drops.pz[id] + drops.dz[id] * height;
    } else {
        drops.dwTime_Life[id] = Device.dwTimeGlobal + iFloor(1000.f * height / speed) - Device.dwTimeDelta;
        drops.dwTime_Hit[id] = Device.dwTimeGlobal + iFloor(2.f * 1000.f * height / speed) - Device.dwTimeDelta;
        
        drops.hit_x[id] = drops.px[id];
        drops.hit_y[id] = drops.py[id];
        drops.hit_z[id] = drops.pz[id];
    }
}

void CEffect_Rain::OnFrame() {
    // Фікс витоку пулу партиклів (Оновлення часу життя сплесків)
    for (Particle* P = particle_active; P; ) {
        Particle* next = P->next;
        P->time -= Device.fTimeDelta;
        if (P->time < 0.f) p_free(P);
        P = next;
    }

#ifndef _EDITOR
    if (!g_pGameLevel) return;
#endif

    float factor = g_pGamePersistent->Environment().CurrentEnv->rain_density;

#ifndef _EDITOR
    m_uHitSoundsFrame = 0; // Скидаємо лічильник звуків зіткнень на початку кадру
    CObject* E = g_pGameLevel->CurrentViewEntity();
    
    static float current_openness = 1.0f;
    static Fvector current_snd_dir = {0.1f, 1.0f, 0.1f};
    
    float target_volume_mod = 0.05f; 
    float target_openness = 1.0f;
    Fvector target_dir = {0.f, 0.f, 0.f};

    if (E) {
        Fvector start_pos = Device.vCameraPosition;
        float ray_range = 20.0f; 
        
        Fvector dirs[9] = {
            { 0.000f, 1.00f,  0.000f}, { 0.000f, 0.25f,  0.968f}, { 0.000f, 0.25f, -0.968f},
            { 0.968f, 0.25f,  0.000f}, {-0.968f, 0.25f,  0.000f}, { 0.684f, 0.25f,  0.684f},
            {-0.684f, 0.25f,  0.684f}, { 0.684f, 0.25f, -0.684f}, {-0.684f, 0.25f, -0.684f} 
        };

        int escaped_rays = 0;
        u16 roof_material = GAMEMTL_NONE_IDX;

        for (int i = 0; i < 9; ++i) {
            collide::rq_result RQ;
            bool bHitSolid = false;

            if (g_pGameLevel->ObjectSpace.RayPick(start_pos, dirs[i], ray_range, collide::rqtStatic, RQ, E)) {
                if (!RQ.O) { 
                    CDB::TRI* T = g_pGameLevel->ObjectSpace.GetStaticTris() + RQ.element;
                    SGameMtl* mtl = GMLib.GetMaterialByIdx(T->material);
                    
                    // Ігноруємо кущі/листя для розрахунку оклюзії
                    if (mtl && !mtl->Flags.test(SGameMtl::flPassable) && 
                        !strstr(mtl->m_Name.c_str(), "bush") && !strstr(mtl->m_Name.c_str(), "leaves")) {
                        bHitSolid = true;
                        if (i == 0) roof_material = T->material;
                    }
                }
            }
            
            if (!bHitSolid) {
                escaped_rays++;
                target_dir.add(dirs[i]); 
            }
        }

        if (escaped_rays == 0 || target_dir.square_magnitude() < 0.01f) {
            target_dir.set(0.1f, 1.0f, 0.1f); 
        }
        target_dir.normalize();

        target_openness = escaped_rays / 9.0f;

        if (roof_material != GAMEMTL_NONE_IDX) {
            SGameMtl* mtl = GMLib.GetMaterialByIdx(roof_material);
            if (mtl) {
                LPCSTR mtl_name = mtl->m_Name.c_str();
                if (strstr(mtl_name, "tin") || strstr(mtl_name, "sheet")) target_volume_mod = 0.25f; 
                else if (strstr(mtl_name, "metal")) target_volume_mod = 0.08f; 
                else if (strstr(mtl_name, "wood") || strstr(mtl_name, "slate")) target_volume_mod = 0.15f; 
                else if (strstr(mtl_name, "concrete")) target_volume_mod = 0.05f; 
            }
        }

        float dt = std::clamp(Device.fTimeDelta * 3.0f, 0.001f, 1.0f);
        current_openness = current_openness * (1.0f - dt) + target_openness * dt;
        
        current_snd_dir.x = current_snd_dir.x * (1.0f - dt) + target_dir.x * dt;
        current_snd_dir.y = current_snd_dir.y * (1.0f - dt) + target_dir.y * dt;
        current_snd_dir.z = current_snd_dir.z * (1.0f - dt) + target_dir.z * dt;
        current_snd_dir.normalize();
    }
#endif

    switch (state) {
    case stIdle:
        if (factor < EPS_L) return;
        state = stWorking;
        snd_Ambient2D.play(0, sm_Looped);
        snd_AmbientPortal.play(0, sm_Looped | sm_2D);
        break;
    case stWorking:
        if (factor < EPS_L) {
            state = stDrippingOut;
            m_fDripEndTime = Device.fTimeGlobal + ::Random.randF(20.0f, 40.0f); 
            return;
        }
        break;
    case stDrippingOut:
        if (Device.fTimeGlobal > m_fDripEndTime) {
            state = stIdle;
            snd_Ambient2D.stop();
            snd_AmbientPortal.stop();
            return;
        } else {
            float time_left = m_fDripEndTime - Device.fTimeGlobal;
            float fade_factor = time_left / 40.0f; 
            
            if (::Random.randI(100) < static_cast<int>(15.f * fade_factor)) {
                Fvector drop_pos = Device.vCameraPosition;
                drop_pos.x += ::Random.randF(-10.f, 10.f); 
                drop_pos.z += ::Random.randF(-10.f, 10.f);
                drop_pos.y -= 1.5f; 
                Hit(drop_pos, GAMEMTL_NONE_IDX);
            }
        }
        break;
    }

    if (snd_Ambient2D._feedback() || snd_AmbientPortal._feedback()) {
        float vol_factor = (state == stDrippingOut) ? ((m_fDripEndTime - Device.fTimeGlobal) / 40.0f) : factor;
        
        if (snd_Ambient2D._feedback()) {
            float vol2D = vol_factor * current_openness;
            snd_Ambient2D.set_volume(vol2D * 0.6f);
            snd_Ambient2D.set_position(Device.vCameraPosition); 
        }

        if (snd_AmbientPortal._feedback()) {
            float volPortal = vol_factor * (1.0f - current_openness) * (target_volume_mod * 2.0f); 
            Fvector portal_pos = Device.vCameraPosition;
            portal_pos.mad(current_snd_dir, 6.0f); 
            
            snd_AmbientPortal.set_position(portal_pos);
            snd_AmbientPortal.set_volume(volPortal);
        }
    }
}

void CEffect_Rain::Render() {
#ifndef _EDITOR
    if (!g_pGameLevel) return;
#endif
    m_pRender->Render(*this);
}

void CEffect_Rain::Hit(Fvector& pos, u16 material_idx) {
    if (m_bEnableMaterialSounds && m_uHitSoundsFrame < 4) {
        float dist_sq = Device.vCameraPosition.distance_to_sqr(pos);
        if (dist_sq < 625.f) { 
            if (material_idx != GAMEMTL_NONE_IDX) {
                SGameMtl* mtl = GMLib.GetMaterialByIdx(material_idx);
                if (mtl) {
                    std::string_view mtl_name(mtl->m_Name.c_str()); 
                    size_t slash_pos = mtl_name.find_last_of("\\/");
                    std::string_view short_name = (slash_pos != std::string_view::npos) ? mtl_name.substr(slash_pos + 1) : mtl_name;

                    for (auto& rhs : m_HitSounds) { 
                        if (short_name == rhs.keyword.c_str()) { 
                            if (Device.dwTimeGlobal > rhs.last_play_time + 50) {
                                Fvector2 snd_range;
                                snd_range.set(5.f, 25.f);
                                rhs.snd.play_no_feedback(0, 0, 0, &pos, 0, 0, &snd_range);
                                rhs.last_play_time = Device.dwTimeGlobal; 
                                m_uHitSoundsFrame++;
                            }
                            break; 
                        }
                    }
                }
            }
        }
    }

    if (0 != ::Random.randI(2)) return;
    Particle* P = p_allocate();
    if (0 == P) return;

    const Fsphere& bv_sphere = m_pRender->GetDropBounds();

    P->time = particles_time;
    P->mXForm.rotateY(::Random.randF(PI_MUL_2));
    P->mXForm.translate_over(pos);
    P->mXForm.transform_tiny(P->bounds.P, bv_sphere.P);
    P->bounds.R = bv_sphere.R;
}

void CEffect_Rain::p_create() {
    particle_pool.resize(max_particles);
    for (u32 it = 0; it < particle_pool.size(); ++it) {
        Particle& P = particle_pool[it];
        P.prev = (it > 0) ? (&particle_pool[it - 1]) : nullptr;
        P.next = (it < particle_pool.size() - 1) ? (&particle_pool[it + 1]) : nullptr;
    }
    particle_active = nullptr;
    particle_idle = &particle_pool.front();
}

void CEffect_Rain::p_destroy() {
    particle_active = nullptr;
    particle_idle = nullptr;
    particle_pool.clear();
}

void CEffect_Rain::p_remove(Particle* P, Particle*& LST) {
    VERIFY(P);
    Particle* prev = P->prev;
    P->prev = nullptr;
    Particle* next = P->next;
    P->next = nullptr;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (LST == P) LST = next;
}

void CEffect_Rain::p_insert(Particle* P, Particle*& LST) {
    VERIFY(P);
    P->prev = nullptr;
    P->next = LST;
    if (LST) LST->prev = P;
    LST = P;
}

int CEffect_Rain::p_size(Particle* P) {
    if (!P) return 0;
    int cnt = 0;
    while (P) { P = P->next; ++cnt; }
    return cnt;
}

CEffect_Rain::Particle* CEffect_Rain::p_allocate() {
    Particle* P = particle_idle;
    if (!P) return nullptr;
    p_remove(P, particle_idle);
    p_insert(P, particle_active);
    return P;
}

void CEffect_Rain::p_free(Particle* P) {
    p_remove(P, particle_active);
    p_insert(P, particle_idle);
}