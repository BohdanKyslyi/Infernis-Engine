// BlenderDefault.cpp: implementation of the CBlender_BmmD class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#include "blender_BmmD.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CBlender_BmmD::CBlender_BmmD() {
    description.CLS = B_BmmD;
    xr_strcpy(oT2_Name, "$null");
    xr_strcpy(oT2_xform, "$null");
    description.version = 3;
    xr_strcpy(oR_Name, "detail\\detail_grnd_grass");   //"$null");
    xr_strcpy(oG_Name, "detail\\detail_grnd_asphalt"); //"$null");
    xr_strcpy(oB_Name, "detail\\detail_grnd_earth");   //"$null");
    xr_strcpy(oA_Name, "detail\\detail_grnd_yantar");  //"$null");
}

CBlender_BmmD::~CBlender_BmmD() {}

void CBlender_BmmD::Save(IWriter& fs) {
    IBlender::Save(fs);
    xrPWRITE_MARKER(fs, "Detail map");
    xrPWRITE_PROP(fs, "Name", xrPID_TEXTURE, oT2_Name);
    xrPWRITE_PROP(fs, "Transform", xrPID_MATRIX, oT2_xform);
    xrPWRITE_PROP(fs, "R2-R", xrPID_TEXTURE, oR_Name);
    xrPWRITE_PROP(fs, "R2-G", xrPID_TEXTURE, oG_Name);
    xrPWRITE_PROP(fs, "R2-B", xrPID_TEXTURE, oB_Name);
    xrPWRITE_PROP(fs, "R2-A", xrPID_TEXTURE, oA_Name);
}

void CBlender_BmmD::Load(IReader& fs, u16 version) {
    IBlender::Load(fs, version);
    if (version < 3) {
        xrPREAD_MARKER(fs);
        xrPREAD_PROP(fs, xrPID_TEXTURE, oT2_Name);
        xrPREAD_PROP(fs, xrPID_MATRIX, oT2_xform);
    } else {
        xrPREAD_MARKER(fs);
        xrPREAD_PROP(fs, xrPID_TEXTURE, oT2_Name);
        xrPREAD_PROP(fs, xrPID_MATRIX, oT2_xform);
        xrPREAD_PROP(fs, xrPID_TEXTURE, oR_Name);
        xrPREAD_PROP(fs, xrPID_TEXTURE, oG_Name);
        xrPREAD_PROP(fs, xrPID_TEXTURE, oB_Name);
        xrPREAD_PROP(fs, xrPID_TEXTURE, oA_Name);
    }
}

#if RENDER == R_R1
//////////////////////////////////////////////////////////////////////////
// R1
//////////////////////////////////////////////////////////////////////////
void CBlender_BmmD::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);
    if (C.bEditor) {
        C.PassBegin();
        {
            C.PassSET_ZB(TRUE, TRUE);
            C.PassSET_Blend_SET();
            C.PassSET_LightFog(TRUE, TRUE);

            // Stage1 - Base texture
            C.StageBegin();
            C.StageSET_Color(D3DTA_TEXTURE, D3DTOP_MODULATE, D3DTA_DIFFUSE);
            C.StageSET_Alpha(D3DTA_TEXTURE, D3DTOP_MODULATE, D3DTA_DIFFUSE);
            C.StageSET_TMC(oT_Name, oT_xform, "$null", 0);
            C.StageEnd();

            // Stage2 - Second texture
            C.StageBegin();
            C.StageSET_Color(D3DTA_TEXTURE, D3DTOP_MODULATE2X, D3DTA_CURRENT);
            C.StageSET_Alpha(D3DTA_TEXTURE, D3DTOP_SELECTARG2, D3DTA_CURRENT);
            C.StageSET_TMC(oT2_Name, oT2_xform, "$null", 0);
            C.StageEnd();
        }
        C.PassEnd();
    } else {
        if (C.L_textures.size() < 2)
            Debug.fatal(DEBUG_INFO, "Not enought textures for shader, base tex: %s",
                        *C.L_textures[0]);
        switch (C.iElement) {
        case SE_R1_NORMAL_HQ:
            C.r_Pass("impl_dt", "impl_dt", TRUE);
            C.r_Sampler("s_base", C.L_textures[0]);
            C.r_Sampler("s_lmap", C.L_textures[1]);
            C.r_Sampler("s_detail", oT2_Name);
            C.r_End();
            break;
        case SE_R1_NORMAL_LQ:
            C.r_Pass("impl_dt", "impl_dt", TRUE);
            C.r_Sampler("s_base", C.L_textures[0]);
            C.r_Sampler("s_lmap", C.L_textures[1]);
            C.r_Sampler("s_detail", oT2_Name);
            C.r_End();
            break;
        case SE_R1_LPOINT:
            C.r_Pass("impl_point", "add_point", FALSE, TRUE, FALSE, TRUE, D3DBLEND_ONE,
                     D3DBLEND_ONE, TRUE);
            C.r_Sampler("s_base", C.L_textures[0]);
            C.r_Sampler_clf("s_lmap", TEX_POINT_ATT);
            C.r_Sampler_clf("s_att", TEX_POINT_ATT);
            C.r_End();
            break;
        case SE_R1_LSPOT:
            C.r_Pass("impl_spot", "add_spot", FALSE, TRUE, FALSE, TRUE, D3DBLEND_ONE, D3DBLEND_ONE,
                     TRUE);
            C.r_Sampler("s_base", C.L_textures[0]);
            C.r_Sampler_clf("s_lmap", "internal\\internal_light_att", true);
            C.r_Sampler_clf("s_att", TEX_SPOT_ATT);
            C.r_End();
            break;
        case SE_R1_LMODELS:
            C.r_Pass("impl_l", "impl_l", FALSE);
            C.r_Sampler("s_base", C.L_textures[0]);
            C.r_Sampler("s_lmap", C.L_textures[1]);
            C.r_End();
            break;
        }
    }
}
#elif RENDER == R_R2

//////////////////////////////////////////////////////////////////////////
// R2
//////////////////////////////////////////////////////////////////////////
#include "uber_deffer.h"
void CBlender_BmmD::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);
    // codepath is the same, only the shaders differ
    // ***only pixel shaders differ***
    string256 mask;
    strconcat(sizeof(mask), mask, C.L_textures[0].c_str(), "_mask");
    switch (C.iElement) {
    case SE_R2_NORMAL_HQ: // deffer
        uber_deffer(C, true, "impl", "impl", false, oT2_Name[0] ? oT2_Name : 0, true);
        C.r_Sampler("s_mask", mask);
        C.r_Sampler("s_lmap", C.L_textures[1]);

        C.r_Sampler("s_dt_r", oR_Name, false, D3DTADDRESS_WRAP, D3DTEXF_ANISOTROPIC, D3DTEXF_LINEAR,
                    D3DTEXF_ANISOTROPIC);
        C.r_Sampler("s_dt_g", oG_Name, false, D3DTADDRESS_WRAP, D3DTEXF_ANISOTROPIC, D3DTEXF_LINEAR,
                    D3DTEXF_ANISOTROPIC);
        C.r_Sampler("s_dt_b", oB_Name, false, D3DTADDRESS_WRAP, D3DTEXF_ANISOTROPIC, D3DTEXF_LINEAR,
                    D3DTEXF_ANISOTROPIC);
        C.r_Sampler("s_dt_a", oA_Name, false, D3DTADDRESS_WRAP, D3DTEXF_ANISOTROPIC, D3DTEXF_LINEAR,
                    D3DTEXF_ANISOTROPIC);

        C.r_Sampler("s_dn_r", strconcat(sizeof(mask), mask, oR_Name, "_bump"));
        C.r_Sampler("s_dn_g", strconcat(sizeof(mask), mask, oG_Name, "_bump"));
        C.r_Sampler("s_dn_b", strconcat(sizeof(mask), mask, oB_Name, "_bump"));
        C.r_Sampler("s_dn_a", strconcat(sizeof(mask), mask, oA_Name, "_bump"));

        C.r_End();
        break;
    case SE_R2_NORMAL_LQ: // deffer
        uber_deffer(C, false, "base", "impl", false, oT2_Name[0] ? oT2_Name : 0, true);
        C.r_Sampler("s_lmap", C.L_textures[1]);
        C.r_End();
        break;
    case SE_R2_SHADOW: // smap
        if (RImplementation.o.HW_smap)
            C.r_Pass("shadow_direct_base", "dumb", FALSE, TRUE, TRUE, FALSE);
        else
            C.r_Pass("shadow_direct_base", "shadow_direct_base", FALSE);
        C.r_Sampler("s_base", C.L_textures[0]);
        C.r_End();
        break;
    }
}
#else
//////////////////////////////////////////////////////////////////////////
// R3
//////////////////////////////////////////////////////////////////////////
#include "uber_deffer.h"
void CBlender_BmmD::Compile(CBlender_Compile& C) {
    IBlender::Compile(C);
    // codepath is the same, only the shaders differ
    // ***only pixel shaders differ***
    //
    // Screen Space Shaders - ground relief. Ported from Monolith Blender_BmmD.cpp:213-397.
    // This blender is the terrain surface, and SSFX replaces it with three shaders of its own,
    // one per distance band: terrain_high right in front of the player, terrain_mid further out,
    // terrain_low at the horizon. rimp_select_sh_static in r4.cpp picks the band, and it finds
    // the terrain by the isLandscape flag set below - which is why the flag is cleared here on
    // every compile and only raised on the SSFX paths.
    //
    // Deviation from the donor: Monolith also carries a terrain depth-prepass ( its z_prepass
    // local, R2FLAG_TERRAIN_PREPASS, the bLandscape flag and an eighth uber_deffer argument ).
    // This engine has no such feature, and with the prepass switched off Monolith's code is the
    // code below line for line, so nothing of SSFX depends on it.
    C.SH->flags.isLandscape = FALSE;
    string256 mask;
    strconcat(sizeof(mask), mask, C.L_textures[0].c_str(), "_mask");
    switch (C.iElement) {
    case SE_R2_NORMAL_HQ: // deffer
#if RENDER == R_R4
        // The near band. Monolith Blender_BmmD.cpp:233-243.
        if (RImplementation.o.ssfx_terrain) {
            C.SH->flags.isLandscape = TRUE;
            uber_deffer(C, true, "terrain", "terrain_high", false, oT2_Name[0] ? oT2_Name : 0, true);
        } else
#endif
        {
            uber_deffer(C, true, "impl", "impl", false, oT2_Name[0] ? oT2_Name : 0, true);
        }
        // C.r_Sampler		("s_mask",	mask);
        // C.r_Sampler		("s_lmap",	C.L_textures[1]);

        // C.r_Sampler		("s_dt_r",	oR_Name,	false,	D3DTADDRESS_WRAP,
        // D3DTEXF_ANISOTROPIC,D3DTEXF_LINEAR,	D3DTEXF_ANISOTROPIC);
        // C.r_Sampler		("s_dt_g",	oG_Name,	false,	D3DTADDRESS_WRAP,
        // D3DTEXF_ANISOTROPIC,D3DTEXF_LINEAR,	D3DTEXF_ANISOTROPIC);
        // C.r_Sampler		("s_dt_b",	oB_Name,	false,	D3DTADDRESS_WRAP,
        // D3DTEXF_ANISOTROPIC,D3DTEXF_LINEAR,	D3DTEXF_ANISOTROPIC);
        // C.r_Sampler		("s_dt_a",	oA_Name,	false,	D3DTADDRESS_WRAP,
        // D3DTEXF_ANISOTROPIC,D3DTEXF_LINEAR,	D3DTEXF_ANISOTROPIC);

        // C.r_Sampler		("s_dn_r",	strconcat(sizeof(mask),mask,oR_Name,"_bump")	);
        // C.r_Sampler		("s_dn_g",	strconcat(sizeof(mask),mask,oG_Name,"_bump") );
        // C.r_Sampler		("s_dn_b",	strconcat(sizeof(mask),mask,oB_Name,"_bump") );
        // C.r_Sampler		("s_dn_a",	strconcat(sizeof(mask),mask,oA_Name,"_bump") );

        C.r_dx10Texture("s_mask", mask);
        C.r_dx10Texture("s_lmap", C.L_textures[1]);

        C.r_dx10Texture("s_dt_r", oR_Name);
        C.r_dx10Texture("s_dt_g", oG_Name);
        C.r_dx10Texture("s_dt_b", oB_Name);
        C.r_dx10Texture("s_dt_a", oA_Name);

        C.r_dx10Texture("s_dn_r", strconcat(sizeof(mask), mask, oR_Name, "_bump"));
        C.r_dx10Texture("s_dn_g", strconcat(sizeof(mask), mask, oG_Name, "_bump"));
        C.r_dx10Texture("s_dn_b", strconcat(sizeof(mask), mask, oB_Name, "_bump"));
        C.r_dx10Texture("s_dn_a", strconcat(sizeof(mask), mask, oA_Name, "_bump"));

#if RENDER == R_R4
        // The depth of the ground relief, one map per splatting channel. Monolith
        // Blender_BmmD.cpp:272-280, line for line.
        if (RImplementation.o.ssfx_terrain) {
            C.r_dx10Texture("s_height_r", strconcat(sizeof(mask), mask, oR_Name, "_height"));
            C.r_dx10Texture("s_height_g", strconcat(sizeof(mask), mask, oG_Name, "_height"));
            C.r_dx10Texture("s_height_b", strconcat(sizeof(mask), mask, oB_Name, "_height"));
            C.r_dx10Texture("s_height_a", strconcat(sizeof(mask), mask, oA_Name, "_height"));
        }
#endif

        // Screen Space Shaders - puddles on terrain. Ported from Monolith Blender_BmmD.cpp:282-285.
        // Bound unconditionally, exactly as the donor binds them: the vanilla "impl" shader has no
        // such samplers declared, so r_dx10Texture finds nothing to fill and the call is a no-op.
        C.r_dx10Texture("s_puddles_normal", "fx\\water_normal");
        C.r_dx10Texture("s_puddles_perlin", "fx\\puddles_perlin");
        C.r_dx10Texture("s_puddles_mask", strconcat(sizeof(mask), mask, C.L_textures[0].c_str(), "_puddles_mask"));
        C.r_dx10Texture("s_rainsplash", "fx\\water_sbumpvolume");

        C.r_dx10Sampler("smp_base");
        C.r_dx10Sampler("smp_linear");

        C.r_Stencil(TRUE, D3DCMP_ALWAYS, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
                    D3DSTENCILOP_KEEP);
        C.r_StencilRef(0x01);

        C.r_End();
        break;
    case SE_R2_NORMAL_LQ: // deffer
#if RENDER == R_R4
        // The middle band. Monolith Blender_BmmD.cpp:304-315.
        if (RImplementation.o.ssfx_terrain) {
            C.SH->flags.isLandscape = TRUE;
            uber_deffer(C, false, "base", "terrain_mid", false, oT2_Name[0] ? oT2_Name : 0, true);
        } else
#endif
        {
            uber_deffer(C, false, "base", "impl", false, oT2_Name[0] ? oT2_Name : 0, true);
        }

        // C.r_Sampler		("s_lmap",	C.L_textures[1]);
        // C.r_dx10Texture("s_lmap", C.L_textures[1]);

        C.r_dx10Texture("s_mask", mask);

#if RENDER == R_R4
        // Monolith Blender_BmmD.cpp:326-340. deffer_terrain_mid_flat.ps samples the splatting mask
        // and one flattened texture that stands in for all four detail layers at this distance.
        // The per-level file is optional and the donor names the shared one when it is absent.
        if (RImplementation.o.ssfx_terrain) {
            LPSTR LodTexture =
                strconcat(sizeof(mask), mask, C.L_textures[0].c_str(), "_lod_textures");
            string_path fn;
            if (FS.exist(fn, "$game_textures$", LodTexture, ".dds"))
                C.r_dx10Texture("s_lod_texture", LodTexture);
            else
                C.r_dx10Texture("s_lod_texture", "terrain\\default_lod_textures");
        }
#endif

        // C.r_dx10Texture("s_lmap", C.L_textures[1]);
        C.r_dx10Sampler("smp_base");
        C.r_dx10Sampler("smp_linear");

        C.r_Stencil(TRUE, D3DCMP_ALWAYS, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
                    D3DSTENCILOP_KEEP);
        C.r_StencilRef(0x01);

        C.r_End();
        break;
#if RENDER == R_R4
    case 3: // Screen Space Shaders - the far band. Monolith Blender_BmmD.cpp:353-374.
        //
        // Element 3 is free here: the R4 element ids stop at SE_R2_SHADOW == 2 ( r2_types.h ), and
        // ResourceManager.cpp compiles element 3 for every blender already, which until now left an
        // empty element behind. Only this blender answers to it, so only the terrain gains a band.
        //
        // The band's shaders ( deffer_terrain_low_* ) exist only with SSS on. With SSS off the
        // element stays empty, as it was before: rimp_select_sh_static ( r4.cpp ) picks it only
        // for a terrain whose element 0 raised isLandscape, which never happens with SSS off.
        if (RImplementation.o.ssfx_terrain) {
            C.SH->flags.isLandscape = TRUE;

            uber_deffer(C, false, "base", "terrain_low", false, oT2_Name[0] ? oT2_Name : 0, true);

            C.r_dx10Sampler("smp_linear");

            C.r_Stencil(TRUE, D3DCMP_ALWAYS, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE,
                        D3DSTENCILOP_KEEP);
            C.r_StencilRef(0x01);

            C.r_End();
        }
        break;
#endif
    case SE_R2_SHADOW: // smap
        // if (RImplementation.o.HW_smap)	C.r_Pass	("shadow_direct_base","dumb",
        // FALSE,TRUE,TRUE,FALSE);  else							C.r_Pass
        // ("shadow_direct_base","shadow_direct_base",FALSE);
#if RENDER == R_R4
        // Monolith Blender_BmmD.cpp:379-389. The terrain shadow vertex shader lowers the geometry
        // by 0.1 so the shadow does not float above ground the parallax has pushed down.
        if (RImplementation.o.ssfx_terrain)
            C.r_Pass("shadow_direct_terrain", "dumb", FALSE, TRUE, TRUE, FALSE);
        else
#endif
            C.r_Pass("shadow_direct_base", "dumb", FALSE, TRUE, TRUE, FALSE);
        // C.r_Sampler		("s_base",C.L_textures[0]);
        C.r_dx10Texture("s_base", C.L_textures[0]);
        C.r_dx10Sampler("smp_base");
        C.r_dx10Sampler("smp_linear");
        C.r_ColorWriteEnable(false, false, false, false);
        C.r_End();
        break;
    }
}
#endif
