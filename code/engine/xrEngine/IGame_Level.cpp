#include "stdafx.h"
#include "igame_level.h"
#include "igame_persistent.h"

#include "x_ray.h"
#include "std_classes.h"
#include "customHUD.h"
#include "render.h"
#include "gamefont.h"
#include "xrLevel.h"
#include "CameraManager.h"
#include "environment.h"
#include "xr_object.h"
#include "feel_sound.h"
#include <algorithm>

ENGINE_API IGame_Level* g_pGameLevel = NULL;
extern BOOL g_bLoaded;

IGame_Level::IGame_Level() {
    m_pCameras = xr_new<CCameraManager>(true);
    g_pGameLevel = this;
    pLevel = NULL;
    bReady = false;
    pCurrentEntity = NULL;
    pCurrentViewEntity = NULL;
    Device.DumpResourcesMemoryUsage();
}

//#include "resourcemanager.h"

IGame_Level::~IGame_Level() {
    if (strstr(Core.Params, "-nes_texture_storing"))
        // Device.Resources->StoreNecessaryTextures();
        Device.m_pRender->ResourcesStoreNecessaryTextures();
    xr_delete(pLevel);

    // Render-level unload
    Render->level_Unload();
    xr_delete(m_pCameras);
    // Unregister
    Device.seqRender.Remove(this);
    Device.seqFrame.Remove(this);
    CCameraManager::ResetPP();
    ///////////////////////////////////////////
    Sound->set_geometry_occ(NULL);
    Sound->set_acoustic_obstacles(nullptr, 0);
    Sound->set_handler(NULL);
    Device.DumpResourcesMemoryUsage();

    u32 m_base = 0, c_base = 0, m_lmaps = 0, c_lmaps = 0;
    if (Device.m_pRender)
        Device.m_pRender->ResourcesGetMemoryUsage(m_base, c_base, m_lmaps, c_lmaps);

    Msg("* [ D3D ]: textures[%d K]", (m_base + m_lmaps) / 1024);
}

void IGame_Level::net_Stop() {
    for (int i = 0; i < 6; i++)
        Objects.Update(false);
    // Destroy all objects
    Objects.Unload();
    IR_Release();

    bReady = false;
    Sound->set_acoustic_obstacles(nullptr, 0);
}

//-------------------------------------------------------------------------------------------
// extern CStatTimer				tscreate;
void __stdcall _sound_event(ref_sound_data_ptr S, float range) {
    if (g_pGameLevel && S && S->feedback)
        g_pGameLevel->SoundEvent_Register(S, range);
}
static void __stdcall build_callback(Fvector* V, int Vcnt, CDB::TRI* T, int Tcnt, void* params) {
    g_pGameLevel->Load_GameSpecific_CFORM(T, Tcnt);
}

BOOL IGame_Level::Load(u32 dwNum) {
    // Initialize level data
    pApp->Level_Set(dwNum);
    string_path temp;
    if (!FS.exist(temp, "$level$", "level.ltx"))
        Debug.fatal(DEBUG_INFO, "Can't find level configuration file '%s'.", temp);
    pLevel = xr_new<CInifile>(temp);

    // Open
    //	g_pGamePersistent->LoadTitle	("st_opening_stream");
    g_pGamePersistent->LoadTitle();
    IReader* LL_Stream = FS.r_open("$level$", "level");
    IReader& fs = *LL_Stream;

    // Header
    hdrLEVEL H;
    fs.r_chunk_safe(fsL_HEADER, &H, sizeof(H));
    R_ASSERT2(XRCL_PRODUCTION_VERSION == H.XRLC_version, "Incompatible level version.");

    // CForms
    //	g_pGamePersistent->LoadTitle	("st_loading_cform");
    g_pGamePersistent->LoadTitle();
    ObjectSpace.Load(build_callback);
    // Sound->set_geometry_occ		( &Static );
    Sound->set_geometry_occ(ObjectSpace.GetStaticModel());
    Sound->set_handler(_sound_event);

    pApp->LoadSwitch();

    // HUD + Environment
    if (!g_hud)
        g_hud = (CCustomHUD*)NEW_INSTANCE(CLSID_HUDMANAGER);

    // Render-level Load
    Render->level_Load(LL_Stream);
    // tscreate.FrameEnd			();
    // Msg						("* S-CREATE: %f ms, %d
    // times",tscreate.result,tscreate.count);

    // Objects
    g_pGamePersistent->Environment().mods_load();
    R_ASSERT(Load_GameSpecific_Before());
    Objects.Load();
    //. ANDY	R_ASSERT					(Load_GameSpecific_After ());

    // Done
    FS.r_close(LL_Stream);
    bReady = true;
    IR_Capture();

    Device.seqRender.Add(this);
    Device.seqFrame.Add(this);

    return TRUE;
}

void IGame_Level::OnRender() {
//	if (_abs(Device.fTimeDelta)<EPS_S) return;

#ifdef _GPA_ENABLED
    TAL_ID rtID = TAL_MakeID(1, Core.dwFrame, 0);
    TAL_CreateID(rtID);
    TAL_BeginNamedVirtualTaskWithID("GameRenderFrame", rtID);
    TAL_Parami("Frame#", Device.dwFrame);
    TAL_EndVirtualTask();
#endif // _GPA_ENABLED

    // Level render, only when no client output required
    // Render the optic before the main HUD so its texture belongs to this frame.
    // Keep the render targets at screen size until the separate-resolution path is ready.
    const float lens_fov = ScopeLensFov();
    Device.scopeLensActive = false;
    if (lens_fov >= 5.f && lens_fov <= 90.f) {
        const float main_fov = Device.fFOV;
        const Fmatrix main_project = Device.mProject;
        const Fmatrix main_full = Device.mFullTransform;
        const Fmatrix main_inverse = Device.mInvFullTransform;

        Device.scopeLensPass = true;
        Device.fFOV = lens_fov;
        Device.mProject.build_projection(deg2rad(lens_fov), Device.fASPECT, VIEWPORT_NEAR,
            g_pGamePersistent->Environment().CurrentEnv->far_plane);
        Device.mFullTransform.mul(Device.mProject, Device.mView);
        Device.mInvFullTransform.invert(Device.mFullTransform);
        Device.m_pRender->SetCacheXform(Device.mView, Device.mProject);
        Render->Calculate();
        Render->Render();
        Device.scopeLensActive = Render->CaptureScopeLens();

        Device.scopeLensPass = false;
        Device.fFOV = main_fov;
        Device.mProject = main_project;
        Device.mFullTransform = main_full;
        Device.mInvFullTransform = main_inverse;
        Device.m_pRender->SetCacheXform(Device.mView, Device.mProject);
    }
    Render->Calculate();
    Render->Render();
    Device.scopeLensActive = false;

#ifdef _GPA_ENABLED
    TAL_RetireID(rtID);
#endif // _GPA_ENABLED

    // Font
    //	pApp->pFontSystem->SetSizeI(0.023f);
    //	pApp->pFontSystem->OnRender	();
}

void IGame_Level::OnFrame() {
    // Log				("- level:on-frame: ",u32(Device.dwFrame));
    //	if (_abs(Device.fTimeDelta)<EPS_S) return;

    // Update all objects
    VERIFY(bReady);
    Objects.Update(false);
    // Copy only coarse collision proxies on the game thread. The sound thread
    // never walks the changing object list or touches collision forms.
    if ((Device.dwFrame % 8) == 0) {
        xr_vector<SAcousticObstacle> obstacles;
        if (psSoundAcoustics) {
            obstacles.reserve(96);
            for (u32 i = 0; i < Objects.o_count() &&
                 (psSoundAcousticsPreset == 1 || obstacles.size() < 96); ++i) {
                CObject* object = Objects.o_get_by_iterator(i);
                if (!object || !object->getEnabled() || !object->Visual() ||
                    (psSoundAcousticsPreset == 1 && !object->acoustic_obstacle_active()) ||
                    object == pCurrentEntity) continue;
                const float radius = object->Radius();
                if (radius < 0.35f || radius > 8.f ||
                    object->Position().distance_to_sqr(Device.vCameraPosition) > 1600.f) continue;
                SAcousticObstacle obstacle;
                obstacle.position = object->Position();
                obstacle.radius = radius;
                obstacle.object = object;
                obstacle.shape = 0;
                obstacle.half_extent.set(radius, radius, radius);
                obstacle.axes[0].set(1.f, 0.f, 0.f);
                obstacle.axes[1].set(0.f, 1.f, 0.f);
                obstacle.axes[2].set(0.f, 0.f, 1.f);
                obstacle.material = object->acoustic_material();
                if (psSoundAcousticsPreset == 1) {
                    Fobb door_box;
                    if (object->acoustic_door_box(door_box)) {
                        obstacle.shape = 4;
                        obstacle.position = door_box.m_translate;
                        obstacle.half_extent = door_box.m_halfsize;
                        obstacle.axes[0] = door_box.m_rotate.i;
                        obstacle.axes[1] = door_box.m_rotate.j;
                        obstacle.axes[2] = door_box.m_rotate.k;
                        if (!obstacle.material) obstacle.material = 2;
                    } else {
                        Fbox world_box;
                        world_box.xform(object->BoundingBox(), object->XFORM());
                        if (!world_box.is_valid()) continue;
                        world_box.getcenter(obstacle.position);
                        Fvector dimensions;
                        dimensions.sub(world_box.max, world_box.min);
                        obstacle.half_extent.set(
                            std::max(0.15f, dimensions.x * 0.5f),
                            std::max(0.15f, dimensions.y * 0.5f),
                            std::max(0.15f, dimensions.z * 0.5f));
                        const float horizontal = std::max(obstacle.half_extent.x, obstacle.half_extent.z);
                        obstacle.shape = obstacle.half_extent.y > horizontal * 1.5f ? 1 :
                            (obstacle.half_extent.y < horizontal * 0.7f ? 3 : 2);
                    }
                    // Optional LTX material override for modded doors, panes and props.
                    const shared_str section = object->cNameSect();
                    if (section.size() && pSettings->line_exist(section.c_str(), "acoustic_material")) {
                        LPCSTR material = pSettings->r_string(section.c_str(), "acoustic_material");
                        if (!_stricmp(material, "glass")) obstacle.material = 1;
                        else if (!_stricmp(material, "wood")) obstacle.material = 2;
                        else if (!_stricmp(material, "metal")) obstacle.material = 3;
                        else if (!_stricmp(material, "fabric")) obstacle.material = 4;
                        else obstacle.material = 0;
                    }
                }
                obstacles.push_back(obstacle);
            }
            if (psSoundAcousticsPreset == 1 && obstacles.size() > 96) {
                const Fvector listener = Device.vCameraPosition;
                std::partial_sort(obstacles.begin(), obstacles.begin() + 96, obstacles.end(),
                    [&listener](const SAcousticObstacle& a, const SAcousticObstacle& b) {
                        const float a_distance = a.position.distance_to_sqr(listener) *
                            (a.shape == 4 ? 0.5f : (a.material == 1 ? 0.8f : 1.f));
                        const float b_distance = b.position.distance_to_sqr(listener) *
                            (b.shape == 4 ? 0.5f : (b.material == 1 ? 0.8f : 1.f));
                        return a_distance < b_distance;
                    });
                obstacles.resize(96);
            }
        }
        Sound->set_acoustic_obstacles(obstacles.empty() ? nullptr : obstacles.data(), obstacles.size());
    }
    g_hud->OnFrame();

    // Ambience
    if (Sounds_Random.size() && (Device.dwTimeGlobal > Sounds_Random_dwNextTime)) {
        Sounds_Random_dwNextTime = Device.dwTimeGlobal + ::Random.randI(10000, 20000);
        Fvector pos;
        pos.random_dir().normalize().mul(::Random.randF(30, 100)).add(Device.vCameraPosition);
        int id = ::Random.randI(Sounds_Random.size());
        if (Sounds_Random_Enabled) {
            Sounds_Random[id].play_at_pos(0, pos, 0);
            Sounds_Random[id].set_volume(1.f);
            Sounds_Random[id].set_range(10, 200);
        }
    }
}
// ==================================================================================================

void CServerInfo::AddItem(LPCSTR name_, LPCSTR value_, u32 color_) {
    shared_str s_name(name_);
    AddItem(s_name, value_, color_);
}

void CServerInfo::AddItem(shared_str& name_, LPCSTR value_, u32 color_) {
    SItem_ServerInfo it;
    //	shared_str s_name = CStringTable().translate( name_ );

    //	xr_strcpy( it.name, s_name.c_str() );
    xr_strcpy(it.name, name_.c_str());
    xr_strcat(it.name, " = ");
    xr_strcat(it.name, value_);
    it.color = color_;

    if (data.size() < max_item) {
        data.push_back(it);
    }
}

void IGame_Level::SetEntity(CObject* O) {
    if (pCurrentEntity)
        pCurrentEntity->On_LostEntity();

    if (O)
        O->On_SetEntity();

    pCurrentEntity = pCurrentViewEntity = O;
}

void IGame_Level::SetViewEntity(CObject* O) {
    if (pCurrentViewEntity)
        pCurrentViewEntity->On_LostEntity();

    if (O)
        O->On_SetEntity();

    pCurrentViewEntity = O;
}

void IGame_Level::SoundEvent_Register(ref_sound_data_ptr S, float range) {
    if (!g_bLoaded)
        return;
    if (!S)
        return;
    if (S->g_object && S->g_object->getDestroy()) {
        S->g_object = 0;
        return;
    }
    if (0 == S->feedback)
        return;

    clamp(range, 0.1f, 500.f);

    const CSound_params* p = S->feedback->get_params();
    Fvector snd_position = p->position;
    if (S->feedback->is_2D()) {
        snd_position.add(Sound->listener_position());
    }

    VERIFY(p && xr::valid(range));
    range = std::min(range, p->max_ai_distance);
    VERIFY(xr::valid(snd_position));
    VERIFY(xr::valid(p->max_ai_distance));
    VERIFY(xr::valid(p->volume));

    // Query objects
    Fvector bb_size = { range, range, range };
    g_SpatialSpace->q_box(snd_ER, 0, STYPE_REACTTOSOUND, snd_position, bb_size);

    // Iterate
    xr_vector<ISpatial*>::iterator it = snd_ER.begin();
    xr_vector<ISpatial*>::iterator end = snd_ER.end();
    for (; it != end; it++) {
        Feel::Sound* L = (*it)->dcast_FeelSound();
        if (0 == L)
            continue;
        CObject* CO = (*it)->dcast_CObject();
        VERIFY(CO);
        if (CO->getDestroy())
            continue;

        // Energy and signal
        VERIFY(xr::valid((*it)->spatial.sphere.P));
        float dist = snd_position.distance_to((*it)->spatial.sphere.P);
        if (dist > p->max_ai_distance)
            continue;
        VERIFY(xr::valid(dist));
        VERIFY2(!fis_zero(p->max_ai_distance), S->handle->file_name());
        float Power = (1.f - dist / p->max_ai_distance) * p->volume;
        VERIFY(xr::valid(Power));
        if (Power > EPS_S) {
            float occ = Sound->get_occlusion_to((*it)->spatial.sphere.P, snd_position);
            VERIFY(xr::valid(occ));
            Power *= occ;
            if (Power > EPS_S) {
                _esound_delegate D = { L, S, Power };
                snd_Events.push_back(D);
            }
        }
    }
    snd_ER.clear();
}

void IGame_Level::SoundEvent_Dispatch() {
    while (!snd_Events.empty()) {
        _esound_delegate& D = snd_Events.back();
        VERIFY(D.dest && D.source);
        if (D.source->feedback) {
            D.dest->feel_sound_new(D.source->g_object, D.source->g_type, D.source->g_userdata,

                                   D.source->feedback->is_2D()
                                       ? Device.vCameraPosition
                                       : D.source->feedback->get_params()->position,
                                   D.power);
        }
        snd_Events.pop_back();
    }
}

// Lain: added
void IGame_Level::SoundEvent_OnDestDestroy(Feel::Sound* obj) {
    struct rem_pred {
        rem_pred(Feel::Sound* obj) : m_obj(obj) {}

        bool operator()(const _esound_delegate& d) { return d.dest == m_obj; }

    private:
        Feel::Sound* m_obj;
    };

    snd_Events.erase(std::remove_if(snd_Events.begin(), snd_Events.end(), rem_pred(obj)),
                     snd_Events.end());
}
