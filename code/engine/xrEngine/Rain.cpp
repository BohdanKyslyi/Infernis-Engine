#include "stdafx.h"
#include "Rain.h"
#include "igame_persistent.h"
#include "environment.h"
#include <algorithm>
#include <io.h>
#include "RainConfigEdit.h"

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
static const float drop_speed_min = 40.f;
static const float drop_speed_max = 80.f;

const int max_particles = 1000;
const int particles_cache = 400;
const float particles_time = .3f;

// Rain sounds may be omitted by a mod or supplied only by a language pack.
// Do not create a sound source for an asset that cannot be opened later.

static bool rain_sound_available(LPCSTR name) {
    if (!name || !name[0]) return false;
    string_path localized;
    xr_sprintf(localized, "%s.ogg", name);
    if (snd_localized_exists(localized)) return true;
    Msg("! [Rain] Sound is missing, disabling it: %s", localized);
    return false;
}

// Read the physical override first. FS.r_open alone cannot recover an
// unindexed .ltx, while .ogg/.xml have a special fallback in LocatorAPI.
static bool rain_read_config(LPCSTR path, std::string& bytes, bool& exists) {
    exists = false;
    const auto result = rain_config::read_loose_file(path, bytes);
    if (result == rain_config::ReadResult::loaded) {
        exists = true;
    } else if (result == rain_config::ReadResult::error) {
        Msg("! Weather editor: rain config disk read failed [%s], errno=%d", path, errno);
        return false;
    } else {
        if (!FS.exist(path)) return true;
        exists = true; // A packed config is still a valid source.
        IReader* reader = FS.r_open(path);
        if (!reader) {
            Msg("! Weather editor: rain config archive read failed [%s]", path);
            return false;
        }
        bytes.assign(static_cast<const char*>(reader->pointer()), reader->length());
        FS.r_close(reader);
    }
    if (bytes.size() > 0x7fffffffU) {
        Msg("! Weather editor: rain config is too large [%s]", path);
        return false;
    }
    return true;
}

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
	
    shared_str snd_name_2d = "ambient\\rain";
    shared_str snd_name_portal = "ambient\\rain";
    
    // Read extension switches from the merged global settings. Their source
    // file and include layout are intentionally irrelevant.
	
    if (pSettings && pSettings->section_exist("environment")) {
        if (pSettings->line_exist("environment", "enable_rain_material_sounds"))
            m_bEnableMaterialSounds =
                pSettings->r_bool("environment", "enable_rain_material_sounds");

        if (pSettings->line_exist("environment", "enable_dynamic_rain_wind"))
            m_bEnableDynamicWind =
                pSettings->r_bool("environment", "enable_dynamic_rain_wind");
    }

    string_path wex_path;
    FS.update_path(wex_path, "$game_config$", "environment\\noirWeatherEffect.ltx");
    std::string config_bytes;
    bool config_exists = false;
    if (rain_read_config(wex_path, config_bytes, config_exists) && config_exists) {
        IReader config_reader(config_bytes.data(), static_cast<int>(config_bytes.size()));
        string_path config_directory;
        FS.update_path(config_directory, "$game_config$", "environment\\");
        CInifile wex_ini(&config_reader, config_directory);
        
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
                    if (!rain_sound_available(value)) continue;
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

        if (wex_ini.section_exist("dynamic_wind")) {
            if (wex_ini.line_exist("dynamic_wind", "max_angle"))
                m_fWindMaxAngle = wex_ini.r_float("dynamic_wind", "max_angle");
            if (wex_ini.line_exist("dynamic_wind", "speed_wind_multiplier")) {
                SetWindSpeedMultiplier(wex_ini.r_float("dynamic_wind", "speed_wind_multiplier"));
                m_bHasSpeedConfig = true;
            }
            if (wex_ini.line_exist("dynamic_wind", "tilt_multiplier")) {
                const float configured_tilt = wex_ini.r_float("dynamic_wind", "tilt_multiplier");
                SetWindTiltMultiplier(configured_tilt);
                // Persist the corrected limit on Save even if the user did not touch the slider.
                m_bHasTiltConfig = configured_tilt == m_fWindTiltMultiplier;
            }
        }
        if (!xr::valid(m_fWindMaxAngle)) m_fWindMaxAngle = 10.f;
        clamp(m_fWindMaxAngle, 0.f, 80.f);
        if (!xr::valid(m_fWindSpeedMultiplier)) m_fWindSpeedMultiplier = 0.f;
        clamp(m_fWindSpeedMultiplier, 0.f, 10.f);
    } else {
        Msg("! [Noir Engine]: [Rain] Cannot load noirWeatherEffect.ltx, using default values.");
        m_bEnableMaterialSounds = false;
        m_bEnableDynamicWind = false;
    }

    m_fSavedWindTiltMultiplier = m_fWindTiltMultiplier;
    m_fSavedWindSpeedMultiplier = m_fWindSpeedMultiplier;

    // Створюємо звуки ПІСЛЯ того, як зчитали їх імена з конфігу
	
    if (rain_sound_available(snd_name_2d.c_str()))
        snd_Ambient2D.create(snd_name_2d.c_str(), st_Effect, sg_Undefined);
    if (rain_sound_available(snd_name_portal.c_str()))
        snd_AmbientPortal.create(snd_name_portal.c_str(), st_Effect, sg_Undefined);

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

void CEffect_Rain::SetWindTiltMultiplier(float value) {
    value = weather_motion::rain_tilt_multiplier(value);
    if (value == m_fWindTiltMultiplier) return;
    m_fWindTiltMultiplier = value;
    RefreshTrajectories();
}

void CEffect_Rain::SetWindSpeedMultiplier(float value) {
    if (!xr::valid(value)) value = 0.f;
    clamp(value, 0.f, 10.f);
    if (value == m_fWindSpeedMultiplier) return;
    m_fWindSpeedMultiplier = value;
    RefreshTrajectories();
}

void CEffect_Rain::InvalidateDrops() {
    // Rebuild trajectories and collision hits together on the next render.
    for (u32 id = 0; id < drops.capacity; ++id) {
        drops.invalidate(id);
        drops.uv_set[id] |= 2; // Do not play an obsolete trajectory hit.
    }
}

bool CEffect_Rain::SaveWindSettings() {
    const bool save_tilt = !m_bHasTiltConfig || m_fWindTiltMultiplier != m_fSavedWindTiltMultiplier;
    const bool save_speed = !m_bHasSpeedConfig || m_fWindSpeedMultiplier != m_fSavedWindSpeedMultiplier;
    if (!save_tilt && !save_speed) return true;
    string_path path;
    FS.update_path(path, "$game_config$", "environment\\noirWeatherEffect.ltx");
    std::string text;
    bool has_original = false;
    if (!rain_read_config(path, text, has_original)) return false;
    const std::string original = text;
    string32 value;
    if (save_tilt) {
        xr_sprintf(value, "%.6f", m_fWindTiltMultiplier);
        text = rain_config::set_tilt_multiplier(text, value);
    }
    if (save_speed) {
        xr_sprintf(value, "%.6f", m_fWindSpeedMultiplier);
        text = rain_config::set_dynamic_wind_value(text, "speed_wind_multiplier", value);
    }

    string_path temporary, backup;
    xr_strcpy(temporary, path);
    xr_strcat(temporary, ".weather_editor.tmp");
    createPath(temporary);
    const char* stage = nullptr;
    if (!rain_config::write_verified_file(temporary, text, stage)) {
        Msg("! Weather editor: rain config %s failed [%s], errno=%d", stage, temporary, errno);
        return false;
    }
    xr_strcpy(backup, path);
    xr_strcat(backup, ".weather_editor.bak");
    if (has_original && _access(backup, 0) != 0) {
        if (!rain_config::write_verified_file(backup, original, stage)) {
            Msg("! Weather editor: rain backup %s failed [%s], errno=%d", stage, backup, errno);
            return false;
        }
    }
    const DWORD attributes = GetFileAttributesA(path);
    const bool read_only = attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_READONLY);
    if (read_only && !SetFileAttributesA(path, attributes & ~FILE_ATTRIBUTE_READONLY)) {
        Msg("! Weather editor: cannot make rain config writable [%s], win_error=%lu", path, GetLastError());
        return false;
    }
    // Replace without unlinking the original first. A failed replacement keeps it intact.
    if (!MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        if (read_only) SetFileAttributesA(path, attributes);
        Msg("! Weather editor: rain config replacement failed [%s], win_error=%lu", path, error);
        return false;
    }
    // Native writes must be made visible to subsequent CInifile/VFS reads.
    string_path directory;
    FS.update_path(directory, "$game_config$", "environment\\");
    FS.rescan_path(directory, FALSE);
    Msg("* Weather editor: saved rain settings [%s]", path);
    m_fSavedWindTiltMultiplier = m_fWindTiltMultiplier;
    m_fSavedWindSpeedMultiplier = m_fWindSpeedMultiplier;
    m_bHasTiltConfig = true;
    m_bHasSpeedConfig = true;
    return true;
}

void CEffect_Rain::Born(u32 id, float radius) {
    Fvector axis;
    axis.set(0, -1, 0);

    if (m_bEnableDynamicWind) {
        const CEnvironment& environment = g_pGamePersistent->Environment();
        const float k = weather_motion::rain_wind_influence(
            environment.CurrentEnv->wind_velocity, environment.wind_strength_factor);
        const float tilt = deg2rad(std::min(80.f, m_fWindMaxAngle * k * m_fWindTiltMultiplier));
        const float heading = environment.CurrentEnv->wind_direction;
        // Match the (+sin, +cos) horizontal wind used by FTreeVisual.
        axis.set(std::sin(heading) * std::sin(tilt), -std::cos(tilt),
                 std::cos(heading) * std::sin(tilt));

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

    // The disk is defined at camera height, then projected back to the top
    // plane along this drop's direction. Use ray distance, not vertical height.
    const float top_distance = weather_motion::rain_path_length(source_offset, D.y);
    drops.px[id] = x + view.x - D.x * top_distance;
    drops.py[id] = source_offset + view.y;
    drops.pz[id] = z + view.z - D.z * top_distance;

    float height = weather_motion::rain_path_length(max_distance, D.y);
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

void CEffect_Rain::RenewItem(u32 id, float distance, BOOL bHit, u16 material_idx) {
    drops.uv_set[id] = Random.randI(2) | (bHit ? 0u : 2u);
    drops.material_idx[id] = material_idx;
    const float speed = std::max(1.f, drops.speed[id]);
    const u32 flight_ms = static_cast<u32>(std::max(1, iCeil(1000.f * std::max(0.f, distance) / speed)));
    const u32 trail_ms = static_cast<u32>(std::max(1, iCeil(1000.f * drop_length / speed)));
    const u32 arrival = Device.dwTimeGlobal + flight_ms;
    drops.dwTime_Hit[id] = bHit ? arrival : u32(-1);
    drops.dwTime_Life[id] = arrival + (bHit ? trail_ms : 0u);
    drops.hit_x[id] = drops.px[id] + drops.dx[id] * distance;
    drops.hit_y[id] = drops.py[id] + drops.dy[id] * distance;
    drops.hit_z[id] = drops.pz[id] + drops.dz[id] * distance;
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

    // Environment::OnFrame has already mixed the newly edited wind. Rebuild
    // after that mix, rather than spawning with the previous frame's direction.
    if (m_bRefreshTrajectories) {
        InvalidateDrops();
        m_bRefreshTrajectories = false;
    }
    float factor = g_pGamePersistent->Environment().CurrentEnv->rain_density;

#ifndef _EDITOR
    m_uHitSoundsFrame = 0; 
    CObject* E = g_pGameLevel->CurrentViewEntity();
    
    static float current_openness = 1.0f;
    static Fvector current_snd_dir = {0.1f, 1.0f, 0.1f};
    
    float target_volume_mod = 0.05f; 
    float target_openness = 1.0f;
    Fvector target_dir = {0.f, 0.f, 0.f};

    // The screen raindrops post-process uses the same solid-roof test as the
    // rain audio system. Keep it closed until a valid view entity is present.
	
    m_fViewRainExposure = 0.f;

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
            const float current_ray_range = i == 0 ? 50.f : ray_range;

            if (g_pGameLevel->ObjectSpace.RayPick(start_pos, dirs[i], current_ray_range,
                                                  collide::rqtStatic, RQ, E)) {
                if (!RQ.O) { 
                    CDB::TRI* T = g_pGameLevel->ObjectSpace.GetStaticTris() + RQ.element;
                    SGameMtl* mtl = GMLib.GetMaterialByIdx(T->material);
                    
                    // Ігноруємо кущі/листя для розрахунку оклюзії
					
                    if (mtl && !mtl->Flags.test(SGameMtl::flPassable) && 
                        !strstr(mtl->m_Name.c_str(), "bush") && !strstr(mtl->m_Name.c_str(), "leaves")) {
                        bHitSolid = true;
                        if (i == 0) {
                            roof_material = T->material;
                            m_fViewRainExposure = 0.f;
                        }
                    }
                }
            }

            if (i == 0 && !bHitSolid)
                m_fViewRainExposure = 1.f;
            
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
        if (snd_Ambient2D._handle()) snd_Ambient2D.play(0, sm_Looped);
        if (snd_AmbientPortal._handle()) snd_AmbientPortal.play(0, sm_Looped);
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
