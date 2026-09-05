#include "StdAfx.h"
#include "light.h"
#include <algorithm> // Для std::clamp та std::max

static const float SQRT2 = 1.4142135623730950488016887242097f;
static const float RSQRTDIV2 = 0.70710678118654752440084436210485f;

light::light() : ISpatial(g_SpatialSpace) {
    spatial.type = STYPE_LIGHTSOURCE;
    flags.type = POINT;
    flags.bStatic = false;
    flags.bActive = false;
    flags.bShadow = false;
    flags.bVolumetric = false;
    flags.bHudMode = false;

#if (RENDER == R_R2) || (RENDER == R_R3) || (RENDER == R_R4)
    vis.frame2test = 0;
    vis.query_id = 0;
    vis.query_order = 0;
    vis.visible = true;
    vis.pending = false;
#endif // (RENDER==R_R2) || (RENDER==R_R3) || (RENDER==R_R4)
}

light::~light() {
#if (RENDER == R_R2) || (RENDER == R_R3) || (RENDER == R_R4)
    // C++11 Range-based for loop
    for (auto*& part : omnipart) {
        xr_delete(part);
    }
#endif // (RENDER==R_R2) || (RENDER==R_R3) || (RENDER==R_R4)
    
    set_active(false);

#if (RENDER == R_R2) || (RENDER == R_R3) || (RENDER == R_R4)
    // C++11 Range-based for loop для безпечного видалення з рендера
    for (auto*& l : RImplementation.Lights_LastFrame) {
        if (this == l) l = nullptr;
    }
#endif // (RENDER==R_R2) || (RENDER==R_R3) || (RENDER==R_R4)
}

#if (RENDER == R_R2) || (RENDER == R_R3) || (RENDER == R_R4)
void light::set_texture(LPCSTR name) {
    if (!name || !name[0]) {
        s_spot.destroy();
        s_point.destroy();
        s_volumetric.destroy();
        return;
    }

#pragma todo("Only shadowed spot implements projective texture")
    string256 temp;

    strconcat(sizeof(temp), temp, "r2\\accum_spot_", name);
    s_spot.create(RImplementation.Target->b_accum_spot, temp, name);

#if (RENDER != R_R3) && (RENDER != R_R4)
    s_volumetric.create("accum_volumetric", name);
#else 
    s_volumetric.create("accum_volumetric_nomsaa", name);
    if (RImplementation.o.dx10_msaa) {
        int bound = 1;
        if (!RImplementation.o.dx10_msaa_opt)
            bound = RImplementation.o.dx10_msaa_samples;

        for (int i = 0; i < bound; ++i) {
            s_spot_msaa[i].create(RImplementation.Target->b_accum_spot_msaa[i],
                                  strconcat(sizeof(temp), temp, "r2\\accum_spot_", name), name);
            s_volumetric_msaa[i].create(RImplementation.Target->b_accum_volumetric_msaa[i],
                                        strconcat(sizeof(temp), temp, "r2\\accum_volumetric_", name), name);
        }
    }
#endif // (RENDER!=R_R3) && (RENDER!=R_R4)
}
#endif

#if RENDER == R_R1
void light::set_texture(LPCSTR name) {}
#endif

void light::set_active(bool a) {
    if (a) {
        if (flags.bActive) return;
        flags.bActive = true;
        spatial_register();
        spatial_move();

#ifdef DEBUG
        Fvector zero = { 0.f, -1000.f, 0.f };
        if (position.similar(zero)) {
            Msg("- Uninitialized light position.");
        }
#endif // DEBUG
    } else {
        if (!flags.bActive) return;
        flags.bActive = false;
        spatial_move();
        spatial_unregister();
    }
}

void light::set_position(const Fvector& P) {
    const float eps = EPS_L; 
    if (position.similar(P, eps)) return;
    position.set(P);
    spatial_move();
}

void light::set_range(float R) {
    const float eps = std::max(range * 0.1f, EPS_L);
    if (fsimilar(range, R, eps)) return;
    range = R;
    spatial_move();
}

void light::set_cone(float angle) {
    if (fsimilar(cone, angle)) return;
    VERIFY(cone < deg2rad(121.f)); // 120 is hard limit for lights
    cone = angle;
    spatial_move();
}

void light::set_rotation(const Fvector& D, const Fvector& R) {
    Fvector old_D = direction;
    direction.normalize(D);
    right.normalize(R);
    if (!fsimilar(1.f, old_D.dotproduct(D)))
        spatial_move();
}

void light::spatial_move() {
    switch (flags.type) {
    case IRender_Light::REFLECTED:
    case IRender_Light::POINT: {
        spatial.sphere.set(position, range);
    } break;
    case IRender_Light::SPOT: {
        VERIFY2(cone < deg2rad(121.f), "Too large light-cone angle. Maybe you have passed it in 'degrees'?");
        if (cone >= PI_DIV_2) {
            // obtused-angled
            spatial.sphere.P.mad(position, direction, range);
            spatial.sphere.R = range * std::tan(cone * 0.5f);
        } else {
            // acute-angled
            spatial.sphere.R = range / (2.f * xr::sqr(std::cos(cone * 0.5f)));
            spatial.sphere.P.mad(position, direction, spatial.sphere.R);
        }
    } break;
    case IRender_Light::OMNIPART: {
        const float fSphereR = range * RSQRTDIV2;
        spatial.sphere.P.mad(position, direction, fSphereR);
        spatial.sphere.R = fSphereR;
    } break;
    }

    ISpatial::spatial_move();

#if (RENDER == R_R2) || (RENDER == R_R3) || (RENDER == R_R4)
    if (flags.bActive) gi_generate();
    svis.invalidate();
#endif // (RENDER==R_R2) || (RENDER==R_R3) || (RENDER==R_R4)
}

vis_data& light::get_homdata() {
    hom.sphere.set(spatial.sphere.P, spatial.sphere.R);
    hom.box.set(spatial.sphere.P, spatial.sphere.P);
    hom.box.grow(spatial.sphere.R);
    return hom;
}

Fvector light::spatial_sector_point() { return position; }

//////////////////////////////////////////////////////////////////////////
#if (RENDER == R_R2) || (RENDER == R_R3) || (RENDER == R_R4)
void light::xform_calc() {
    if (Device.dwFrame == m_xform_frame) return;
    m_xform_frame = Device.dwFrame;

    Fvector L_dir, L_up, L_right;

    L_dir.set(direction);
    const float l_dir_m = L_dir.magnitude();
    if (xr::valid(l_dir_m) && l_dir_m > EPS_S)
        L_dir.div(l_dir_m);
    else
        L_dir.set(0.f, 0.f, 1.f);

    if (right.square_magnitude() > EPS) {
        L_right.set(right);
        L_right.normalize();
        L_up.crossproduct(L_dir, L_right);
        L_up.normalize();
        L_right.crossproduct(L_up, L_dir);
        L_right.normalize();
    } else {
        L_up.set(0.f, 1.f, 0.f);
        if (xr::abs(L_up.dotproduct(L_dir)) > 0.99f)
            L_up.set(0.f, 0.f, 1.f);
        L_right.crossproduct(L_up, L_dir);
        L_right.normalize();
        L_up.crossproduct(L_dir, L_right);
        L_up.normalize();
    }

    Fmatrix mR;
    mR.i = L_right; mR._14 = 0.f;
    mR.j = L_up;    mR._24 = 0.f;
    mR.k = L_dir;   mR._34 = 0.f;
    mR.c = position;mR._44 = 1.f;

    switch (flags.type) {
    case IRender_Light::REFLECTED:
    case IRender_Light::POINT: {
        Fmatrix mScale;
        mScale.scale(range, range, range);
        m_xform.mul_43(mR, mScale);
    } break;
    case IRender_Light::SPOT: {
        const float s = 2.f * range * std::tan(cone * 0.5f);
        Fmatrix mScale;
        mScale.scale(s, s, range); 
        m_xform.mul_43(mR, mScale);
    } break;
    case IRender_Light::OMNIPART: {
        const float L_R = 2.f * range; 
        Fmatrix mScale;
        mScale.scale(L_R, L_R, L_R);
        m_xform.mul_43(mR, mScale);
    } break;
    default:
        m_xform.identity();
        break;
    }
}

static const Fvector cmNorm[6] = { {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, 0.f, 1.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f} };
static const Fvector cmDir[6]  = { {1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, -1.f} };

void light::export_(light_Package& package) {
    if (flags.bShadow) {
        switch (flags.type) {
        case IRender_Light::POINT: {
            if (!omnipart[0]) {
                for (auto*& part : omnipart) part = xr_new<light>();
            }
            
            for (int f = 0; f < 6; ++f) {
                light* L = omnipart[f];
                Fvector R;
                R.crossproduct(cmNorm[f], cmDir[f]);
                L->set_type(IRender_Light::OMNIPART);
                L->set_shadow(true);
                L->set_position(position);
                L->set_rotation(cmDir[f], R);
                L->set_cone(PI_DIV_2);
                L->set_range(range);
                L->set_color(color);
                L->spatial.sector = spatial.sector; 
                L->s_spot = s_spot;
                L->s_point = s_point;

#if (RENDER == R_R3) || (RENDER == R_R4)
                if (RImplementation.o.dx10_msaa) {
                    int bound = RImplementation.o.dx10_msaa_opt ? 1 : RImplementation.o.dx10_msaa_samples;
                    for (int i = 0; i < bound; ++i) {
                        L->s_point_msaa[i] = s_point_msaa[i];
                        L->s_spot_msaa[i] = s_spot_msaa[i];
                    }
                }
#endif // (RENDER==R_R3) || (RENDER==R_R4)

                L->set_volumetric(flags.bVolumetric);
                L->set_volumetric_quality(m_volumetric_quality);
                L->set_volumetric_intensity(m_volumetric_intensity);
                L->set_volumetric_distance(m_volumetric_distance);

                package.v_shadowed.push_back(L);
            }
        } break;
        case IRender_Light::SPOT:
            package.v_shadowed.push_back(this);
            break;
        }
    } else {
        switch (flags.type) {
        case IRender_Light::POINT:
            package.v_point.push_back(this);
            break;
        case IRender_Light::SPOT:
            package.v_spot.push_back(this);
            break;
        }
    }
}

void light::set_attenuation_params(float a0, float a1, float a2, float fo) {
    attenuation0 = a0;
    attenuation1 = a1;
    attenuation2 = a2;
    falloff = fo;
}
#endif // (RENDER==R_R2) || (RENDER==R_R3) || (RENDER==R_R4)

extern float r_ssaGLOD_start, r_ssaGLOD_end;
extern float ps_r2_slight_fade;

float light::get_LOD() const {
    if (!flags.bShadow) return 1.f;
    const float distSQ = Device.vCameraPosition.distance_to_sqr(spatial.sphere.P) + EPS;
    const float ssa = ps_r2_slight_fade * spatial.sphere.R / distSQ;
    
    // МАКСИМАЛЬНА ОПТИМІЗАЦІЯ: Безпечний std::clamp
    return std::sqrt(std::clamp((ssa - r_ssaGLOD_end) / (r_ssaGLOD_start - r_ssaGLOD_end), 0.f, 1.f));
}