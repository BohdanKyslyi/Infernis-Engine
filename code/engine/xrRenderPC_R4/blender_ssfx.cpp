#include "stdafx.h"
#pragma hdrstop

#include "blender_ssfx.h"

// Screen Space Shaders blenders, ported from Monolith.
// The one deliberate difference from the donor: Monolith writes the scene copy read by SSR as the
// literal "$user$generic_temp"; Infernis already owns that buffer under the name
// r2_RT_generic0_temp, so the macro is used instead of repeating the string.

CBlender_ssfx_ssr::CBlender_ssfx_ssr() { description.CLS = 0; }
CBlender_ssfx_ssr::~CBlender_ssfx_ssr() {}

void CBlender_ssfx_ssr::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);

    switch (C.iElement) {
    case 0: // Do SSR
        C.r_Pass("stub_screen_space", "ssfx_ssr", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("ssr_image", r2_RT_ssfx_ssr); // Prev Frame
        C.r_dx10Texture("s_rimage", r2_RT_generic0_temp);
        C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);
        C.r_dx10Texture("s_prev_pos", r2_RT_ssfx_prevPos);
        C.r_dx10Texture("s_gloss_data", r2_RT_ssfx_temp3);

        C.r_dx10Texture("blue_noise", "fx\\blue_noise");

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_linear");

        C.r_End();
        break;

    case 1: // Blur Phase 1
        C.r_Pass("stub_screen_space", "ssfx_ssr_blur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("ssr_image", r2_RT_ssfx);
        C.r_dx10Texture("s_diffuse", r2_RT_albedo);

        C.r_dx10Sampler("smp_nofilter");
        C.r_End();
        break;

    case 2: // Blur Phase 2
        C.r_Pass("stub_screen_space", "ssfx_ssr_blur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("ssr_image", r2_RT_ssfx_temp);
        C.r_dx10Texture("s_diffuse", r2_RT_albedo);

        C.r_dx10Sampler("smp_nofilter");
        C.r_End();
        break;

    case 3: // Combine
        C.r_Pass("stub_screen_space", "ssfx_ssr_combine", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_rimage", r2_RT_generic0_temp);

        C.r_dx10Texture("ssr_image", r2_RT_ssfx_temp2);
        C.r_dx10Texture("s_gloss_data", r2_RT_ssfx_temp3);

        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_nofilter");
        C.r_End();
        break;

    case 4: // No blur, straight into r2_RT_ssfx_temp2
        C.r_Pass("stub_screen_space", "ssfx_ssr_noblur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("ssr_image", r2_RT_ssfx);

        C.r_dx10Sampler("smp_nofilter");
        C.r_End();
        break;

    case 5: // Gloss
        C.r_Pass("stub_screen_space", "ssfx_ssr_gloss", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_diffuse", r2_RT_albedo);
        C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);

        C.r_dx10Texture("env_s0", r2_T_envs0);
        C.r_dx10Texture("env_s1", r2_T_envs1);
        C.r_dx10Texture("sky_s0", r2_T_sky0);
        C.r_dx10Texture("sky_s1", r2_T_sky1);

        C.r_dx10Sampler("smp_nofilter");
        C.r_End();
        break;
    }
}

CBlender_ssfx_volumetric_blur::CBlender_ssfx_volumetric_blur() { description.CLS = 0; }
CBlender_ssfx_volumetric_blur::~CBlender_ssfx_volumetric_blur() {}

void CBlender_ssfx_volumetric_blur::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);

    switch (C.iElement) {
    case 0: // Blur Phase 1
        C.r_Pass("stub_screen_space", "ssfx_volumetric_blur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("vol_buffer", r2_RT_ssfx_volumetric);

        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_End();
        break;

    case 1: // Blur Phase 2
        C.r_Pass("stub_screen_space", "ssfx_volumetric_blur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("vol_buffer", r2_RT_ssfx_volumetric_tmp);

        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_End();
        break;

    case 2: // Blur Phase 1
        C.r_Pass("stub_screen_space", "ssfx_volumetric_blur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("vol_buffer", r2_RT_generic2);

        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_End();
        break;

    case 3: // Blur Phase 2
        C.r_Pass("stub_screen_space", "ssfx_volumetric_blur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("vol_buffer", r2_RT_ssfx_accum);

        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_End();
        break;

    case 5: // Combine
        C.r_Pass("stub_screen_space", "ssfx_volumetric_combine", FALSE, FALSE, FALSE);

        C.r_dx10Texture("vol_buffer", r2_RT_generic2);
        C.r_dx10Texture("vol_point", r2_RT_ssfx_volumetric);

        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_End();
        break;
    }
}

CBlender_ssfx_ao::CBlender_ssfx_ao() { description.CLS = 0; }
CBlender_ssfx_ao::~CBlender_ssfx_ao() {}

void CBlender_ssfx_ao::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);

    switch (C.iElement) {
    case 0: // AO
        C.r_Pass("stub_screen_space", "ssfx_ao", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);
        C.r_dx10Texture("ssfx_ao", r2_RT_ssfx_ao);

        C.r_dx10Texture("s_prev_pos", r2_RT_ssfx_prevPos);

        C.r_dx10Texture("jitter0", JITTER(0));

        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_jitter");

        C.r_End();
        break;

    case 1: // Blur Phase 1
        C.r_Pass("stub_screen_space", "ssfx_ao_blur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("ao_image", r2_RT_ssfx_temp);
        C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);
        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_End();
        break;

    case 2: // Blur Phase 2
        C.r_Pass("stub_screen_space", "ssfx_ao_blur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("ao_image", r2_RT_ssfx_temp3);
        C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);
        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_End();
        break;

    case 3: // IL
        C.r_Pass("stub_screen_space", "ssfx_il", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_accumulator", r2_RT_accum);
        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);

        C.r_dx10Texture("ssfx_ao", r2_RT_ssfx_il);

        C.r_dx10Texture("s_prev_pos", r2_RT_ssfx_prevPos);

        C.r_dx10Texture("jitter0", JITTER(0));

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_jitter");

        C.r_End();
        break;

    case 4: // IL Blur Phase 1
        C.r_Pass("stub_screen_space", "ssfx_il_blur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("ao_image", r2_RT_ssfx_temp2);
        C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);

        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_End();
        break;

    case 5: // IL Blur Phase 2
        C.r_Pass("stub_screen_space", "ssfx_il_blur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("ao_image", r2_RT_ssfx_temp3);
        C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);

        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_End();
        break;
    }
}

CBlender_ssfx_sss::CBlender_ssfx_sss() { description.CLS = 0; }
CBlender_ssfx_sss::~CBlender_ssfx_sss() {}

void CBlender_ssfx_sss::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);

    switch (C.iElement) {
    case 0: // SSS
        C.r_Pass("stub_screen_space", "ssfx_sss", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);
        C.r_dx10Texture("sss_image", r2_RT_ssfx_sss);

        C.r_dx10Texture("s_prev_pos", r2_RT_ssfx_prevPos);

        C.r_dx10Texture("jitter0", JITTER(0));

        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_jitter");

        C.r_End();
        break;

    case 1: // SSS Blur Occ Mask - Phase 1
        C.r_Pass("stub_screen_space", "ssfx_sss_blur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("sss_image", r2_RT_ssfx);

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_jitter");

        C.r_End();
        break;

    case 2: // SSS Blur Occ Mask - Phase 2
        C.r_Pass("stub_screen_space", "ssfx_sss_blur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("sss_image", r2_RT_ssfx_temp);

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_jitter");

        C.r_End();
        break;
    }
}

CBlender_ssfx_sss_ext::CBlender_ssfx_sss_ext() { description.CLS = 0; }
CBlender_ssfx_sss_ext::~CBlender_ssfx_sss_ext() {}

void CBlender_ssfx_sss_ext::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);

    switch (C.iElement) {
    case 0: // SSS Ext, slots 0..3
        C.r_Pass("stub_screen_space", "ssfx_sss_ext", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);
        C.r_dx10Texture("sss_image", r2_RT_ssfx_sss_ext);

        C.r_dx10Texture("s_prev_pos", r2_RT_ssfx_prevPos);

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_jitter");

        C.r_End();
        break;

    case 1: // SSS Ext, slots 4..7
        C.r_Pass("stub_screen_space", "ssfx_sss_ext", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);
        C.r_dx10Texture("sss_image", r2_RT_ssfx_sss_ext2);

        C.r_dx10Texture("s_prev_pos", r2_RT_ssfx_prevPos);

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_jitter");

        C.r_End();
        break;

    case 2: // SSS Ext Combine
        C.r_Pass("stub_screen_space", "ssfx_sss_ext_combine", FALSE, FALSE, FALSE);

        C.r_dx10Texture("sss_image_1", r2_RT_ssfx_sss_ext);
        C.r_dx10Texture("sss_image_2", r2_RT_ssfx_sss_ext2);

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");

        C.r_End();
        break;
    }
}

CBlender_ssfx_rain::CBlender_ssfx_rain() { description.CLS = 0; }
CBlender_ssfx_rain::~CBlender_ssfx_rain() {}

void CBlender_ssfx_rain::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);

    switch (C.iElement) {
    case 0:
        C.r_Pass("stub_screen_space", "ssfx_rain", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("ssfx_color_buffer", r2_RT_generic0);
        C.r_dx10Texture("volumetric_buffer", r2_RT_ssfx_volumetric);
        C.r_dx10Texture("bloom_buffer", r2_RT_ssfx_bloom1);

        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");

        C.r_End();
        break;

    case 1: // MSAA
        C.r_Pass("stub_screen_space", "ssfx_rain", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("ssfx_color_buffer", r2_RT_generic0_r);
        C.r_dx10Texture("volumetric_buffer", r2_RT_ssfx_volumetric);
        C.r_dx10Texture("bloom_buffer", r2_RT_ssfx_bloom1);

        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");

        C.r_End();
        break;
    }
}

CBlender_ssfx_water_blur::CBlender_ssfx_water_blur() { description.CLS = 0; }
CBlender_ssfx_water_blur::~CBlender_ssfx_water_blur() {}

void CBlender_ssfx_water_blur::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);

    switch (C.iElement) {
    case 0: // Blur Phase 1
        C.r_Pass("stub_screen_space", "ssfx_water_blur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("water_buffer", r2_RT_ssfx_temp);

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_End();
        break;

    case 1: // Blur Phase 2
        C.r_Pass("stub_screen_space", "ssfx_water_blur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("water_buffer", r2_RT_ssfx_temp2);

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_End();
        break;

    case 2: // No Blur
        C.r_Pass("stub_screen_space", "ssfx_water_noblur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("water_buffer", r2_RT_ssfx_temp2);

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_End();
        break;

    case 5: // Water Waves
        C.r_Pass("stub_screen_space", "ssfx_water_waves", FALSE, FALSE, FALSE);

        C.r_dx10Texture("water_waves", "fx\\water_height");

        C.r_dx10Sampler("smp_linear");
        C.r_End();
        break;
    }
}

CBlender_ssfx_motion_blur::CBlender_ssfx_motion_blur() { description.CLS = 0; }
CBlender_ssfx_motion_blur::~CBlender_ssfx_motion_blur() {}

void CBlender_ssfx_motion_blur::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);

    switch (C.iElement) {
    case 0:
        C.r_Pass("stub_screen_space", "ssfx_motion_blur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_image", r2_RT_generic0);

        C.r_dx10Texture("ssfx_motion_vectors", r2_RT_ssfx_motion_vectors);

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_End();
        break;

    case 1:
        C.r_Pass("stub_screen_space", "ssfx_motion_blur", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_image", r2_RT_ssfx_temp);

        C.r_dx10Texture("ssfx_motion_vectors", r2_RT_ssfx_motion_vectors);

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_linear");
        C.r_End();
        break;
    }
}

CBlender_ssfx_fog_scattering::CBlender_ssfx_fog_scattering() { description.CLS = 0; }
CBlender_ssfx_fog_scattering::~CBlender_ssfx_fog_scattering() {}

void CBlender_ssfx_fog_scattering::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);

    switch (C.iElement) {
    case 0:
        C.r_Pass("stub_screen_space", "ssfx_fog_scattering", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_image", r2_RT_generic0);
        C.r_dx10Texture("s_blur_2", r2_RT_blur_2);

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_End();
        break;

    case 2:
        C.r_Pass("stub_screen_space", "ssfx_fog_scattering_blur", FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_image", r2_RT_generic0);

        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;

    case 3:
        C.r_Pass("stub_screen_space", "ssfx_fog_scattering_blur", FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_image", r2_RT_blur_4);

        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;
    }
}

CBlender_ssfx_taa::CBlender_ssfx_taa() { description.CLS = 0; }
CBlender_ssfx_taa::~CBlender_ssfx_taa() {}

void CBlender_ssfx_taa::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);

    switch (C.iElement) {
    case 0: // Motion Vectors
        C.r_Pass("stub_screen_space", "ssfx_taa_prepare", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_ssfx_taa", r2_RT_ssfx_taa);
        C.r_dx10Texture("s_position", r2_RT_P);

        C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);

        C.r_dx10Sampler("smp_point");
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");

        C.r_End();
        break;

    case 1: // TAA
        C.r_Pass("stub_screen_space", "ssfx_taa", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_current", r2_RT_generic0);
        C.r_dx10Texture("s_previous", r2_RT_ssfx_prev_frame);

        C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_taa);
        C.r_dx10Texture("s_mv", r2_RT_ssfx_motion_vectors);

        C.r_dx10Texture("s_depth", r2_RT_P);
        C.r_dx10Texture("s_previous_depth", r2_RT_ssfx_prevPos);

        C.r_dx10Texture("s_ssfx_temp", r2_RT_ssfx_temp);

        C.r_dx10Sampler("smp_point");
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");

        C.r_End();
        break;

    case 2: // TAA Sharp
        C.r_Pass("stub_screen_space", "ssfx_taa_sharp", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_current", r2_RT_ssfx_prev_frame);

        C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);

        C.r_dx10Sampler("smp_point");
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");

        C.r_End();
        break;
    }
}

CBlender_ssfx_bloom_build::CBlender_ssfx_bloom_build() { description.CLS = 0; }
CBlender_ssfx_bloom_build::~CBlender_ssfx_bloom_build() {}

void CBlender_ssfx_bloom_build::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);

    switch (C.iElement) {
    case 0: // Bloom Build
        C.r_Pass("stub_screen_space", "ssfx_bloom", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_scene", r2_RT_blur_2);
        C.r_dx10Texture("s_emissive", r2_RT_ssfx_bloom_emissive);
        C.r_dx10Texture("s_hud_mask", r2_RT_ssfx_motion_vectors);

        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");

        C.r_End();
        break;

    case 1: // Combine ( Finish )
        C.r_Pass("stub_screen_space", "ssfx_bloom_combine", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_bloom", r2_RT_ssfx_bloom_tmp2);
        C.r_dx10Texture("s_emissive", r2_RT_ssfx_bloom_emissive);

        C.r_dx10Texture("s_ssfx_lensdirt", "fx\\lens_dirt");
        C.r_dx10Texture("s_starburst", "fx\\starburst");
        C.r_dx10Texture("s_bloom_lens", r2_RT_ssfx_bloom_lens);
        C.r_dx10Texture("s_lenscolors", "fx\\lens_colors");

        C.r_dx10Sampler("smp_point");
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");

        C.r_End();
        break;
    }
}

CBlender_ssfx_bloom_lens::CBlender_ssfx_bloom_lens() { description.CLS = 0; }
CBlender_ssfx_bloom_lens::~CBlender_ssfx_bloom_lens() {}

void CBlender_ssfx_bloom_lens::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);

    switch (C.iElement) {
    case 0: // Lens Build
        C.r_Pass("stub_screen_space", "ssfx_bloom_flares", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_emissive", r2_RT_ssfx_bloom_emissive);
        C.r_dx10Texture("s_lenscolors", "fx\\lens_colors");

        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");

        C.r_End();
        break;

    case 1: // Flares Blur
        C.r_Pass("stub_screen_space", "ssfx_bloom_upsample", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_bloom", r2_RT_ssfx_bloom_tmp4);
        C.r_dx10Texture("s_downsample", r2_RT_ssfx_bloom_tmp4);

        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");

        C.r_End();
        break;

    case 2: // Flares Blur
        C.r_Pass("stub_screen_space", "ssfx_bloom_upsample", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_bloom", r2_RT_ssfx_bloom_tmp4_2);
        C.r_dx10Texture("s_downsample", r2_RT_ssfx_bloom_tmp4_2);

        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");

        C.r_End();
        break;
    }
}

CBlender_ssfx_bloom_downsample::CBlender_ssfx_bloom_downsample() { description.CLS = 0; }
CBlender_ssfx_bloom_downsample::~CBlender_ssfx_bloom_downsample() {}

void CBlender_ssfx_bloom_downsample::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);

    switch (C.iElement) {
    case 0:
        C.r_Pass("stub_screen_space", "ssfx_bloom_downsample", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_bloom", r2_RT_ssfx_bloom1);
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");
        C.r_End();
        break;

    case 1:
        C.r_Pass("stub_screen_space", "ssfx_bloom_downsample", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_bloom", r2_RT_ssfx_bloom_tmp2);
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_nofilter");
        C.r_End();
        break;

    case 2:
        C.r_Pass("stub_screen_space", "ssfx_bloom_downsample", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_bloom", r2_RT_ssfx_bloom_tmp4);
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;

    case 3:
        C.r_Pass("stub_screen_space", "ssfx_bloom_downsample", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_bloom", r2_RT_ssfx_bloom_tmp8);
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;

    case 4:
        C.r_Pass("stub_screen_space", "ssfx_bloom_downsample", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_bloom", r2_RT_ssfx_bloom_tmp16);
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;

    case 5:
        C.r_Pass("stub_screen_space", "ssfx_bloom_downsample", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_bloom", r2_RT_ssfx_bloom_tmp32);
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;
    }
}

CBlender_ssfx_bloom_upsample::CBlender_ssfx_bloom_upsample() { description.CLS = 0; }
CBlender_ssfx_bloom_upsample::~CBlender_ssfx_bloom_upsample() {}

void CBlender_ssfx_bloom_upsample::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);

    switch (C.iElement) {
    case 0:
        C.r_Pass("stub_screen_space", "ssfx_bloom_upsample", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_downsample", r2_RT_ssfx_bloom_tmp64);
        C.r_dx10Texture("s_bloom", r2_RT_ssfx_bloom_tmp64);
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;

    case 1:
        C.r_Pass("stub_screen_space", "ssfx_bloom_upsample", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_downsample", r2_RT_ssfx_bloom_tmp32);
        C.r_dx10Texture("s_bloom", r2_RT_ssfx_bloom_tmp32_2);
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;

    case 2:
        C.r_Pass("stub_screen_space", "ssfx_bloom_upsample", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_downsample", r2_RT_ssfx_bloom_tmp16);
        C.r_dx10Texture("s_bloom", r2_RT_ssfx_bloom_tmp16_2);
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;

    case 3:
        C.r_Pass("stub_screen_space", "ssfx_bloom_upsample", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_downsample", r2_RT_ssfx_bloom_tmp8);
        C.r_dx10Texture("s_bloom", r2_RT_ssfx_bloom_tmp8_2);
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;

    case 4:
        C.r_Pass("stub_screen_space", "ssfx_bloom_upsample", FALSE, FALSE, FALSE);

        C.r_dx10Texture("s_downsample", r2_RT_ssfx_bloom_tmp4);
        C.r_dx10Texture("s_bloom", r2_RT_ssfx_bloom_tmp4_2);
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;
    }
}

// --------------------------------------------------------------------------------------------
// Monolith Layers\xrRenderPC_R4\blender_blur.cpp. Not an SSFX class, but SSFX depends on it: the
// six passes it builds fill the blur chain that the SSFX bloom ( s_scene ) and the SSFX depth of
// field ( s_blur_2 ) sample. pp_blur.ps only reads s_position and s_lut_atlas in its night
// vision branch, which this shader set does not compile, so those two lines bind nothing here;
// they stay because they are the donor's lines.
CBlender_blur::CBlender_blur() { description.CLS = 0; }
CBlender_blur::~CBlender_blur() {}

void CBlender_blur::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);

    switch (C.iElement) {
    case 0: // Fullres Horizontal
        C.r_Pass("stub_screen_space", "pp_blur", FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_image", r2_RT_generic0);
        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_lut_atlas", "shaders\\lut_atlas");

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;
    case 1: // Fullres Vertical
        C.r_Pass("stub_screen_space", "pp_blur", FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_image", r2_RT_blur_h_2);
        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_lut_atlas", "shaders\\lut_atlas");

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;
    case 2: // Halfres Horizontal
        C.r_Pass("stub_screen_space", "pp_blur", FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_image", r2_RT_generic0);
        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_lut_atlas", "shaders\\lut_atlas");

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;
    case 3: // Halfres Vertical
        C.r_Pass("stub_screen_space", "pp_blur", FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_image", r2_RT_blur_h_4);
        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_lut_atlas", "shaders\\lut_atlas");

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;
    case 4: // Quarterres Horizontal
        C.r_Pass("stub_screen_space", "pp_blur", FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_image", r2_RT_generic0);
        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_lut_atlas", "shaders\\lut_atlas");

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;
    case 5: // Quarterres Vertical
        C.r_Pass("stub_screen_space", "pp_blur", FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_image", r2_RT_blur_h_8);
        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_lut_atlas", "shaders\\lut_atlas");

        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;
    }
}

// --------------------------------------------------------------------------------------------
// Monolith Layers\xrRenderPC_R4\blender_dof.cpp. Element 0 runs depth_of_field.ps, which in this
// shader set is the SSFX weapon depth of field; element 1 copies its result back into the scene.
CBlender_dof::CBlender_dof() { description.CLS = 0; }
CBlender_dof::~CBlender_dof() {}

void CBlender_dof::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);

    switch (C.iElement) {
    case 0:
        C.r_Pass("stub_screen_space", "depth_of_field", FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_image", r2_RT_generic0);
        C.r_dx10Texture("s_blur_2", r2_RT_blur_2);
        C.r_dx10Sampler("smp_base");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;
    case 1:
        C.r_Pass("stub_screen_space", "post_processing", FALSE, FALSE, FALSE);
        C.r_dx10Texture("samplero_pepero", r2_RT_dof);
        C.r_dx10Sampler("smp_base");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
        break;
    }
}
