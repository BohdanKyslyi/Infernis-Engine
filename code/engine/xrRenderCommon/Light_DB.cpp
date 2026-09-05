#include "StdAfx.h"
#include "xrEngine/_d3d_extensions.h"
#include "xrEngine/xrLevel.h"
#include "xrEngine/igame_persistent.h"
#include "xrEngine/environment.h"
#include "xrLC_Light/R_light.h"
#include "light_db.h"

void CLight_DB::Load(IReader* fs) {
    sun_original = nullptr;
    sun_adapted = nullptr;

    // МАКСИМАЛЬНА БЕЗПЕКА: if-init захищає від вильотів на битих рівнях
    if (IReader* F = fs->open_chunk(fsL_LIGHT_DYNAMIC)) {
        const u32 size = F->length();
        const u32 element = sizeof(Flight) + 4;
        const u32 count = size / element;
        VERIFY(count * element == size);
        
        v_static.reserve(count);
        
        for (u32 i = 0; i < count; ++i) {
            Flight Ldata;
            auto* L = Create();
            L->flags.bStatic = true;
            L->set_type(IRender_Light::POINT);

#if RENDER == R_R1
            L->set_shadow(false);
#else
            L->set_shadow(true);
#endif
            u32 controller = 0;
            F->r(&controller, 4);
            F->r(&Ldata, sizeof(Flight));
            
            if (Ldata.type == D3DLIGHT_DIRECTIONAL) {
                // Швидка агрегатна ініціалізація C++11
                Fvector tmp_R{ 1.f, 0.f, 0.f };

                // directional (base)
                sun_original = L;
                L->set_type(IRender_Light::DIRECT);
                L->set_shadow(true);
                L->set_rotation(Ldata.direction, tmp_R);

                // copy to env-sun
                sun_adapted = L = Create();
                L->flags.bStatic = true;
                L->set_type(IRender_Light::DIRECT);
                L->set_shadow(true);
                L->set_rotation(Ldata.direction, tmp_R);
            } else {
                Fvector tmp_D{ 0.f, 0.f, -1.f }; 
                Fvector tmp_R{ 1.f, 0.f, 0.f };  

                // point
                v_static.push_back(L);
                L->set_position(Ldata.position);
                L->set_rotation(tmp_D, tmp_R);
                L->set_range(Ldata.range);
                L->set_color(Ldata.diffuse);
                L->set_active(true);
            }
        }
        F->close();
    }
    R_ASSERT2(sun_original && sun_adapted, "Where is sun?");
}

#if RENDER != R_R1
void CLight_DB::LoadHemi() {
    string_path fn_game;
    if (FS.exist(fn_game, "$level$", "build.lights")) {
        if (IReader* F = FS.r_open(fn_game)) {
            if (IReader* chunk = F->open_chunk(1)) { // Hemispheric light chunk
                const u32 size = chunk->length();
                const u32 element = sizeof(R_Light);
                const u32 count = size / element;
                VERIFY(count * element == size);
                
                v_hemi.reserve(count);
                
                for (u32 i = 0; i < count; ++i) {
                    R_Light Ldata;
                    chunk->r(&Ldata, sizeof(R_Light));

                    if (Ldata.type == D3DLIGHT_POINT) {
                        auto* L = Create();
                        L->flags.bStatic = true;
                        L->set_type(IRender_Light::POINT);

                        Fvector tmp_D{ 0.f, 0.f, -1.f };
                        Fvector tmp_R{ 1.f, 0.f, 0.f };

                        v_hemi.push_back(L);
                        L->set_position(Ldata.position);
                        L->set_rotation(tmp_D, tmp_R);
                        L->set_range(Ldata.range);
                        L->set_color(Ldata.diffuse.x, Ldata.diffuse.y, Ldata.diffuse.z);
                        L->set_active(true);
                        L->set_attenuation_params(Ldata.attenuation0, Ldata.attenuation1, Ldata.attenuation2, Ldata.falloff);
                        L->spatial.type = STYPE_LIGHTSOURCEHEMI;
                    }
                }
                chunk->close();
            }
            FS.r_close(F);
        }
    }
}
#endif

void CLight_DB::Unload() {
    v_static.clear();
    v_hemi.clear();
    sun_original.destroy();
    sun_adapted.destroy();
}

light* CLight_DB::Create() {
    auto* L = xr_new<light>();
    L->flags.bStatic = false;
    L->flags.bActive = false;
    L->flags.bShadow = true;
    return L;
}

#if RENDER == R_R1
void CLight_DB::add_light(light* L) {
    if (Device.dwFrame == L->frame_render) return;
    L->frame_render = Device.dwFrame;
    
    if (L->flags.bStatic) return; 
    
    if (ps_r1_flags.test(R1FLAG_DLIGHTS))
        RImplementation.L_Dynamic->add(L);
}
#endif

#if (RENDER == R_R2) || (RENDER == R_R3) || (RENDER == R_R4)
void CLight_DB::add_light(light* L) {
    if (Device.dwFrame == L->frame_render) return;
    L->frame_render = Device.dwFrame;
    
    if (RImplementation.o.noshadows)
        L->flags.bShadow = FALSE;
        
    if (L->flags.bStatic && !ps_r2_ls_flags.test(R2FLAG_R1LIGHTS))
        return;
        
    L->export_(package);
}
#endif

void CLight_DB::Update() {
    // set sun params
    if (sun_original && sun_adapted) {
        auto* _sun_original = static_cast<light*>(sun_original._get());
        auto* _sun_adapted = static_cast<light*>(sun_adapted._get());
        CEnvDescriptor& E = *g_pGamePersistent->Environment().CurrentEnv;
        
        VERIFY(xr::valid(E.sun_dir));
        VERIFY2(E.sun_dir.y < 0.f, "Invalid sun direction settings in environment-config");

        Fvector OD, OP, AD, AP;
        OD.set(E.sun_dir).normalize();
        OP.mad(Device.vCameraPosition, OD, -500.f);
        
        AD.set(0.f, -0.75f, 0.f).add(E.sun_dir);

        // fix for rare upward sun directions
        int counter = 0;
        while (AD.magnitude() < 0.001f && counter < 10) {
            AD.add(E.sun_dir);
            counter++;
        }
        AD.normalize();
        AP.mad(Device.vCameraPosition, AD, -500.f);
        
        sun_original->set_rotation(OD, _sun_original->right);
        sun_original->set_position(OP);
        sun_original->set_color(E.sun_color.x, E.sun_color.y, E.sun_color.z);
        sun_original->set_range(600.f);
        
        sun_adapted->set_rotation(AD, _sun_adapted->right);
        sun_adapted->set_position(AP);
        sun_adapted->set_color(
            E.sun_color.x * ps_r2_sun_lumscale,
            E.sun_color.y * ps_r2_sun_lumscale,
            E.sun_color.z * ps_r2_sun_lumscale
        );
        sun_adapted->set_range(600.f);

        if (!::Render->is_sun_static()) {
            sun_adapted->set_rotation(OD, _sun_original->right);
            sun_adapted->set_position(OP);
        }
    }

    // Clear selection
    package.clear();
}