#include "stdafx.h"
#include "igame_level.h"

//#include "xr_effgamma.h"
#include "x_ray.h"
#include "xr_ioconsole.h"
#include "xr_ioc_cmd.h"
#include "ssfx_console_vars.h"
#include "infernis_pbr_console_vars.h"
#include "shader_bus.h"
//#include "fbasicvisual.h"
#include "cameramanager.h"
#include "environment.h"
#include "xr_input.h"
#include "CustomHUD.h"

#include "xrRender/RenderDeviceRender.h"

#include "xr_object.h"

xr_token* vid_quality_token = nullptr;

xr_token vid_bpp_token[] = { { "16", 16 }, { "32", 32 }, { 0, 0 } };
//-----------------------------------------------------------------------

void IConsole_Command::add_to_LRU(std::string arg) {
    if (arg.empty() || bEmptyArgsHandled) {
        return;
    }

    const bool dup = std::find(m_LRU.begin(), m_LRU.end(), arg) != m_LRU.end();
    if (!dup) {
        m_LRU.push_back(std::move(arg));
        if (m_LRU.size() > LRU_MAX_COUNT) {
            m_LRU.erase(m_LRU.begin());
        }
    }
}

void IConsole_Command::add_LRU_to_tips(vecTips& tips) {
    tips.reserve(m_LRU.size());
    for (auto it_rb = m_LRU.rbegin(); it_rb != m_LRU.rend(); ++it_rb) {
        tips.push_back(*it_rb);
    }
}

// =======================================================

class CCC_Quit : public IConsole_Command {
public:
    CCC_Quit(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args) {
        //		TerminateProcess(GetCurrentProcess(),0);
        Console->Hide();
        Engine.Event.Defer("KERNEL:disconnect");
        Engine.Event.Defer("KERNEL:quit");
    }
};
//-----------------------------------------------------------------------

class CCC_DbgStrCheck : public IConsole_Command {
public:
    CCC_DbgStrCheck(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args) { g_pStringContainer->verify(); }
};

class CCC_DbgStrDump : public IConsole_Command {
public:
    CCC_DbgStrDump(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args) { g_pStringContainer->dump(); }
};

//-----------------------------------------------------------------------
class CCC_MotionsStat : public IConsole_Command {
public:
    CCC_MotionsStat(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args) {
        // g_pMotionsContainer->dump();
        //	TODO: move this console commant into renderer
        VERIFY(0);
    }
};
class CCC_TexturesStat : public IConsole_Command {
public:
    CCC_TexturesStat(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args) {
        Device.DumpResourcesMemoryUsage();
        // Device.Resources->_DumpMemoryUsage();
        //	TODO: move this console commant into renderer
        // VERIFY(0);
    }
};
//-----------------------------------------------------------------------
class CCC_E_Dump : public IConsole_Command {
public:
    CCC_E_Dump(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args) { Engine.Event.Dump(); }
};
class CCC_E_Signal : public IConsole_Command {
public:
    CCC_E_Signal(LPCSTR N) : IConsole_Command(N){};
    virtual void Execute(LPCSTR args) {
        char Event[128], Param[128];
        Event[0] = 0;
        Param[0] = 0;
        sscanf(args, "%[^,],%s", Event, Param);
        Engine.Event.Signal(Event, (u64)Param);
    }
};

//-----------------------------------------------------------------------
class CCC_Help : public IConsole_Command {
public:
    CCC_Help(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args) {
        Log("- --- Command listing: start ---");
        CConsole::vecCMD_IT it;
        for (it = Console->Commands.begin(); it != Console->Commands.end(); it++) {
            IConsole_Command& C = *(it->second);
            TStatus _S;
            C.Status(_S);
            TInfo _I;
            C.Info(_I);

            Msg("%-20s (%-10s) --- %s", C.Name(), _S, _I);
        }
        Log("Key: Ctrl + A         === Select all ");
        Log("Key: Ctrl + C         === Copy to clipboard ");
        Log("Key: Ctrl + V         === Paste from clipboard ");
        Log("Key: Ctrl + X         === Cut to clipboard ");
        Log("Key: Ctrl + Z         === Undo ");
        Log("Key: Ctrl + Insert    === Copy to clipboard ");
        Log("Key: Shift + Insert   === Paste from clipboard ");
        Log("Key: Shift + Delete   === Cut to clipboard ");
        Log("Key: Insert           === Toggle mode <Insert> ");
        Log("Key: Back / Delete          === Delete symbol left / right ");

        Log("Key: Up   / Down            === Prev / Next command in tips list ");
        Log("Key: Ctrl + Up / Ctrl + Down === Prev / Next executing command ");
        Log("Key: Left, Right, Home, End {+Shift/+Ctrl}       === Navigation in text ");
        Log("Key: PageUp / PageDown      === Scrolling history ");
        Log("Key: Tab  / Shift + Tab     === Next / Prev possible command from list");
        Log("Key: Enter  / NumEnter      === Execute current command ");

        Log("- --- Command listing: end ----");
    }
};

XRCORE_API void _dump_open_files(int mode);
class CCC_DumpOpenFiles : public IConsole_Command {
public:
    CCC_DumpOpenFiles(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = FALSE; };
    virtual void Execute(LPCSTR args) {
        int _mode = atoi(args);
        _dump_open_files(_mode);
    }
};

// Screen Space Shaders: the two profiles, see ssfx_profile_select below.
static bool ssfx_profile_named(LPCSTR args);
static bool ssfx_profile_keeps(LPCSTR name);
static void ssfx_profile_saved();

//-----------------------------------------------------------------------
class CCC_SaveCFG : public IConsole_Command {
public:
    CCC_SaveCFG(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args) {
        string_path cfg_full_name;
        // Screen Space Shaders: without a name, or with the name of one of the two profiles ( user,
        // user_sss - the user, 2026-10-27 ), this is the save of the profile in use, and it keeps
        // only what belongs to that profile ( ssfx_profile_keeps ). Any other name writes every
        // command, as before - the options page keeps its undo copy, tmp.ltx, that way.
        const bool profile_save = xr_strlen(args) == 0 || ssfx_profile_named(args);
        xr_strcpy(cfg_full_name, profile_save ? Console->ConfigFile : args);

        bool b_abs_name = xr_strlen(cfg_full_name) > 2 && cfg_full_name[1] == ':';

        if (!b_abs_name)
            FS.update_path(cfg_full_name, "$app_data_root$", cfg_full_name);

        if (strext(cfg_full_name))
            *strext(cfg_full_name) = 0;
        xr_strcat(cfg_full_name, ".ltx");

        BOOL b_allow = TRUE;
        if (FS.exist(cfg_full_name))
            b_allow = SetFileAttributes(cfg_full_name, FILE_ATTRIBUTE_NORMAL);

        if (b_allow) {
            IWriter* F = FS.w_open(cfg_full_name);
            CConsole::vecCMD_IT it;
            // The command list is sorted by name and the file is replayed top to bottom on the
            // next start, so of two commands that write the same variable the one written last
            // wins. The SSFX menu's controls are such commands - each is a view onto one float of
            // a vector that the author's own command carries whole - and a view does not always
            // print the bare number: a drop-down reads the variable as its nearest entry, a check
            // box as on or off. Written in a second pass they are replayed after the vectors, so
            // the game starts with exactly what the menu showed, wherever a vector's name sorts
            // ( "ssfx_water_setup1" falls after "ssfx_ui_water_*", "ssfx_pom" before it ).
            for (it = Console->Commands.begin(); it != Console->Commands.end(); it++)
                if (strncmp(it->second->Name(), "ssfx_ui_", 8) != 0 &&
                    (!profile_save || ssfx_profile_keeps(it->second->Name())))
                    it->second->Save(F);
            for (it = Console->Commands.begin(); it != Console->Commands.end(); it++)
                if (strncmp(it->second->Name(), "ssfx_ui_", 8) == 0 &&
                    (!profile_save || ssfx_profile_keeps(it->second->Name())))
                    it->second->Save(F);
            FS.w_close(F);
            Msg("Config-file [%s] saved successfully", cfg_full_name);
            if (profile_save)
                ssfx_profile_saved();
        } else
            Msg("!Cannot store config file [%s]", cfg_full_name);
    }
};
CCC_LoadCFG::CCC_LoadCFG(LPCSTR N) : IConsole_Command(N){};

void CCC_LoadCFG::Execute(LPCSTR args) {
    Msg("Executing config-script \"%s\"...", args);
    string_path cfg_name;

    xr_strcpy(cfg_name, args);
    if (strext(cfg_name))
        *strext(cfg_name) = 0;
    xr_strcat(cfg_name, ".ltx");

    string_path cfg_full_name;

    FS.update_path(cfg_full_name, "$app_data_root$", cfg_name);

    if (NULL == FS.exist(cfg_full_name))
        FS.update_path(cfg_full_name, "$fs_root$", cfg_name);

    if (NULL == FS.exist(cfg_full_name))
        xr_strcpy(cfg_full_name, cfg_name);

    IReader* F = FS.r_open(cfg_full_name);

    string1024 str;
    if (F != NULL) {
        while (!F->eof()) {
            F->r_string(str, sizeof(str));
            if (allow(str))
                Console->Execute(str);
        }
        FS.r_close(F);
        Msg("[%s] successfully loaded.", cfg_full_name);
    } else {
        Msg("! Cannot open script file [%s]", cfg_full_name);
    }
}

CCC_LoadCFG_custom::CCC_LoadCFG_custom(LPCSTR cmd) : CCC_LoadCFG(cmd) { xr_strcpy(m_cmd, cmd); };
bool CCC_LoadCFG_custom::allow(LPCSTR cmd) { return (cmd == strstr(cmd, m_cmd)); };

//-----------------------------------------------------------------------
class CCC_Start : public IConsole_Command {
    void parse(LPSTR dest, LPCSTR args, LPCSTR name) {
        dest[0] = 0;
        if (strstr(args, name))
            sscanf(strstr(args, name) + xr_strlen(name), "(%[^)])", dest);
    }

    void protect_Name_strlwr(LPSTR str) {
        string4096 out;
        xr_strcpy(out, sizeof(out), str);
        strlwr(str);

        LPCSTR name_str = "name=";
        LPCSTR name1 = strstr(str, name_str);
        if (!name1 || !xr_strlen(name1)) {
            return;
        }
        int begin_p = xr_strlen(str) - xr_strlen(name1) + xr_strlen(name_str);
        if (begin_p < 1) {
            return;
        }

        LPCSTR name2 = strchr(name1, '/');
        int end_p = xr_strlen(str) - ((name2) ? xr_strlen(name2) : 0);
        if (begin_p >= end_p) {
            return;
        }
        for (int i = begin_p; i < end_p; ++i) {
            str[i] = out[i];
        }
    }

public:
    CCC_Start(LPCSTR N) : IConsole_Command(N) { bLowerCaseArgs = false; };
    virtual void Execute(LPCSTR args) {
        /*		if (g_pGameLevel)	{
                                Log		("! Please disconnect/unload first");
                                return;
                        }
        */
        string4096 op_server, op_client, op_demo;
        op_server[0] = 0;
        op_client[0] = 0;

        parse(op_server, args, "server"); // 1. server
        parse(op_client, args, "client"); // 2. client
        parse(op_demo, args, "demo");     // 3. demo

        strlwr(op_server);
        protect_Name_strlwr(op_client);

        if (!op_client[0] && strstr(op_server, "single"))
            xr_strcpy(op_client, "localhost");

        if ((0 == xr_strlen(op_client)) && (0 == xr_strlen(op_demo))) {
            Log("! Can't start game without client. Arguments: '%s'.", args);
            return;
        }
        if (g_pGameLevel)
            Engine.Event.Defer("KERNEL:disconnect");

        if (xr_strlen(op_demo)) {
            Engine.Event.Defer("KERNEL:start_mp_demo", u64(xr_strdup(op_demo)), 0);
        } else {
            Engine.Event.Defer("KERNEL:start", u64(xr_strlen(op_server) ? xr_strdup(op_server) : 0),
                               u64(xr_strdup(op_client)));
        }
    }
};

class CCC_Disconnect : public IConsole_Command {
public:
    CCC_Disconnect(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args) { Engine.Event.Defer("KERNEL:disconnect"); }
};
//-----------------------------------------------------------------------
class CCC_VID_Reset : public IConsole_Command {
public:
    CCC_VID_Reset(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args) {
        if (Device.b_is_Ready) {
            Device.Reset();
        }
    }
};
class CCC_VidMode : public CCC_Token {
    u32 _dummy;

public:
    CCC_VidMode(LPCSTR N) : CCC_Token(N, &_dummy, NULL) { bEmptyArgsHandled = FALSE; };
    virtual void Execute(LPCSTR args) {
        u32 _w, _h;
        int cnt = sscanf(args, "%dx%d", &_w, &_h);
        if (cnt == 2) {
            psCurrentVidMode[0] = _w;
            psCurrentVidMode[1] = _h;
        } else {
            LogMsg("! Wrong video mode [{}]", args);
            return;
        }
    }
    virtual void Status(TStatus& S) {
        xr_sprintf(S, sizeof(S), "%dx%d", psCurrentVidMode[0], psCurrentVidMode[1]);
    }
    virtual xr_token* GetToken() { return vid_mode_token; }
    virtual void Info(TInfo& I) { xr_strcpy(I, sizeof(I), "change screen resolution WxH"); }

    virtual void fill_tips(vecTips& tips, u32 mode) {
        TStatus str, cur;
        Status(cur);

        bool res = false;
        xr_token* tok = GetToken();
        while (tok->name && !res) {
            if (!xr_strcmp(tok->name, cur)) {
                xr_sprintf(str, sizeof(str), "%s  (current)", tok->name);
                tips.push_back(str);
                res = true;
            }
            tok++;
        }
        if (!res) {
            tips.push_back("---  (current)");
        }
        tok = GetToken();
        while (tok->name) {
            tips.push_back(tok->name);
            tok++;
        }
    }
};
//-----------------------------------------------------------------------
class CCC_SND_Restart : public IConsole_Command {
public:
    CCC_SND_Restart(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args) { Sound->_restart(); }
};

//-----------------------------------------------------------------------
float ps_gamma = 1.f, ps_brightness = 1.f, ps_contrast = 1.f;
class CCC_Gamma : public CCC_Float {
public:
    CCC_Gamma(LPCSTR N, float* V) : CCC_Float(N, V, 0.5f, 1.5f) {}

    virtual void Execute(LPCSTR args) {
        CCC_Float::Execute(args);
        // Device.Gamma.Gamma		(ps_gamma);
        Device.m_pRender->setGamma(ps_gamma);
        // Device.Gamma.Brightness	(ps_brightness);
        Device.m_pRender->setBrightness(ps_brightness);
        // Device.Gamma.Contrast	(ps_contrast);
        Device.m_pRender->setContrast(ps_contrast);
        // Device.Gamma.Update		();
        Device.m_pRender->updateGamma();
    }
};

//-----------------------------------------------------------------------
/*
#ifdef	DEBUG
extern  INT	g_bDR_LM_UsePointsBBox;
extern	INT	g_bDR_LM_4Steps;
extern	INT g_iDR_LM_Step;
extern	Fvector	g_DR_LM_Min, g_DR_LM_Max;

class CCC_DR_ClearPoint : public IConsole_Command
{
public:
        CCC_DR_ClearPoint(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
        virtual void Execute(LPCSTR args) {
                g_DR_LM_Min.x = 1000000.0f;
                g_DR_LM_Min.z = 1000000.0f;

                g_DR_LM_Max.x = -1000000.0f;
                g_DR_LM_Max.z = -1000000.0f;

                Msg("Local BBox (%f, %f) - (%f, %f)", g_DR_LM_Min.x, g_DR_LM_Min.z, g_DR_LM_Max.x,
g_DR_LM_Max.z);
        }
};

class CCC_DR_TakePoint : public IConsole_Command
{
public:
        CCC_DR_TakePoint(LPCSTR N) : IConsole_Command(N)	{ bEmptyArgsHandled = TRUE; };
        virtual void Execute(LPCSTR args) {
                Fvector CamPos =  Device.vCameraPosition;

                if (g_DR_LM_Min.x > CamPos.x)	g_DR_LM_Min.x = CamPos.x;
                if (g_DR_LM_Min.z > CamPos.z)	g_DR_LM_Min.z = CamPos.z;

                if (g_DR_LM_Max.x < CamPos.x)	g_DR_LM_Max.x = CamPos.x;
                if (g_DR_LM_Max.z < CamPos.z)	g_DR_LM_Max.z = CamPos.z;

                Msg("Local BBox (%f, %f) - (%f, %f)", g_DR_LM_Min.x, g_DR_LM_Min.z, g_DR_LM_Max.x,
g_DR_LM_Max.z);
        }
};

class CCC_DR_UsePoints : public CCC_Integer
{
public:
        CCC_DR_UsePoints(LPCSTR N, int* V, int _min=0, int _max=999) : CCC_Integer(N, V, _min, _max)
{}; virtual void	Save	(IWriter *F)	{};
};
#endif
*/

ENGINE_API BOOL r2_sun_static = TRUE;
ENGINE_API BOOL r2_advanced_pp = FALSE; //	advanced post process and effects

u32 renderer_value = 3;
// void fill_render_mode_list();
// void free_render_mode_list();

class CCC_r2 : public CCC_Token {
    typedef CCC_Token inherited;

public:
    CCC_r2(LPCSTR N) : inherited(N, &renderer_value, NULL) { renderer_value = 3; };
    virtual ~CCC_r2() {
        // free_render_mode_list();
    }
    virtual void Execute(LPCSTR args) {
        // fill_render_mode_list	();
        //	vid_quality_token must be already created!
        tokens = vid_quality_token;

        inherited::Execute(args);
        //	0 - r1
        //	1..3 - r2
        //	4 - r3
        psDeviceFlags.set(rsR2, ((renderer_value > 0) && renderer_value < 4));
        psDeviceFlags.set(rsR3, (renderer_value == 4));
        psDeviceFlags.set(rsR4, (renderer_value >= 5));

        r2_sun_static = (renderer_value < 2);

        r2_advanced_pp = (renderer_value >= 3);
    }

    virtual void Save(IWriter* F) {
        // fill_render_mode_list	();
        tokens = vid_quality_token;
        if (!strstr(Core.Params, "-r2")) {
            inherited::Save(F);
        }
    }
    virtual xr_token* GetToken() {
        tokens = vid_quality_token;
        return inherited::GetToken();
    }
};

extern __declspec(dllimport) u32 snd_device_id;
extern __declspec(dllimport) xr_token* snd_devices_token;
extern __declspec(dllimport) int snd_hrtf;
extern __declspec(dllimport) u32 snd_output;
extern __declspec(dllimport) char snd_language[32];
extern __declspec(dllimport) void snd_refresh_devices();
extern __declspec(dllimport) void snd_get_status(char* text, u32 size);

static xr_token snd_output_token[] = {
    { "snd_output_auto", 0 }, { "snd_output_stereo", 1 },
    { "snd_output_surround51", 2 }, { "snd_output_surround71", 3 }, { nullptr, -1 }
};
static xr_token snd_acoustics_preset_token[] = {
    { "ui_mm_acoustics_optimized", 0 }, { "ui_mm_acoustics_quality", 1 }, { nullptr, -1 }
};

class CCC_SoundDevicesRefresh : public IConsole_Command {
public:
    CCC_SoundDevicesRefresh(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }
    virtual void Execute(LPCSTR) { snd_refresh_devices(); }
};

class CCC_SoundStatus : public IConsole_Command {
public:
    CCC_SoundStatus(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }
    virtual void Execute(LPCSTR) {
        TStatus text;
        Status(text);
        Msg("* [Noir Engine Audio] %s", text);
    }
    virtual void Status(TStatus& text) { snd_get_status(text, sizeof(text)); }
    virtual void Save(IWriter*) {} // Read-only diagnostics are not configuration.
};

class CCC_soundDevice : public CCC_Token {
    typedef CCC_Token inherited;

public:
    CCC_soundDevice(LPCSTR N) : inherited(N, &snd_device_id, NULL) {
        bLowerCaseArgs = false; // Preserve UTF-8 endpoint names verbatim.
    }
    virtual ~CCC_soundDevice() {}

    virtual void Execute(LPCSTR args) {
        GetToken();
        if (!tokens) return;
        inherited::Execute(args);
    }

    virtual void Status(TStatus& S) {
        GetToken();
        if (!tokens) { xr_strcpy(S, "Default"); return; }
        inherited::Status(S);
    }

    virtual xr_token* GetToken() {
        // A combo box requires a valid sentinel even with -nosound.
        static xr_token fallback[] = { { "Default", 0 }, { nullptr, -1 } };
        tokens = snd_devices_token ? snd_devices_token : fallback;
        return inherited::GetToken();
    }

    virtual void Info(TInfo& I) {
        xr_strcpy(I, "output device name; use snd_device_refresh to refresh the list");
    }

    virtual void Save(IWriter* F) {
        GetToken();
        if (!tokens) return;
        inherited::Save(F);
    }
};
// ------------------------------------------------------
//-----------------------------------------------------------------------
class CCC_ExclusiveMode : public IConsole_Command {
private:
    typedef IConsole_Command inherited;

public:
    CCC_ExclusiveMode(LPCSTR N) : inherited(N) {}

    virtual void Execute(LPCSTR args) {
        bool value = false;
        if (!xr_strcmp(args, "on"))
            value = true;
        else if (!xr_strcmp(args, "off"))
            value = false;
        else if (!xr_strcmp(args, "true"))
            value = true;
        else if (!xr_strcmp(args, "false"))
            value = false;
        else if (!xr_strcmp(args, "1"))
            value = true;
        else if (!xr_strcmp(args, "0"))
            value = false;
        else
            InvalidSyntax();

        pInput->exclusive_mode(value);
    }

    virtual void Save(IWriter* F) {}
};

class ENGINE_API CCC_HideConsole : public IConsole_Command {
public:
    CCC_HideConsole(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }

    virtual void Execute(LPCSTR args) { Console->Hide(); }
    virtual void Status(TStatus& S) { S[0] = 0; }
    virtual void Info(TInfo& I) { xr_sprintf(I, sizeof(I), "hide console"); }
};

ENGINE_API float psHUD_FOV = 0.45f;

// extern int			psSkeletonUpdate;
extern int rsDVB_Size;
extern int rsDIB_Size;
extern int psNET_ClientUpdate;
extern int psNET_ClientPending;
extern int psNET_ServerUpdate;
extern int psNET_ServerPending;
extern char psNET_Name[32];
extern Flags32 psEnvFlags;
// extern float		r__dtex_range;

extern int g_ErrorLineCount;

ENGINE_API int ps_r__Supersample = 1;

// Screen Space Shaders.
// These live in xrEngine rather than in the renderer's console file because both sides use them:
// Environment drives the wind animation from ssfx_wind_trees and the wetness from
// ssfx_wetness_multiplier, and every renderer reads them. xrRender_console.cpp is compiled once per
// renderer DLL, so a definition there would give each DLL its own private copy.
// Names, defaults and ranges follow Monolith ( xrRender_console.cpp and xr_ioc_cmd.cpp ).
ENGINE_API Fvector4 ps_ssfx_floravariation = { 0.025f, 0.1f, 0.025f, 0.05f }; // Grass Int, Grass Freq, Foliage Int, Foliage Freq ( 0.025, 0.1, 0.03, 0.05 )
ENGINE_API Fvector4 ps_ssfx_motionblur = { 6.0f, 1.0f, 0.0f, 1.0f }; // Samples, Intensity, Only HUD, Camera
ENGINE_API Fvector4 ps_ssfx_taa = { 1.0f, 0.5f, 0.6f, 0.0f }; // Enable, Jitter, Sharpness, -
ENGINE_API Fvector4 ps_ssfx_fog = { 8.0f, 1.3f, 0.1f, 0.0f }; // Height, Density, SunColor, -
ENGINE_API float ps_ssfx_fog_scattering = 0.7f; // Fog scattering intensity

ENGINE_API int ps_ssfx_pom_refine = 0;
ENGINE_API Fvector4 ps_ssfx_pom = { 16.0f, 12.0f, 0.035f, 0.4f }; // Samples , Range, Height, AO

ENGINE_API int ps_ssfx_terrain_grass_align = 0; // Grass align
ENGINE_API float ps_ssfx_terrain_grass_slope = 1.0f; // Grass slope limit // Recommended 0.3f
ENGINE_API Fvector4 ps_ssfx_terrain_pom = { 12.0f, 20.0f, 0.04f, 1.0f }; // Samples, Range, Height, Water Limit
ENGINE_API int ps_ssfx_terrain_pom_refine = 0;

ENGINE_API int ps_ssfx_bloom_use_presets = 0;
ENGINE_API Fvector4 ps_ssfx_bloom_1 = { 3.5f, 3.0f, 0.0f, 0.6f }; // Threshold, Exposure, -, Sky
ENGINE_API Fvector4 ps_ssfx_bloom_2 = { 3.0f, 1.5f, 1.5f, 1.0f }; // Blur Radius, Vibrance, Lens, Dirt

ENGINE_API Fvector4 ps_ssfx_sss_quality = { 12.0f, 4.0f, 1.0f, 1.0f }; // Dir Samples, Omni Samples, Dir Enable, Omni Enable
ENGINE_API Fvector4 ps_ssfx_sss = { 1.0f, 1.0f, 1.0f, 0.0f }; // Dir Len, Omni Len, Grass shadows, -

ENGINE_API float ps_ssfx_hud_hemi = 0.15f; // HUD Hemi Offset

ENGINE_API int ps_ssfx_il_quality = 32; // IL Samples
ENGINE_API Fvector4 ps_ssfx_il = { 6.6667f, 1.0f, 1.0f, 5.0f }; // Res, Int, Vibrance, Blur
ENGINE_API Fvector4 ps_ssfx_il_setup1 = { 150.0f, 1.0f, 0.5f, 0.0f }; // Distance, HUD, Flora, -

ENGINE_API int ps_ssfx_ao_quality = 4; // AO Samples
ENGINE_API Fvector4 ps_ssfx_ao = { 1.0f, 5.0f, 1.0f, 2.5f }; // Res, AO int, Blur, Radius
ENGINE_API Fvector4 ps_ssfx_ao_setup1 = { 150.0f, 1.0f, 1.0f, 0.0f }; // Distance, HUD, Flora, Max OCC

ENGINE_API Fvector4 ps_ssfx_water = { 1.0f, 0.8f, 1.0f, 0.0f }; // Res, Blur, Blur Perlin, -
ENGINE_API Fvector3 ps_ssfx_water_quality = { 1.0f, 2.0f, 0.0f }; // SSR Quality, Parallax Quality, -
ENGINE_API Fvector4 ps_ssfx_water_setup1 = { 0.6f, 3.0f, 0.3f, 0.05f }; // Distortion, Turbidity, Softborder, Parallax Height
ENGINE_API Fvector4 ps_ssfx_water_setup2 = { 0.8f, 6.0f, 0.3f, 0.5f }; // Reflection, Specular, Caustics, Ripples

ENGINE_API int ps_ssfx_ssr_quality = 0; // Quality
ENGINE_API Fvector4 ps_ssfx_ssr = { 1.0f, 0.2f, 0.0f, 0.0f }; // Res, Blur, Temp, Noise
// Intensity, Sky Intensity, Weapon Intensity, Max Weapon Intensity - the order the shader states
// ( screenspace_reflections.h:17 ), not the donor's stale comment. Values are the author's own
// ( ssfx_ssr_mcm.script:22-26 ): general 1.0, sky 1.0, weapon 0.5, weapon max 0.02. The engine used
// to start with intensity 0, which switched reflections off before the player ever saw them.
ENGINE_API Fvector4 ps_ssfx_ssr_2 = { 1.0f, 1.0f, 0.5f, 0.02f };

ENGINE_API Fvector4 ps_ssfx_terrain_quality = { 8.0f, 0.0f, 0.0f, 0.0f };
ENGINE_API Fvector4 ps_ssfx_terrain_offset = { 0.0f, 0.0f, 0.0f, 0.0f };

ENGINE_API Fvector3 ps_ssfx_shadows = { 256.0f, 1536.0f, 0.0f }; // LOD min res, LOD max res, -
ENGINE_API Fvector4 ps_ssfx_volumetric = { 1.0f, 1.0f, 4.0f, 1.0f }; // Force Volumetric, Vol Intensity, Vol Quality, -

ENGINE_API Fvector3 ps_ssfx_shadow_bias = { 0.4f, 0.03f, 0.0f };

ENGINE_API Fvector4 ps_ssfx_lut = { 0.0f, 0.0f, 0.0f, 0.0f };

ENGINE_API Fvector4 ps_ssfx_wind_grass = { 9.5f, 1.4f, 1.5f, 0.4f };
ENGINE_API Fvector4 ps_ssfx_wind_trees = { 11.0f, 0.15f, 0.5f, 0.1f };

ENGINE_API Fvector4 ps_ssfx_florafixes_1 = { 0.3f, 0.21f, 0.3f, 0.21f }; // Flora fixes 1
ENGINE_API Fvector4 ps_ssfx_florafixes_2 = { 2.0f, 1.0f, 0.0f, 0.0f }; // Flora fixes 2

// Wet surfaces 1: Buildup speed, Dry speed, Puddles, Ripples.
// Wet surfaces 2: Gloss, Specular, Darkening, Waterfall.
// All-ones was a placeholder. With the native menu owning these settings the author's MCM applier
// returns early, so whatever stands here is what the shader gets for a setting with no slider yet.
// These are the author's own module defaults ( ssfx_wetsurfaces_settings.script ).
ENGINE_API Fvector4 ps_ssfx_wetsurfaces_1 = { 0.5f, 1.4f, 0.7f, 1.25f };
ENGINE_API Fvector4 ps_ssfx_wetsurfaces_2 = { 0.8f, 1.5f, 0.2f, 0.35f };

ENGINE_API int ps_ssfx_is_underground = 0;
ENGINE_API int ps_ssfx_gloss_method = 0;
ENGINE_API float ps_ssfx_gloss_factor = 0.5f;
ENGINE_API int ps_ssfx_fakescope = 0; // Screen Space Shaders: Anomaly 2D shader scopes, unused here
// Gloss min / max. The author ships 0.6 / 0.9; a minimum of 0.0 left every surface fully rough
// until the player touched the slider.
ENGINE_API Fvector4 ps_ssfx_gloss_minmax = { 0.6f, 0.9f, 0.0f, 0.0f }; // Gloss

ENGINE_API Fvector4 ps_ssfx_lightsetup_1 = { 0.35f, 0.5f, 0.0f, 0.0f }; // Spec intensity

// Screen Space Shaders: the pseudo-PBR module ( Enhanced Shaders 1.10, pbr_settings.h ). The donor
// keeps these as fixed numbers in the shader; here they are settings, so the SSFX menu can change
// them. The defaults are the donor's numbers. The ranges keep every formula defined: a roughness
// of 0 divides by zero in the GGX highlight, and so does a power of 0 or less in calc_rough().
ENGINE_API Fvector4 ps_ssfx_es_albedo = { 0.0f, 1.0f, 0.0f, 0.0f }; // boost, amount
ENGINE_API Fvector4 ps_ssfx_es_rough = { 0.5f, 1.0f, 1.0f, 0.0f }; // low, high, power
ENGINE_API Fvector4 ps_ssfx_es_spec = { 0.01f, 1.0f, 1.0f, 0.0f }; // base, range, power
ENGINE_API Fvector4 ps_ssfx_es_metal = { 0.25f, 0.125f, 0.0f, 0.0f }; // boost, threshold
ENGINE_API int ps_ssfx_es_burley = 1; // Burley diffuse ( 0 = Lambert ), compiled in
ENGINE_API int ps_ssfx_es_ggx = 1; // GGX highlight ( 0 = Blinn-Phong ), compiled in
// The same module: eight numbers Enhanced Shaders writes straight into its formulas, settings of
// the SSS window as well ( the user's choice of 2026-10-14, items 1-7 ). Each default is the
// donor's number, so nothing changes on screen until a slider moves.
//   light: x = the power of n.l in Lit_BRDF ( pbr_brdf.h, 2 - the donor squares it ),
//          y = the horizon falloff of the direct highlight ( pbr_brdf.h, DotNV * 2 ),
//          z = the glow strength of accum_emissive.ps ( 16 )
//   flora: x = the grass and leaf specular ( pbr_brdf.h calc_foliage, gloss * 0.05 ),
//          y = how much of the sky reflection flora loses ( hmodel.h, 0.75 ),
//          z = the strength of the light through leaves ( lmodel.h, SSS() x 1 )
//   water: x = the reflectance of the sun highlight on water ( lmodel.h specular_phong, 0.02 ),
//          y = its roughness, which sets how wide the highlight is ( 0.1 )
ENGINE_API Fvector4 ps_ssfx_es_light = { 2.0f, 2.0f, 16.0f, 0.0f }; // n.l power, horizon, glow
ENGINE_API Fvector4 ps_ssfx_es_flora = { 0.05f, 0.75f, 1.0f, 0.0f }; // specular, sky cut, light-through
ENGINE_API Fvector4 ps_ssfx_es_water = { 0.02f, 0.1f, 0.0f, 0.0f }; // sun highlight: reflectance, roughness

// Anim Speed, Int, Reflection, Refraction. Reflection is a x30 and refraction a x0.05 transform of
// the author's 0.5..1.5 slider, so his default of 1.0 lands on 30 and 0.05 - not on 1.0 and 1.0.
ENGINE_API Fvector4 ps_ssfx_hud_drops_1 = { 1.0f, 1.0f, 30.0f, 0.05f };
// DEVIATION FROM THE DONOR, and the reason the raindrops on the weapon were invisible.
//
// Monolith ships { 1.5, 0.85, 0.0, 2.0 } here, but nothing in Monolith ever renders with it: the
// MCM applier ssfx_rain_hud_raindrops.script:92-95 overwrites x and y on every game start from the
// module defaults in ssfx_rain_hud_raindrops_settings.script:10,17
//
//     val_density = 0.15 * ( 3.5 - density )  =  0.15 * ( 3.5 - 2.0 )  =  0.225
//     val_texsize = 2.0 - size                =  2.0 - 0.75            =  1.25
//
// so the value the donor's shader actually sees is { 0.225, 1.25, 0.0, 2.0 }. In this port the
// native menu owns these settings, so that applier returns early ( menu_owns_settings ) and the
// engine default is what reaches the shader. x is SUBTRACTED there:
//
//     DropsInt = saturate( ( DropsFrac * droplets.x ) - ssfx_hud_drops_2.x )
//
// and DropsFrac * droplets.x can never exceed 1, so x = 1.5 clamps every drop on the weapon to
// exactly zero in every frame. The two values below are the donor's own, arrived at by the donor's
// own arithmetic; ssfx_ui_drops_density is registered over 0..1, which 1.5 was outside of anyway.
ENGINE_API Fvector4 ps_ssfx_hud_drops_2 = { 0.225f, 1.25f, 0.0f, 2.0f }; // Density, Size, Extra Gloss, Gloss

ENGINE_API Fvector4 ps_ssfx_blood_decals = { 0.6f, 0.6f, 0.0f, 0.0f };
ENGINE_API Fvector4 ps_ssfx_rain_1 = { 2.0f, 0.1f, 0.6f, 2.0f }; // Len, Width, Speed, Quality
ENGINE_API Fvector4 ps_ssfx_rain_2 = { 0.8f, 0.3f, 1.0f, 1.0f }; // Alpha, Brigthness, Refraction, Reflection
ENGINE_API Fvector4 ps_ssfx_rain_3 = { 0.5f, 1.5f, 0.0f, 0.0f }; // Alpha, Refraction ( Splashes )
ENGINE_API Fvector4 ps_ssfx_rain_drops_setup = { 2500.0f, 15.0f, 0.0f, 0.0f };

ENGINE_API Fvector3 ps_ssfx_shadow_cascades = { 20.0f, 60.0f, 160.0f };
// Quality preset, shadow distance, non-directional light cut-off, -.
//
// The distance is a FRACTION of the grass draw distance, not metres. r2_R_sun.cpp multiplies it
// straight into the fade distance ( dm_fade * dm_fade * .y ), and the author's applier divides
// his 0 .. 100 slider by 100 and clamps the result to 0 .. 1 before sending it
// ( ssfx_shadow_cascades.script:36-44 ). His default of 35 is therefore 0.35, which is also the
// donor engine's own default ( Monolith xrRender_console.cpp:464 ). This read 35 for a while, on
// the belief that the unit was metres: that drew every blade of grass on the level into the
// shadow cascades, a hundred times the distance the author asks for.
ENGINE_API Fvector4 ps_ssfx_grass_shadows = { 0.0f, 0.35f, 30.0f, 0.0f };

// Monolith defaults, xr_ioc_cmd.cpp:999-1000.
ENGINE_API float ps_r2_sun_shafts_min = 0.f;
ENGINE_API float ps_r2_sun_shafts_value = 1.f;

// On by default: the whole point is that the player no longer sets the stock options by hand.
ENGINE_API int ps_ssfx_auto_settings = 1;
ENGINE_API int ps_ssfx_menu = 1;
ENGINE_API int ps_ssfx_menu_configured = 0;

// SSS on or off - ssfx_console_vars.h says what the two are. Both start on, as a missing line of
// [shader_extensions] leaves them; ssfx_settings_config_loaded() puts that line over them.
ENGINE_API int ps_ssfx_enable = 1;
ENGINE_API int ssfx_session_enable = 1;

// Monolith xrRender_console.cpp:482, off by default. See ssfx_console_vars.h.
ENGINE_API Flags32 ps_actor_shadow_flags = {0};

ENGINE_API Fvector4 ps_ssfx_grass_interactive = { 0.0f, 0.0f, 2000.0f, 1.0f };
ENGINE_API Fvector4 ps_ssfx_int_grass_params_1 = { 1.0f, 1.0f, 1.0f, 25.0f };
// Explosion strength, shockwave speed, shooting strength, shooting range.
//
// The donor engine starts the last two at 1 and 1 ( Monolith xrRender_console.cpp:468 ), and never
// runs with them: the author's applier sends his own 0.3 and 2 on every game start
// ( ssfx_interactive_grass.script:32, values from ssfx_interactive_grass_settings.script:24-25 ).
// Here the native page owns the vector and that applier stays out of the way, so the start value
// is what the grass gets on a profile that has never saved one - it is the author's pair.
ENGINE_API Fvector4 ps_ssfx_int_grass_params_2 = { 1.0f, 5.0f, 0.3f, 2.0f };

ENGINE_API Fvector4 ps_ssfx_wpn_dof_1 = { 0.0f, 0.0f, 0.0f, 0.0f };
ENGINE_API float ps_ssfx_wpn_dof_2 = 1.0f;

// Rain wetness buildup and drying speed, read by CEnvDescriptorMixer::lerp.
ENGINE_API Fvector3 ssfx_wetness_multiplier = Fvector3().set(1.0f, 0.3f, 0.0f);

// Infernis PBR - the settings page. infernis_pbr_console_vars.h says what each one is.
// The first two start at what the code used when [shader_extensions] gave no line;
// pbr_settings_config_loaded() puts the two lines of that section over them.
ENGINE_API int ps_pbr_enable = 1;
ENGINE_API u32 ps_pbr_sslr_mode = 1;
ENGINE_API int pbr_session_enable = 1;
ENGINE_API u32 pbr_session_sslr_mode = 1;
// The numbers shaders\r3\infernis_pbr_lighting.h ships with, in the order the header names.
ENGINE_API Fvector4 ps_pbr_cal_diffuse = { 0.90f, 0.80f, 0.90f, 0.72f };
ENGINE_API Fvector4 ps_pbr_cal_specular = { 1.00f, 1.00f, 1.00f, 2.0f };
ENGINE_API Fvector4 ps_pbr_highlight = { 0.25f, 2.0f, 0.35f, 0.10f };
ENGINE_API Fvector4 ps_pbr_surface = { 0.08f, 0.0f, 0.0f, 0.0f };

// A token's name is a string id of pbr_ui_menu.xml, its id the number IE_PBR_SSLR_MODE takes.
static xr_token tok_pbr_sslr_mode[] = {
    { "ui_pbr_sslr_off", 0 },
    { "ui_pbr_sslr_performance", 1 },
    { "ui_pbr_sslr_balanced", 2 },
    { "ui_pbr_sslr_quality", 3 },
    { "ui_pbr_sslr_reference", 4 },
    { 0, 0 },
};

// The reading xrRenderCommon/infernis_pbr_settings.h did on every call, with the same fallbacks:
// no section or no line leaves PBR on and SSLR at "performance", and so does a mode word nobody
// knows.
void pbr_settings_config_loaded() {
    if (!pSettings || !pSettings->section_exist("shader_extensions"))
        return;

    ps_pbr_enable =
        READ_IF_EXISTS(pSettings, r_bool, "shader_extensions", "enable_pbr_rendering", true) ? 1 : 0;

    if (!pSettings->line_exist("shader_extensions", "sslr_mode"))
        return;
    LPCSTR mode = pSettings->r_string("shader_extensions", "sslr_mode");
    ps_pbr_sslr_mode = 1;
    if (!mode)
        return;
    if (0 == _stricmp(mode, "off"))
        ps_pbr_sslr_mode = 0;
    else if (0 == _stricmp(mode, "balanced"))
        ps_pbr_sslr_mode = 2;
    else if (0 == _stricmp(mode, "quality"))
        ps_pbr_sslr_mode = 3;
    else if (0 == _stricmp(mode, "reference"))
        ps_pbr_sslr_mode = 4;
}

void pbr_settings_user_config_loaded() {
    pbr_session_enable = ps_pbr_enable;
    pbr_session_sslr_mode = ps_pbr_sslr_mode;
}

// The factory value of the SSS switch, with the fallback of the PBR one: no section or no line
// leaves SSS on.
void ssfx_settings_config_loaded() {
    if (!pSettings || !pSettings->section_exist("shader_extensions"))
        return;

    ps_ssfx_enable =
        READ_IF_EXISTS(pSettings, r_bool, "shader_extensions", "enable_ssfx", true) ? 1 : 0;
}

void ssfx_settings_user_config_loaded() {
    ssfx_session_enable = ps_ssfx_enable;
}

// Screen Space Shaders: two profiles ( the user, 2026-10-27 ). user.ltx is the game without SSS -
// any renderer, the game's own settings - and the one place the SSS switch ( ssfx_enable ) is
// kept; user_sss.ltx is the game with SSS - DX11, no SSAO, no MSAA, no tessellation - and never
// keeps the switch. With one file for both, an SSS session saved its values there and the game
// without SSS started with them. A missing profile comes back from its template in
// gamedata\configs ( default_user.ltx, default_user_sss.ltx ). The keys, the sound, the mouse and
// the "Game" tab are one for both: every save carries them over to the other profile. With a
// profile named on the command line ( -ltx ) that one file is used for everything, as before.
static LPCSTR ssfx_profile_plain = "user.ltx";
static LPCSTR ssfx_profile_sss = "user_sss.ltx";
static bool ssfx_profiles = false; // the two profiles are in use ( no -ltx )

// The settings both profiles share: the entries of the "Game", "Sound" and "Controls" tabs
// ( mm_opt_gameplay, mm_opt_sound, mm_opt_controls in ui_mm_opt.xml ) and the lines the key
// bindings are saved as ( xrGame\xr_level_controller.cpp ).
static LPCSTR ssfx_profile_shared_cmds[] = {
    "g_game_difficulty", "hud_crosshair", "cl_dynamiccrosshair", "hud_weapon",
    "hud_crosshair_dist", "hud_info", "g_crouch_toggle", "wpn_aim_toggle", "g_lookout_toggle",
    "g_important_save", "hud_draw", "r__actor_shadow", "snd_volume_eff", "snd_volume_music",
    "snd_device", "snd_efx", "g_dynamic_music", "snd_hrtf", "snd_output", "snd_acoustics",
    "snd_acoustics_preset", "snd_language", "text_language", "mouse_sens", "mouse_invert",
    "default_controls", "unbindall", "bind", "bind_sec", "bind_console", nullptr};

// The SSS settings that do not start with "ssfx_": the ones SSS added to the engine, set by its
// settings page ( mm_opt_ssfx ) or by its scripts ( r__fakescope ).
static LPCSTR ssfx_profile_sss_cmds[] = {
    "r2_gloss_min", "r2_ss_sunshafts_length", "r2_ss_sunshafts_radius", "r2_sunshafts_min",
    "r2_sunshafts_value", "r__detail_height", "r__detail_radius", "r__fakescope", nullptr};

extern int g_current_renderer; // EngineAPI.cpp

static bool ssfx_profile_listed(LPCSTR name, LPCSTR* list) {
    for (; *list; ++list)
        if (0 == xr_strcmp(name, *list))
            return true;
    return false;
}

static bool ssfx_profile_is_sss() { return 0 == xr_strcmp(Console->ConfigFile, ssfx_profile_sss); }

// "cfg_save user" and "cfg_save user_sss" save the profile in use, as "cfg_save" does ( the user,
// 2026-10-27: with SSS on that is user_sss.ltx, with SSS off user.ltx ). Only the bare names, with
// or without ".ltx"; a path or any other name is a file of its own, as before.
static bool ssfx_profile_named(LPCSTR args) {
    if (!ssfx_profiles)
        return false;
    string_path name;
    xr_strcpy(name, args);
    if (strext(name) && 0 == _stricmp(strext(name), ".ltx"))
        *strext(name) = 0;
    return 0 == _stricmp(name, "user") || 0 == _stricmp(name, "user_sss");
}

// The command goes into the profile in use when the console saves it ( CCC_SaveCFG ).
static bool ssfx_profile_keeps(LPCSTR name) {
    if (!ssfx_profiles)
        return true;
    if (0 == xr_strcmp(name, "ssfx_enable"))
        return !ssfx_profile_is_sss();
    if (ssfx_profile_is_sss())
        return true;
    return 0 != strncmp(name, "ssfx_", 5) && !ssfx_profile_listed(name, ssfx_profile_sss_cmds);
}

static void ssfx_profile_path(string_path& full, LPCSTR name) {
    FS.update_path(full, "$app_data_root$", name);
}

// The command a line of a profile holds: its first word.
static void ssfx_profile_word(LPCSTR line, string64& word) {
    u32 n = 0;
    for (; line[n] && line[n] != ' ' && line[n] != '\t' && n + 1 < sizeof(word); ++n)
        word[n] = line[n];
    word[n] = 0;
}

// The lines of a profile; false when there is no such file.
static bool ssfx_profile_read(LPCSTR full, xr_vector<xr_string>& lines) {
    if (!FS.exist(full))
        return false;
    IReader* F = FS.r_open(full);
    if (!F)
        return false;
    string1024 str;
    while (!F->eof()) {
        F->r_string(str, sizeof(str));
        if (str[0])
            lines.push_back(str);
    }
    FS.r_close(F);
    return true;
}

// The profile "to" takes the shared settings of the profile "from" in place of its own. user.ltx
// gets switch_line in place of its own switch line when there is one, and renderer_line in place
// of its own renderer line when there is one; user_sss.ltx loses any switch line.
static void ssfx_profile_share(LPCSTR from, LPCSTR to, LPCSTR switch_line, LPCSTR renderer_line) {
    string_path src, dst;
    ssfx_profile_path(src, from);
    ssfx_profile_path(dst, to);
    xr_vector<xr_string> src_lines, dst_lines;
    if (!ssfx_profile_read(src, src_lines) || !ssfx_profile_read(dst, dst_lines))
        return;
    const bool drop_switch = switch_line || 0 == xr_strcmp(to, ssfx_profile_sss);
    SetFileAttributes(dst, FILE_ATTRIBUTE_NORMAL);
    IWriter* F = FS.w_open(dst);
    if (!F)
        return;
    string64 word;
    for (const xr_string& line : dst_lines) {
        ssfx_profile_word(line.c_str(), word);
        if (ssfx_profile_listed(word, ssfx_profile_shared_cmds))
            continue;
        if (drop_switch && 0 == xr_strcmp(word, "ssfx_enable"))
            continue;
        if (renderer_line && 0 == xr_strcmp(word, "renderer"))
            continue;
        F->w_printf("%s\r\n", line.c_str());
    }
    for (const xr_string& line : src_lines) {
        ssfx_profile_word(line.c_str(), word);
        if (ssfx_profile_listed(word, ssfx_profile_shared_cmds))
            F->w_printf("%s\r\n", line.c_str());
    }
    if (renderer_line)
        F->w_printf("%s\r\n", renderer_line);
    if (switch_line)
        F->w_printf("%s\r\n", switch_line);
    FS.w_close(F);
}

// A missing profile comes back from its template; true when it had to.
static bool ssfx_profile_restore(LPCSTR name, LPCSTR tmpl) {
    string_path full, src;
    ssfx_profile_path(full, name);
    if (FS.exist(full))
        return false;
    FS.update_path(src, "$game_config$", tmpl);
    FS.file_copy(src, full);
    if (!FS.exist(full)) {
        Msg("! Profile [%s] is missing, and so is its template [%s]", name, tmpl);
        return false;
    }
    Msg("* Profile [%s] made from [%s]", name, tmpl);
    return true;
}

static void ssfx_profile_copy(LPCSTR from, LPCSTR to) {
    string_path src, dst;
    ssfx_profile_path(src, from);
    ssfx_profile_path(dst, to);
    if (!FS.exist(src))
        return;
    if (FS.exist(dst))
        SetFileAttributes(dst, FILE_ATTRIBUTE_NORMAL);
    FS.file_copy(src, dst);
}

// The profile in use is saved: the other one takes the shared settings, and user.ltx the switch as
// it stands now ( the check box on the video page ). With the box not ticked in a game with SSS,
// user.ltx takes the renderer too: the list offers every renderer again then, and the one
// picked is for the game without SSS ( the user, 2026-10-27 ). A missing profile comes back first.
static void ssfx_profile_saved() {
    if (!ssfx_profiles)
        return;
    ssfx_profile_restore(ssfx_profile_plain, "default_user.ltx");
    ssfx_profile_restore(ssfx_profile_sss, "default_user_sss.ltx");
    if (ssfx_profile_is_sss()) {
        string64 line, renderer = "";
        xr_sprintf(line, "ssfx_enable %d", ps_ssfx_enable);
        LPCSTR picked = Console->GetToken("renderer");
        if (!ps_ssfx_enable && picked && picked[0])
            xr_sprintf(renderer, "renderer %s", picked);
        ssfx_profile_share(ssfx_profile_sss, ssfx_profile_plain, line,
                           renderer[0] ? renderer : nullptr);
    } else
        ssfx_profile_share(ssfx_profile_plain, ssfx_profile_sss, nullptr, nullptr);
}

// The console is made: which of the two files is the profile of this start.
void ssfx_profile_select() {
    if (strstr(Core.Params, "-ltx "))
        return;
    ssfx_profiles = true;

    // a profile that comes back from its template takes the shared settings of the other one
    const bool new_plain = ssfx_profile_restore(ssfx_profile_plain, "default_user.ltx");
    const bool new_sss = ssfx_profile_restore(ssfx_profile_sss, "default_user_sss.ltx");
    if (new_plain && !new_sss)
        ssfx_profile_share(ssfx_profile_sss, ssfx_profile_plain, nullptr, nullptr);
    if (new_sss && !new_plain)
        ssfx_profile_share(ssfx_profile_plain, ssfx_profile_sss, nullptr, nullptr);

    // the switch: the ssfx_enable line of user.ltx; without one the factory value stays
    // ( ssfx_settings_config_loaded )
    CCC_LoadCFG_custom* pTmp = xr_new<CCC_LoadCFG_custom>("ssfx_enable ");
    pTmp->Execute(ssfx_profile_plain);
    xr_delete(pTmp);

    xr_strcpy(Console->ConfigFile, ps_ssfx_enable ? ssfx_profile_sss : ssfx_profile_plain);
    Msg("* Profile [%s] ( ssfx_enable %d )", Console->ConfigFile, ps_ssfx_enable);
}

// The renderer line is read: with SSS the game runs on DX11, whatever user_sss.ltx holds.
void ssfx_profile_renderer() {
    if (ssfx_profiles && ssfx_profile_is_sss())
        Console->Execute("renderer renderer_r4");
}

// The renderer is loaded: the shader cache is emptied when the renderer or the SSS switch is not
// the one of the last start ( the user, 2026-10-27 ), or, with SSS on DX11, the SSS addon packs
// ( the user, 2026-10-28: a pack put in or taken out empties it as a switch of SSS does );
// shaders_cache\last_start.txt keeps them. Through FS, so the cache files go from its list too -
// the renderer looks the cache up there.
void ssfx_profile_cache_check() {
    string_path cache, key_file;
    FS.update_path(cache, "$app_data_root$", "shaders_cache\\");
    FS.update_path(key_file, "$app_data_root$", "shaders_cache\\last_start.txt");

    string_path now;
    xr_sprintf(now, "renderer %u %d sss %d", renderer_value, g_current_renderer, ps_ssfx_enable);
    // The packs this start mounts ( x_ray.cpp, Startup: SSS switched on and DX11 ). Without one the
    // line is the one of before.
    if (ps_ssfx_enable && g_current_renderer == 4) {
        string_path packs;
        FS.ssfx_addon_names(packs);
        if (packs[0] && xr_strlen(now) + xr_strlen(packs) + 9 <= sizeof(now)) {
            xr_strcat(now, " addons ");
            xr_strcat(now, packs);
        }
    }

    string_path was = "";
    if (FS.exist(key_file)) {
        IReader* F = FS.r_open(key_file);
        if (F) {
            if (!F->eof())
                F->r_string(was, sizeof(was));
            FS.r_close(F);
        }
    }
    if (0 == xr_strcmp(was, now))
        return;

    if (FS.exist(cache)) {
        FS.dir_delete(cache, TRUE);
        Msg("* Shader cache emptied: [%s], the last start [%s]", now, was);
    }
    IWriter* W = FS.w_open(key_file);
    if (W) {
        W->w_printf("%s\r\n", now);
        FS.w_close(W);
    }
}

// The last save is done: a copy of each profile ( user_backup.ltx, user_sss_backup.ltx ), to bring
// back by hand. Not "tmp": userdata\tmp.ltx is the options page's undo copy.
void ssfx_profile_backup() {
    if (!ssfx_profiles)
        return;
    ssfx_profile_copy(ssfx_profile_plain, "user_backup.ltx");
    ssfx_profile_copy(ssfx_profile_sss, "user_sss_backup.ltx");
}

// Screen Space Shaders: settings that never reach a shader through a vector the settings page
// could aim a control at. The author's MCM held them and his scripts read them from there, so the
// donor engine has no console variable for any of them. With the native page in charge each one
// needs a variable of its own, which ssfx_001_mcm.ssfx_get_setting() reads back. Every default is
// the author's factory value and every range, at the registration below, is his own slider's.
//
// HUD raindrops - the three speeds of the per-frame integrator in ssfx_rain_hud_raindrops.script.
// Only the accumulated pair is sent on, to ssfx_hud_drops_1.x/.y, on every frame it rains.
static float ps_ssfx_ui_drops_animation_speed = 1.0f;
static float ps_ssfx_ui_drops_buildup = 1.0f;
static float ps_ssfx_ui_drops_drying = 1.0f;
// Rain footsteps - sound volumes, ssfx_rain_footsteps.script.
static float ps_ssfx_ui_fs_main_vol = 0.4f;
static float ps_ssfx_ui_fs_vol_rnd = 0.15f;
static float ps_ssfx_ui_fs_multi_no_rain = 0.3f;
static float ps_ssfx_ui_fs_multi_walk = 0.33f;
static float ps_ssfx_ui_fs_multi_run = 1.4f;
static float ps_ssfx_ui_fs_jump_vol = 0.7f;
static float ps_ssfx_ui_fs_land_vol = 0.7f;
// Weapon DOF - ssfx_weapons_dof.script is a state machine that rewrites ssfx_wpn_dof_1 and
// ssfx_wpn_dof_2 on every aim, reload, PDA and inventory change, from these thirteen. A control
// aimed at either of those two commands would be undone the next time the player raised the
// sights.
static float ps_ssfx_ui_wdof_aim_fadestart = 0.1f;
static float ps_ssfx_ui_wdof_aim_fadelen = 0.25f;
static float ps_ssfx_ui_wdof_aim_blur = 1.6f;
static float ps_ssfx_ui_wdof_aim_edgeblur = 1.0f;
static float ps_ssfx_ui_wdof_fadestart = 0.15f;
static float ps_ssfx_ui_wdof_fadelen = 0.25f;
static float ps_ssfx_ui_wdof_blur = 1.1f;
static float ps_ssfx_ui_wdof_edgeblur = 0.3f;
static float ps_ssfx_ui_wdof_reloading = 1.0f;
static float ps_ssfx_ui_wdof_pda = 1.0f;
static float ps_ssfx_ui_wdof_inventory = 1.0f;
static float ps_ssfx_ui_wdof_fdda = 1.0f;
static float ps_ssfx_ui_wdof_looting_mutant = 1.0f;
// Gloss - whether it follows the rain, and how far ( ssfx_wetness.script, update_rain ).
static float ps_ssfx_ui_gloss_auto_gloss = 1.0f;
static float ps_ssfx_ui_gloss_auto_gloss_max = 1.0f;
// Interactive grass - the two module switches and the three values they zero.
static float ps_ssfx_ui_grass_enable = 1.0f;
static float ps_ssfx_ui_grass_max_entities = 8.0f;
static float ps_ssfx_ui_grass_enable_player = 1.0f;
static float ps_ssfx_ui_grass_enable_anomalies = 1.0f;
static float ps_ssfx_ui_grass_anomalies_distance = 25.0f;
// Wind Leaves - the thirteen settings of its MCM page ( wind_leaves_mcm.script ), set in its own
// window in the Weather_ATMOS addon and read by its Wind_Leaves.script. The defaults are the mod's.
static float ps_ssfx_ui_wl_haze_mode = 2.0f;
static float ps_ssfx_ui_wl_night_fog = 1.0f;
static float ps_ssfx_ui_wl_calm_leaves = 1.0f;
static float ps_ssfx_ui_wl_evening_leaves = 1.0f;
static float ps_ssfx_ui_wl_storm_leaves = 1.0f;
static float ps_ssfx_ui_wl_storm_dust = 1.0f;
static float ps_ssfx_ui_wl_tornado = 1.0f;
static float ps_ssfx_ui_wl_fireflies = 1.0f;
static float ps_ssfx_ui_wl_insects = 1.0f;
static float ps_ssfx_ui_wl_underground_fog = 1.0f;
static float ps_ssfx_ui_wl_indoor_fog = 1.0f;
static float ps_ssfx_ui_wl_indoor_disable_all = 0.0f;
static float ps_ssfx_ui_wl_debug_log = 0.0f;

// The author switches the interactive grass off by sending 0 for the number of benders and for
// the player flag, and the anomalies off by sending 0 for their range
// ( ssfx_interactive_grass.script:57-68 ). His menu recomputes all of it from stored settings
// every time, so the sliders keep their values while the module is off. Same thing here: the
// switches and the values they gate are stored apart, and the three numbers the engine and the
// shaders read are derived from them - after a write to any of the five, and after a write to
// either vector, so nothing can leave them out of step. With the native page switched off
// ( ssfx_menu 0 ) the author's own scripts own the vectors again and this stays out of the way.
static void ssfx_ui_apply_inter_grass() {
    if (!ps_ssfx_menu)
        return;
    const bool grass = ps_ssfx_ui_grass_enable > 0.5f;
    ps_ssfx_grass_interactive.x = grass ? ps_ssfx_ui_grass_enable_player : 0.0f;
    ps_ssfx_grass_interactive.y = grass ? ps_ssfx_ui_grass_max_entities : 0.0f;
    ps_ssfx_int_grass_params_1.w =
        ps_ssfx_ui_grass_enable_anomalies > 0.5f ? ps_ssfx_ui_grass_anomalies_distance : 0.0f;
}

class CCC_ssfx_ui_grass_float : public CCC_Float {
public:
    CCC_ssfx_ui_grass_float(LPCSTR N, float* V, float _min, float _max)
        : CCC_Float(N, V, _min, _max){};
    virtual void Execute(LPCSTR args) {
        CCC_Float::Execute(args);
        ssfx_ui_apply_inter_grass();
    }
};

class CCC_ssfx_ui_grass_bool : public CCC_ssfx_ui_bool {
public:
    CCC_ssfx_ui_grass_bool(LPCSTR N, float* V) : CCC_ssfx_ui_bool(N, V){};
    virtual void Execute(LPCSTR args) {
        CCC_ssfx_ui_bool::Execute(args);
        ssfx_ui_apply_inter_grass();
    }
};

class CCC_ssfx_grass_vector : public CCC_Vector4 {
public:
    CCC_ssfx_grass_vector(LPCSTR N, Fvector4* V, const Fvector4 _min, const Fvector4 _max)
        : CCC_Vector4(N, V, _min, _max){};
    virtual void Execute(LPCSTR args) {
        CCC_Vector4::Execute(args);
        ssfx_ui_apply_inter_grass();
    }
};

// The drop-downs of the SSFX settings page. A token's name is the author's own string id
// ( "ui_mcm_lst_" + the label his MCM list carries ), so the box shows his wording in whichever
// language is loaded. Its id is the number the engine variable takes: for the two shadow LOD
// lists that is the texture size itself, where the author's list holds an index and his applier
// turns it into the size ( ssfx_shadows.script:43-70 ).
static xr_token tok_ao_quality[] = {
    { "ui_mcm_lst_ssfx_quality_low", 2 },
    { "ui_mcm_lst_ssfx_quality_medium", 4 },
    { "ui_mcm_lst_ssfx_quality_high", 8 },
    { 0, 0 },
};
static xr_token tok_pom_quality[] = {
    { "ui_mcm_lst_ssfx_quality_low", 16 },
    { "ui_mcm_lst_ssfx_quality_medium", 24 },
    { "ui_mcm_lst_ssfx_quality_high", 36 },
    { 0, 0 },
};
static xr_token tok_ssr_quality[] = {
    { "ui_mcm_lst_ssfx_quality_very_low", 0 },
    { "ui_mcm_lst_ssfx_quality_low", 1 },
    { "ui_mcm_lst_ssfx_quality_medium", 2 },
    { "ui_mcm_lst_ssfx_quality_high", 3 },
    { "ui_mcm_lst_ssfx_quality_veryhigh", 4 },
    { "ui_mcm_lst_ssfx_quality_ultra", 5 },
    { 0, 0 },
};
static xr_token tok_water_ssr_quality[] = {
    { "ui_mcm_lst_ssfx_quality_very_low", 0 },
    { "ui_mcm_lst_ssfx_quality_low", 1 },
    { "ui_mcm_lst_ssfx_quality_medium", 2 },
    { "ui_mcm_lst_ssfx_quality_high", 3 },
    { "ui_mcm_lst_ssfx_quality_veryhigh", 4 },
    { 0, 0 },
};
static xr_token tok_water_parallax_quality[] = {
    { "ui_mcm_lst_ssfx_disable", 0 },
    { "ui_mcm_lst_ssfx_quality_low", 1 },
    { "ui_mcm_lst_ssfx_quality_medium", 2 },
    { "ui_mcm_lst_ssfx_quality_high", 3 },
    { 0, 0 },
};
static xr_token tok_rain_quality[] = {
    { "ui_mcm_lst_ssfx_quality_low", 0 },
    { "ui_mcm_lst_ssfx_quality_medium", 1 },
    { "ui_mcm_lst_ssfx_quality_high", 2 },
    { 0, 0 },
};
static xr_token tok_il_quality[] = {
    { "ui_mcm_lst_ssfx_quality_very_low", 16 },
    { "ui_mcm_lst_ssfx_quality_low", 24 },
    { "ui_mcm_lst_ssfx_quality_medium", 32 },
    { "ui_mcm_lst_ssfx_quality_high", 48 },
    { "ui_mcm_lst_ssfx_quality_veryhigh", 64 },
    { 0, 0 },
};
static xr_token tok_terrain_pom_quality[] = {
    { "ui_mcm_lst_ssfx_quality_low", 12 },
    { "ui_mcm_lst_ssfx_quality_medium", 24 },
    { "ui_mcm_lst_ssfx_quality_high", 36 },
    { 0, 0 },
};
static xr_token tok_mblur_quality[] = {
    { "ui_mcm_lst_ssfx_quality_very_low", 4 },
    { "ui_mcm_lst_ssfx_quality_low", 6 },
    { "ui_mcm_lst_ssfx_quality_medium", 8 },
    { "ui_mcm_lst_ssfx_quality_high", 12 },
    { 0, 0 },
};
static xr_token tok_shw_lod_min[] = {
    { "ui_mcm_lst_shadows_128", 128 },
    { "ui_mcm_lst_shadows_256", 256 },
    { "ui_mcm_lst_shadows_512", 512 },
    { "ui_mcm_lst_shadows_768_V", 768 },
    { "ui_mcm_lst_shadows_1536", 1536 },
    { 0, 0 },
};
static xr_token tok_shw_lod_max[] = {
    { "ui_mcm_lst_shadows_1536_V", 1536 },
    { "ui_mcm_lst_shadows_2048", 2048 },
    { "ui_mcm_lst_shadows_2560", 2560 },
    { "ui_mcm_lst_shadows_3072", 3072 },
    { "ui_mcm_lst_shadows_4096", 4096 },
    { 0, 0 },
};
static xr_token tok_shw_volumetric_quality[] = {
    { "ui_mcm_lst_ssfx_quality_very_low", 1 },
    { "ui_mcm_lst_ssfx_quality_low", 2 },
    { "ui_mcm_lst_ssfx_quality_medium", 3 },
    { "ui_mcm_lst_ssfx_quality_high", 4 },
    { "ui_mcm_lst_ssfx_quality_veryhigh", 5 },
    { 0, 0 },
};
static xr_token tok_cas_grass_shw_quality[] = {
    { "ui_mcm_lst_shw_cascades_low", 0 },
    { "ui_mcm_lst_shw_cascades_medium", 1 },
    { "ui_mcm_lst_shw_cascades_high", 2 },
    { "ui_mcm_lst_shw_cascades_ultra", 3 },
    { 0, 0 },
};
static xr_token tok_sss_quality_dir[] = {
    { "ui_mcm_lst_ssfx_quality_low", 6 },
    { "ui_mcm_lst_ssfx_quality_medium", 12 },
    { "ui_mcm_lst_ssfx_quality_high", 18 },
    { "ui_mcm_lst_ssfx_quality_veryhigh", 24 },
    { 0, 0 },
};
static xr_token tok_sss_quality_point[] = {
    { "ui_mcm_lst_ssfx_quality_low", 2 },
    { "ui_mcm_lst_ssfx_quality_medium", 4 },
    { "ui_mcm_lst_ssfx_quality_high", 6 },
    { "ui_mcm_lst_ssfx_quality_veryhigh", 8 },
    { 0, 0 },
};
// Wind Leaves haze mode: the mod's list ( wind_leaves_mcm.script ) and its own text ids
static xr_token tok_wl_haze_mode[] = {
    { "wind_leaves_haze_mode_lst_off", 0 },
    { "wind_leaves_haze_mode_lst_light", 1 },
    { "wind_leaves_haze_mode_lst_full", 2 },
    { 0, 0 },
};

// Tonemapping
ENGINE_API float ps_r2_tnmp_a = .205f;
ENGINE_API float ps_r2_tnmp_b = .35f;
ENGINE_API float ps_r2_tnmp_c = .55f;
ENGINE_API float ps_r2_tnmp_d = .20f;
ENGINE_API float ps_r2_tnmp_e = .02f;
ENGINE_API float ps_r2_tnmp_f = .15f;
ENGINE_API float ps_r2_tnmp_w = 7.5f;
ENGINE_API float ps_r2_tnmp_exposure = 7.0f;
ENGINE_API float ps_r2_tnmp_gamma = .25f;
ENGINE_API float ps_r2_tnmp_onoff = .0f;

// Mask control ( set by the bloom and combine phases )
ENGINE_API Fvector4 ps_r2_mask_control = { .0f, .0f, .0f, .0f };

// Image corrections
ENGINE_API float ps_r2_img_exposure = 1.0f;
ENGINE_API float ps_r2_img_gamma = 1.0f;
ENGINE_API float ps_r2_img_saturation = 1.0f;
ENGINE_API Fvector ps_r2_img_cg = { .0f, .0f, .0f };

// Shader params ( shader_param_1..8 )
ENGINE_API Fvector4 ps_dev_param_1 = { .0f, .0f, .0f, .0f };
ENGINE_API Fvector4 ps_dev_param_2 = { .0f, .0f, .0f, .0f };
ENGINE_API Fvector4 ps_dev_param_3 = { .0f, .0f, .0f, .0f };
ENGINE_API Fvector4 ps_dev_param_4 = { .0f, .0f, .0f, .0f };
ENGINE_API Fvector4 ps_dev_param_5 = { .0f, .0f, .0f, .0f };
ENGINE_API Fvector4 ps_dev_param_6 = { .0f, .0f, .0f, .0f };
ENGINE_API Fvector4 ps_dev_param_7 = { .0f, .0f, .0f, .0f };
ENGINE_API Fvector4 ps_dev_param_8 = { .0f, .0f, .0f, .0f };

static const struct {
    LPCSTR name;
    Fvector4* value;
} legacy_lanes[] = {
    { "shader_param_1", &ps_dev_param_1 }, { "shader_param_2", &ps_dev_param_2 },
    { "shader_param_3", &ps_dev_param_3 }, { "shader_param_4", &ps_dev_param_4 },
    { "shader_param_5", &ps_dev_param_5 }, { "shader_param_6", &ps_dev_param_6 },
    { "shader_param_7", &ps_dev_param_7 }, { "shader_param_8", &ps_dev_param_8 }
};

class CCC_BusList : public IConsole_Command {
public:
    CCC_BusList(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };

    virtual void Execute(LPCSTR args) {
        const u32 lanes = ShaderBus::count();
        Msg("[SHADER-BUS] %d lanes", lanes);

        for (u32 i = 0; i < lanes; ++i) {
            const ShaderBus::lane* l = ShaderBus::at(i);
            if (!l)
                continue;

            if (l->registered)
                Msg("[SHADER-BUS] bus_%s owner '%s' = (%f, %f, %f, %f) %s [%s changes %d writes %d "
                    "bound frame %d]",
                    l->id.c_str(), l->owner.c_str(), l->bound.x, l->bound.y, l->bound.z, l->bound.w,
                    l->description.c_str(), l->is_forced ? "forced" : "registered", l->changes,
                    l->writes, l->bound_frame);
            else
                Msg("[SHADER-BUS] bus_%s declared by shaders, not registered [%s changes %d writes "
                    "%d bound frame %d]",
                    l->id.c_str(), l->is_forced ? "forced" : "declared", l->changes, l->writes,
                    l->bound_frame);
        }

        for (u32 i = 0; i < sizeof(legacy_lanes) / sizeof(legacy_lanes[0]); ++i)
            Msg("[SHADER-BUS] %s legacy = (%f, %f, %f, %f)", legacy_lanes[i].name,
                legacy_lanes[i].value->x, legacy_lanes[i].value->y, legacy_lanes[i].value->z,
                legacy_lanes[i].value->w);
    }
};

class CCC_BusGet : public IConsole_Command {
public:
    CCC_BusGet(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };

    virtual void Execute(LPCSTR args) {
        if (!args || !args[0]) {
            Msg("~ [SHADER-BUS] usage bus_get <id>");
            return;
        }

        Fvector4 v;
        if (!ShaderBus::get(args, v)) {
            Msg("~ [SHADER-BUS] no lane named %s", args);
            return;
        }
        Msg("[SHADER-BUS] bus_%s = (%f, %f, %f, %f)", args, v.x, v.y, v.z, v.w);
    }

    virtual void Info(TInfo& I) { xr_strcpy(I, "lane id"); }
};

class CCC_BusForce : public IConsole_Command {
public:
    CCC_BusForce(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };

    virtual void Execute(LPCSTR args) {
        string64 id;
        id[0] = 0;

        Fvector4 v;
        if (!args || 5 != sscanf(args, "%63s %f %f %f %f", id, &v.x, &v.y, &v.z, &v.w)) {
            Msg("~ [SHADER-BUS] usage bus_force <id> x y z w");
            return;
        }

        if (!_finite(v.x) || !_finite(v.y) || !_finite(v.z) || !_finite(v.w)) {
            Msg("~ [SHADER-BUS] bus_force needs finite values");
            return;
        }

        if (!ShaderBus::force(id, v)) {
            Msg("~ [SHADER-BUS] no lane named %s", id);
            return;
        }
        Msg("[SHADER-BUS] bus_%s held at (%f, %f, %f, %f)", id, v.x, v.y, v.z, v.w);
    }

    virtual void Info(TInfo& I) { xr_strcpy(I, "lane id and four floats"); }
};

class CCC_BusRelease : public IConsole_Command {
public:
    CCC_BusRelease(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };

    virtual void Execute(LPCSTR args) {
        string64 id;
        id[0] = 0;

        if (!args || 1 != sscanf(args, "%63s", id)) {
            Msg("~ [SHADER-BUS] usage bus_release <id>");
            return;
        }

        if (!ShaderBus::release(id)) {
            Msg("~ [SHADER-BUS] no lane named %s", id);
            return;
        }
        Msg("[SHADER-BUS] bus_%s released", id);
    }

    virtual void Info(TInfo& I) { xr_strcpy(I, "lane id"); }
};

class CCC_Vector4Legacy : public CCC_Vector4 {
public:
    CCC_Vector4Legacy(LPCSTR N, Fvector4* V, const Fvector4 _min, const Fvector4 _max)
        : CCC_Vector4(N, V, _min, _max){};

    virtual void Execute(LPCSTR args) {
        Fvector4 v;
        if (!parse(args, v)) {
            InvalidSyntax();
            return;
        }
        value->set(v);

        LPCSTR caller = Console->ScriptCaller();
        LPCSTR writer = caller[0] ? caller : (Device.b_is_Ready ? "console" : Console->ConfigFile);
        ShaderBus::note_legacy_write(cName, writer);
    }

private:
    // Monolith keeps this parser in CCC_Vector4; the Infernis CCC_Vector4 parses inside Execute.
    bool parse(LPCSTR args, Fvector4& v) {
        if (4 != sscanf(args, "%f,%f,%f,%f", &v.x, &v.y, &v.z, &v.w)) {
            if (4 != sscanf(args, "(%f,%f,%f,%f)", &v.x, &v.y, &v.z, &v.w))
                return false;
        }

        if (v.x < min.x || v.y < min.y || v.z < min.z || v.w < min.w)
            return false;

        if (v.x > max.x || v.y > max.y || v.z > max.z || v.w > max.w)
            return false;

        return true;
    }
};

void CCC_Register() {
    // General
    CMD1(CCC_Help, "help");
    CMD1(CCC_Quit, "quit");
    CMD1(CCC_Start, "start");
    CMD1(CCC_Disconnect, "disconnect");
    CMD1(CCC_SaveCFG, "cfg_save");
    CMD1(CCC_LoadCFG, "cfg_load");

#ifdef DEBUG
    CMD1(CCC_MotionsStat, "stat_motions");
    CMD1(CCC_TexturesStat, "stat_textures");
#endif // DEBUG

#ifdef DEBUG
    CMD3(CCC_Mask, "mt_particles", &psDeviceFlags, mtParticles);

    CMD1(CCC_DbgStrCheck, "dbg_str_check");
    CMD1(CCC_DbgStrDump, "dbg_str_dump");

    CMD3(CCC_Mask, "mt_sound", &psDeviceFlags, mtSound);
    CMD3(CCC_Mask, "mt_physics", &psDeviceFlags, mtPhysics);
    CMD3(CCC_Mask, "mt_network", &psDeviceFlags, mtNetwork);

    // Events
    CMD1(CCC_E_Dump, "e_list");
    CMD1(CCC_E_Signal, "e_signal");

    CMD3(CCC_Mask, "rs_wireframe", &psDeviceFlags, rsWireframe);
    CMD3(CCC_Mask, "rs_clear_bb", &psDeviceFlags, rsClearBB);
    CMD3(CCC_Mask, "rs_occlusion", &psDeviceFlags, rsOcclusion);

    CMD3(CCC_Mask, "rs_detail", &psDeviceFlags, rsDetails);
    // CMD4(CCC_Float,		"r__dtex_range",		&r__dtex_range,		5,		175
    // );

    //	CMD3(CCC_Mask,		"rs_constant_fps",		&psDeviceFlags,		rsConstantFPS
    //);
    CMD3(CCC_Mask, "rs_render_statics", &psDeviceFlags, rsDrawStatic);
    CMD3(CCC_Mask, "rs_render_dynamics", &psDeviceFlags, rsDrawDynamic);
#endif

    // Render device states
    CMD4(CCC_Integer, "r__supersample", &ps_r__Supersample, 1, 4);

    CMD3(CCC_Mask, "rs_v_sync", &psDeviceFlags, rsVSync);
    //	CMD3(CCC_Mask,		"rs_disable_objects_as_crows",&psDeviceFlags,	rsDisableObjectsAsCrows
    //);
    CMD3(CCC_Mask, "rs_fullscreen", &psDeviceFlags, rsFullscreen);
    CMD3(CCC_Mask, "rs_refresh_60hz", &psDeviceFlags, rsRefresh60hz);
    CMD3(CCC_Mask, "rs_stats", &psDeviceFlags, rsStatistic);
    CMD4(CCC_Float, "rs_vis_distance", &psVisDistance, 0.4f, 1.5f);

    CMD3(CCC_Mask, "rs_cam_pos", &psDeviceFlags, rsCameraPos);
#ifdef DEBUG
    CMD3(CCC_Mask, "rs_occ_draw", &psDeviceFlags, rsOcclusionDraw);
    CMD3(CCC_Mask, "rs_occ_stats", &psDeviceFlags, rsOcclusionStats);
// CMD4(CCC_Integer,	"rs_skeleton_update",	&psSkeletonUpdate,	2,		128	);
#endif // DEBUG

    CMD2(CCC_Gamma, "rs_c_gamma", &ps_gamma);
    CMD2(CCC_Gamma, "rs_c_brightness", &ps_brightness);
    CMD2(CCC_Gamma, "rs_c_contrast", &ps_contrast);
    //	CMD4(CCC_Integer,	"rs_vb_size",			&rsDVB_Size,		32,
    //4096); 	CMD4(CCC_Integer,	"rs_ib_size",			&rsDIB_Size,		32,
    //4096);

    // Texture manager
    CMD4(CCC_Integer, "texture_lod", &psTextureLOD, 0, 4);

    // General video control
    CMD1(CCC_VidMode, "vid_mode");

#ifdef DEBUG
    CMD3(CCC_Token, "vid_bpp", &psCurrentBPP, vid_bpp_token);
#endif // DEBUG

    CMD1(CCC_VID_Reset, "vid_restart");

    // Sound
    CMD2(CCC_Float, "snd_volume_eff", &psSoundVEffects);
    CMD2(CCC_Float, "snd_volume_music", &psSoundVMusic);
    CMD3(CCC_String, "snd_language", snd_language, 32);
    CMD1(CCC_SND_Restart, "snd_restart");
    CMD3(CCC_Mask, "snd_acceleration", &psSoundFlags, ss_Hardware);
	// КОМАНДИ NOIR ENGINE
    CMD3(CCC_Mask, "snd_efx", &psSoundFlags, ss_EFX); // Керує EFX
    CMD4(CCC_Integer, "snd_acoustics", &psSoundAcoustics, 0, 1);
    CMD3(CCC_Token, "snd_acoustics_preset", &psSoundAcousticsPreset, snd_acoustics_preset_token);
    CMD4(CCC_Integer, "snd_hrtf", &snd_hrtf, 0, 1);   // Керує HRTF
    CMD4(CCC_Integer, "snd_targets", &psSoundTargets, 4, 256);
    CMD4(CCC_Integer, "snd_cache_size", &psSoundCacheSizeMB, 4, 32);

#ifdef DEBUG
    CMD3(CCC_Mask, "snd_stats", &g_stats_flags, st_sound);
    CMD3(CCC_Mask, "snd_stats_min_dist", &g_stats_flags, st_sound_min_dist);
    CMD3(CCC_Mask, "snd_stats_max_dist", &g_stats_flags, st_sound_max_dist);
    CMD3(CCC_Mask, "snd_stats_ai_dist", &g_stats_flags, st_sound_ai_dist);
    CMD3(CCC_Mask, "snd_stats_info_name", &g_stats_flags, st_sound_info_name);
    CMD3(CCC_Mask, "snd_stats_info_object", &g_stats_flags, st_sound_info_object);

    CMD4(CCC_Integer, "error_line_count", &g_ErrorLineCount, 6, 1024);
#endif // DEBUG

    // Mouse
    CMD3(CCC_Mask, "mouse_invert", &psMouseInvert, 1);
    psMouseSens = 0.12f;
    CMD4(CCC_Float, "mouse_sens", &psMouseSens, 0.001f, 0.6f);

    // Camera
    CMD2(CCC_Float, "cam_inert", &psCamInert);
    CMD2(CCC_Float, "cam_slide_inert", &psCamSlideInert);

    CMD1(CCC_r2, "renderer");

    CMD1(CCC_soundDevice, "snd_device");
    CMD1(CCC_SoundDevicesRefresh, "snd_device_refresh");
    CMD1(CCC_SoundStatus, "snd_status");
    CMD3(CCC_Token, "snd_output", &snd_output, snd_output_token);
    // psSoundRolloff	= pSettings->r_float	("sound","rolloff");		clamp(psSoundRolloff,			EPS_S,
    // 2.f);
    psSoundOcclusionScale = pSettings->r_float("sound", "occlusion_scale");
    clamp(psSoundOcclusionScale, 0.1f, .5f);

    extern int g_Dump_Export_Obj;
    extern int g_Dump_Import_Obj;
    CMD4(CCC_Integer, "net_dbg_dump_export_obj", &g_Dump_Export_Obj, 0, 1);
    CMD4(CCC_Integer, "net_dbg_dump_import_obj", &g_Dump_Import_Obj, 0, 1);

#ifdef DEBUG
    CMD1(CCC_DumpOpenFiles, "dump_open_files");
#endif

    CMD1(CCC_ExclusiveMode, "input_exclusive_mode");

    extern int g_svTextConsoleUpdateRate;
    CMD4(CCC_Integer, "sv_console_update_rate", &g_svTextConsoleUpdateRate, 1, 100);

    CMD1(CCC_HideConsole, "hide");

    // Screen Space Shaders
    {
        Fvector4 tw2_min = { -100.f, -100.f, -100.f, -100.f };
        Fvector4 tw2_max = { 100.f, 100.f, 100.f, 100.f };

        CMD4(CCC_Float, "r2_tnmp_a", &ps_r2_tnmp_a, 0.0f, 20.0f);
        CMD4(CCC_Float, "r2_tnmp_b", &ps_r2_tnmp_b, 0.0f, 20.0f);
        CMD4(CCC_Float, "r2_tnmp_c", &ps_r2_tnmp_c, 0.0f, 20.0f);
        CMD4(CCC_Float, "r2_tnmp_d", &ps_r2_tnmp_d, 0.0f, 20.0f);
        CMD4(CCC_Float, "r2_tnmp_e", &ps_r2_tnmp_e, 0.0f, 20.0f);
        CMD4(CCC_Float, "r2_tnmp_f", &ps_r2_tnmp_f, 0.0f, 20.0f);
        CMD4(CCC_Float, "r2_tnmp_w", &ps_r2_tnmp_w, 0.0f, 20.0f);
        CMD4(CCC_Float, "r2_tnmp_exposure", &ps_r2_tnmp_exposure, 0.0f, 20.0f);
        CMD4(CCC_Float, "r2_tnmp_gamma", &ps_r2_tnmp_gamma, 0.0f, 20.0f);
        CMD4(CCC_Float, "r2_tnmp_onoff", &ps_r2_tnmp_onoff, 0.0f, 1.0f);

        CMD4(CCC_Vector4, "r2_mask_control", &ps_r2_mask_control, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(10, 3, 1, 1));

        CMD4(CCC_Float, "r__exposure", &ps_r2_img_exposure, 0.5f, 4.0f);
        CMD4(CCC_Float, "r__gamma", &ps_r2_img_gamma, 0.5f, 2.2f);
        CMD4(CCC_Float, "r__saturation", &ps_r2_img_saturation, 0.0f, 2.0f);
        CMD4(CCC_Vector3, "r__color_grading", &ps_r2_img_cg, Fvector3().set(0, 0, 0),
             Fvector3().set(1, 1, 1));

        // Shader param stuff
        CMD4(CCC_Vector4Legacy, "shader_param_1", &ps_dev_param_1, tw2_min, tw2_max);
        CMD4(CCC_Vector4Legacy, "shader_param_2", &ps_dev_param_2, tw2_min, tw2_max);
        CMD4(CCC_Vector4Legacy, "shader_param_3", &ps_dev_param_3, tw2_min, tw2_max);
        CMD4(CCC_Vector4Legacy, "shader_param_4", &ps_dev_param_4, tw2_min, tw2_max);
        CMD4(CCC_Vector4Legacy, "shader_param_5", &ps_dev_param_5, tw2_min, tw2_max);
        CMD4(CCC_Vector4Legacy, "shader_param_6", &ps_dev_param_6, tw2_min, tw2_max);
        CMD4(CCC_Vector4Legacy, "shader_param_7", &ps_dev_param_7, tw2_min, tw2_max);
        CMD4(CCC_Vector4Legacy, "shader_param_8", &ps_dev_param_8, tw2_min, tw2_max);

        CMD1(CCC_BusList, "bus_list");
        CMD1(CCC_BusGet, "bus_get");
        CMD1(CCC_BusForce, "bus_force");
        CMD1(CCC_BusRelease, "bus_release");

        CMD4(CCC_Vector4, "ssfx_floravariation", &ps_ssfx_floravariation, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(10, 1, 10, 1));
        CMD4(CCC_Vector4, "ssfx_taa", &ps_ssfx_taa, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(1, 1, 2, 1));
        CMD4(CCC_Vector4, "ssfx_motionblur", &ps_ssfx_motionblur, Fvector4().set(1, 0, 0, 0),
             Fvector4().set(16, 2, 1, 100));
        CMD4(CCC_Float, "ssfx_fog_scattering", &ps_ssfx_fog_scattering, 0, 1);
        CMD4(CCC_Vector4, "ssfx_fog", &ps_ssfx_fog, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(20, 5, 1, 100));

        CMD4(CCC_Integer, "ssfx_pom_refine", &ps_ssfx_pom_refine, 0, 1);
        CMD4(CCC_Vector4, "ssfx_pom", &ps_ssfx_pom, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(36, 60, 1, 1));

        CMD4(CCC_Integer, "ssfx_terrain_pom_refine", &ps_ssfx_terrain_pom_refine, 0, 1);
        CMD4(CCC_Integer, "ssfx_terrain_grass_align", &ps_ssfx_terrain_grass_align, 0, 1);
        CMD4(CCC_Float, "ssfx_terrain_grass_slope", &ps_ssfx_terrain_grass_slope, 0, 1);
        CMD4(CCC_Vector4, "ssfx_terrain_pom", &ps_ssfx_terrain_pom, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(36, 60, 1, 2));

        CMD4(CCC_Integer, "ssfx_bloom_use_presets", &ps_ssfx_bloom_use_presets, 0, 1);
        CMD4(CCC_Vector4, "ssfx_bloom_1", &ps_ssfx_bloom_1, Fvector4().set(1, 1, 0, 0),
             Fvector4().set(10, 100, 100, 10));
        CMD4(CCC_Vector4, "ssfx_bloom_2", &ps_ssfx_bloom_2, Fvector4().set(1, 0, 0, 0),
             Fvector4().set(5, 10, 10, 10));
        CMD4(CCC_Vector4, "ssfx_sss_quality", &ps_ssfx_sss_quality, Fvector4().set(1, 1, 0, 0),
             Fvector4().set(24, 12, 1, 1));
        CMD4(CCC_Vector4, "ssfx_sss", &ps_ssfx_sss, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(3, 3, 1, 1));

        CMD4(CCC_Float, "ssfx_hud_hemi", &ps_ssfx_hud_hemi, 0.0f, 1.0f);

        CMD4(CCC_Integer, "ssfx_il_quality", &ps_ssfx_il_quality, 16, 64);
        CMD4(CCC_Vector4, "ssfx_il", &ps_ssfx_il, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(8, 10, 3, 6));
        CMD4(CCC_Vector4, "ssfx_il_setup1", &ps_ssfx_il_setup1, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(300, 1, 1, 1));

        CMD4(CCC_Integer, "ssfx_ao_quality", &ps_ssfx_ao_quality, 2, 8);
        CMD4(CCC_Vector4, "ssfx_ao", &ps_ssfx_ao, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(8, 10, 1, 10));
        CMD4(CCC_Vector4, "ssfx_ao_setup1", &ps_ssfx_ao_setup1, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(300, 1, 1, 1));

        CMD4(CCC_Vector4, "ssfx_water", &ps_ssfx_water, Fvector4().set(1, 0, 0, 0),
             Fvector4().set(8, 1, 1, 0));
        CMD4(CCC_Vector3, "ssfx_water_quality", &ps_ssfx_water_quality, Fvector3().set(0, 0, 0),
             Fvector3().set(4, 3, 0));
        CMD4(CCC_Vector4, "ssfx_water_setup1", &ps_ssfx_water_setup1, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(2, 10, 1, 0.1f));
        CMD4(CCC_Vector4, "ssfx_water_setup2", &ps_ssfx_water_setup2, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(1, 10, 1, 1));

        CMD4(CCC_Integer, "ssfx_ssr_quality", &ps_ssfx_ssr_quality, 0, 5);
        CMD4(CCC_Vector4, "ssfx_ssr", &ps_ssfx_ssr, Fvector4().set(1, 0, 0, 0),
             Fvector4().set(2, 1, 1, 1));
        CMD4(CCC_Vector4, "ssfx_ssr_2", &ps_ssfx_ssr_2, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(2, 2, 2, 2));

        // Terrain detail distance: up to 100 here, the donor stops at 40 ( user 2026-10-23, r4.cpp ).
        CMD4(CCC_Vector4, "ssfx_terrain_quality", &ps_ssfx_terrain_quality,
             Fvector4().set(0, 0, 0, 0), Fvector4().set(100, 0, 0, 0));
        CMD4(CCC_Vector4, "ssfx_terrain_offset", &ps_ssfx_terrain_offset,
             Fvector4().set(-1, -1, -1, -1), Fvector4().set(1, 1, 1, 1));

        CMD4(CCC_Vector3, "ssfx_shadows", &ps_ssfx_shadows, Fvector3().set(128, 1536, 0),
             Fvector3().set(1536, 4096, 0));
        CMD4(CCC_Vector4, "ssfx_volumetric", &ps_ssfx_volumetric, Fvector4().set(0, 0, 1.0f, 1.0f),
             Fvector4().set(1.0f, 5.0f, 5.0f, 16.0f));

        CMD4(CCC_Vector3, "ssfx_shadow_bias", &ps_ssfx_shadow_bias, Fvector3().set(0, 0, 0),
             Fvector3().set(1.0f, 1.0f, 1.0f));

        CMD4(CCC_Vector4, "ssfx_lut", &ps_ssfx_lut, Fvector4().set(0.0f, 0.0f, 0.0f, 0.0f), tw2_max);

        CMD4(CCC_Vector4, "ssfx_wind_grass", &ps_ssfx_wind_grass,
             Fvector4().set(0.0f, 0.0f, 0.0f, 0.0f), Fvector4().set(20.0f, 5.0f, 5.0f, 5.0f));
        CMD4(CCC_Vector4, "ssfx_wind_trees", &ps_ssfx_wind_trees,
             Fvector4().set(0.0f, 0.0f, 0.0f, 0.0f), Fvector4().set(20.0f, 5.0f, 5.0f, 1.0f));

        CMD4(CCC_Vector4, "ssfx_florafixes_1", &ps_ssfx_florafixes_1,
             Fvector4().set(0.0f, 0.0f, 0.0f, 0.0f), Fvector4().set(1.0f, 1.0f, 1.0f, 1.0f));
        CMD4(CCC_Vector4, "ssfx_florafixes_2", &ps_ssfx_florafixes_2,
             Fvector4().set(0.0f, 0.0f, 0.0f, 0.0f), Fvector4().set(10.0f, 1.0f, 1.0f, 1.0f));

        CMD4(CCC_Vector4, "ssfx_wetsurfaces_1", &ps_ssfx_wetsurfaces_1,
             Fvector4().set(0.01f, 0.01f, 0.01f, 0.01f), Fvector4().set(2.0f, 2.0f, 2.0f, 2.0f));
        CMD4(CCC_Vector4, "ssfx_wetsurfaces_2", &ps_ssfx_wetsurfaces_2,
             Fvector4().set(0.01f, 0.01f, 0.01f, 0.01f), Fvector4().set(2.0f, 2.0f, 2.0f, 2.0f));

        CMD4(CCC_Integer, "ssfx_is_underground", &ps_ssfx_is_underground, 0, 1);
        CMD4(CCC_Integer, "ssfx_gloss_method", &ps_ssfx_gloss_method, 0, 1);
        // Screen Space Shaders: ssfx_001_mcm writes r__fakescope on every start. It switches
        // Anomaly's shader-based 2D scopes, which this engine does not have - it has its own
        // 3D scope - so the flag is registered for the script's sake and read by nothing.
        CMD4(CCC_Integer, "r__fakescope", &ps_ssfx_fakescope, 0, 1);
        CMD4(CCC_Vector4, "ssfx_gloss_minmax", &ps_ssfx_gloss_minmax, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(1.0f, 1.0f, 1.0f, 1.0f));
        CMD4(CCC_Float, "ssfx_gloss_factor", &ps_ssfx_gloss_factor, 0.0f, 1.0f);

        CMD4(CCC_Vector4, "ssfx_lightsetup_1", &ps_ssfx_lightsetup_1, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(1.0f, 1.0f, 1.0f, 1.0f));

        // Screen Space Shaders: the pseudo-PBR module ( pbr_settings.h ). The four vectors change on
        // the fly; the two switches are compiled into the shaders ( r4.cpp ) and need a restart.
        CMD4(CCC_Vector4, "ssfx_es_albedo", &ps_ssfx_es_albedo, Fvector4().set(0.0f, 0.5f, 0.0f, 0.0f),
             Fvector4().set(1.0f, 1.5f, 0.0f, 0.0f));
        CMD4(CCC_Vector4, "ssfx_es_rough", &ps_ssfx_es_rough, Fvector4().set(0.15f, 0.15f, 0.75f, 0.0f),
             Fvector4().set(1.0f, 1.0f, 4.0f, 0.0f));
        CMD4(CCC_Vector4, "ssfx_es_spec", &ps_ssfx_es_spec, Fvector4().set(0.0f, 0.0f, 0.0f, 0.0f),
             Fvector4().set(0.1f, 2.0f, 4.0f, 0.0f));
        CMD4(CCC_Vector4, "ssfx_es_metal", &ps_ssfx_es_metal, Fvector4().set(0.0f, 0.01f, 0.0f, 0.0f),
             Fvector4().set(0.5f, 1.0f, 0.0f, 0.0f));
        CMD4(CCC_Integer, "ssfx_es_burley", &ps_ssfx_es_burley, 0, 1);
        CMD4(CCC_Integer, "ssfx_es_ggx", &ps_ssfx_es_ggx, 0, 1);
        // The eight numbers the donor writes into the formulas ( see the declarations ). The lowest
        // water roughness keeps the GGX highlight finite: at 0 it divides zero by zero.
        CMD4(CCC_Vector4, "ssfx_es_light", &ps_ssfx_es_light, Fvector4().set(1.0f, 0.5f, 1.0f, 0.0f),
             Fvector4().set(3.0f, 4.0f, 32.0f, 0.0f));
        CMD4(CCC_Vector4, "ssfx_es_flora", &ps_ssfx_es_flora, Fvector4().set(0.0f, 0.0f, 0.0f, 0.0f),
             Fvector4().set(0.2f, 1.0f, 2.0f, 0.0f));
        CMD4(CCC_Vector4, "ssfx_es_water", &ps_ssfx_es_water, Fvector4().set(0.0f, 0.05f, 0.0f, 0.0f),
             Fvector4().set(0.1f, 0.5f, 0.0f, 0.0f));

        CMD4(CCC_Vector4, "ssfx_hud_drops_1", &ps_ssfx_hud_drops_1, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(100000, 100, 100, 100));
        CMD4(CCC_Vector4, "ssfx_hud_drops_2", &ps_ssfx_hud_drops_2, Fvector4().set(0, 0, 0, 0),
             tw2_max);

        CMD4(CCC_Vector4, "ssfx_blood_decals", &ps_ssfx_blood_decals, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(5, 5, 0, 0));

        CMD4(CCC_Vector4, "ssfx_rain_drops_setup", &ps_ssfx_rain_drops_setup,
             Fvector4().set(1000, 10, 0, 0), Fvector4().set(5000, 30, 0, 0));
        CMD4(CCC_Vector4, "ssfx_rain_1", &ps_ssfx_rain_1, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(10, 5, 5, 2));
        CMD4(CCC_Vector4, "ssfx_rain_2", &ps_ssfx_rain_2, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(1, 10, 10, 10));
        CMD4(CCC_Vector4, "ssfx_rain_3", &ps_ssfx_rain_3, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(1, 10, 10, 10));

        CMD4(CCC_Vector4, "ssfx_grass_shadows", &ps_ssfx_grass_shadows, Fvector4().set(0, 0, 0, 0),
             Fvector4().set(3, 1, 100, 100));
        // ssfx_shadow_cascades is registered by the renderer ( xrRender_console.cpp ), because
        // changing it has to call CRender::init_cacades().

        // The first two carry the three numbers the interactive-grass switches gate, so a write
        // to either is followed by ssfx_ui_apply_inter_grass().
        CMD4(CCC_ssfx_grass_vector, "ssfx_grass_interactive", &ps_ssfx_grass_interactive,
             Fvector4().set(0, 0, 0, 0), Fvector4().set(1, 15, 5000, 1));
        CMD4(CCC_ssfx_grass_vector, "ssfx_int_grass_params_1", &ps_ssfx_int_grass_params_1,
             Fvector4().set(0, 0, 0, 0), Fvector4().set(5, 5, 5, 60));
        CMD4(CCC_Vector4, "ssfx_int_grass_params_2", &ps_ssfx_int_grass_params_2,
             Fvector4().set(0, 0, 0, 0), Fvector4().set(5, 20, 1, 5));

        CMD4(CCC_Vector4, "ssfx_wpn_dof_1", &ps_ssfx_wpn_dof_1, tw2_min, tw2_max);
        CMD4(CCC_Float, "ssfx_wpn_dof_2", &ps_ssfx_wpn_dof_2, 0, 1);

        CMD4(CCC_Vector3, "ssfx_wetness_multiplier", &ssfx_wetness_multiplier,
             Fvector3().set(0.1f, 0.1f, 0.0f), Fvector3().set(20.0f, 20.0f, 0.0f));

        CMD4(CCC_Integer, "ssfx_auto_settings", &ps_ssfx_auto_settings, 0, 1);
        CMD4(CCC_Integer, "ssfx_menu", &ps_ssfx_menu, 0, 1);
        CMD4(CCC_Integer, "ssfx_menu_configured", &ps_ssfx_menu_configured, 0, 1);
        // SSS on or off ( ssfx_console_vars.h ). The renderer and the scripts take it when the game
        // starts, so a change shows after the game is started again.
        CMD4(CCC_Integer, "ssfx_enable", &ps_ssfx_enable, 0, 1);

        // Monolith xrRender_console.cpp:1339: the actor's own shadow in first person ( DX11 only ).
        CMD3(CCC_Mask, "r__actor_shadow", &ps_actor_shadow_flags, RFLAG_ACTOR_SHADOW);

        // The native SSFX settings page: one console command per control.
        //
        // A menu control goes through CUIOptionsItem, which reads its value and its bounds from
        // the console and writes back with "<command> <value>". That takes a command owning
        // exactly one number, and nearly every SSFX setting is one component of an Fvector4
        // registered as CCC_Vector4. An Fvector4 is four contiguous floats, so a command aimed at
        // one of its members addresses the very memory the shader binders read - there is no copy
        // to keep in sync. The class follows the widget and the variable: CCC_Float for a slider,
        // CCC_Integer where the engine variable is an int, CCC_ssfx_ui_bool for a check box on a
        // float, CCC_ssfx_ui_list for a drop-down ( all three are in xr_ioc_cmd.h ).
        //
        // EVERY RANGE IS THE AUTHOR'S OWN SLIDER RANGE from his MCM module, carried through
        // whatever arithmetic his applier script performs on the way to the console. Where he
        // multiplies ( AO global intensity x5 ) or inverts ( render scale 1/x ) the range here is
        // the transformed one, because that is what the variable stores. No position of a control
        // can produce a number the author would not produce himself - a wider range is a way to
        // break the picture, a narrower one hides part of his. Each line names his setting, and
        // the groups follow the order of his own menu.
        //
        // The views onto variables that live in the renderer - the shadow LOD quality, the sun
        // cascade sizes, the rain cover map - are registered in xrRender_console.cpp.

        // --- ao - ssfx_ao_mcm.script
        // quality
        CMD3(CCC_ssfx_ui_list_i, "ssfx_ui_ao_quality", &ps_ssfx_ao_quality, tok_ao_quality);
        // res: his 0.25 .. 1 through 1.0/X ( transform from ssfx_ao.script:54 )
        CMD4(CCC_Float, "ssfx_ui_ao_res", &ps_ssfx_ao.x, 1.0f, 4.0f);
        CMD4(CCC_Float, "ssfx_ui_ao_dist", &ps_ssfx_ao_setup1.x, 20.0f, 300.0f); // distance
        CMD4(CCC_Float, "ssfx_ui_ao_blur", &ps_ssfx_ao.z, 0.0f, 1.0f); // blur
        CMD4(CCC_Float, "ssfx_ui_ao_radius", &ps_ssfx_ao.w, 1.0f, 5.0f); // radius
        CMD4(CCC_Float, "ssfx_ui_ao_max_occ", &ps_ssfx_ao_setup1.w, 0.0f, 1.0f); // max_occ
        // global_int: his 0 .. 2 through X * 5
        CMD4(CCC_Float, "ssfx_ui_ao_int", &ps_ssfx_ao.y, 0.0f, 10.0f);
        CMD4(CCC_Float, "ssfx_ui_ao_hud_int", &ps_ssfx_ao_setup1.y, 0.0f, 1.0f); // hud_int
        CMD4(CCC_Float, "ssfx_ui_ao_flora_int", &ps_ssfx_ao_setup1.z, 0.0f, 1.0f); // flora_int

        // --- parallax - ssfx_parallax_mcm.script
        CMD3(CCC_ssfx_ui_list, "ssfx_ui_pom_quality", &ps_ssfx_pom.x, tok_pom_quality); // quality
        CMD4(CCC_Integer, "ssfx_ui_pom_refine", &ps_ssfx_pom_refine, 0, 1); // refine
        CMD4(CCC_Float, "ssfx_ui_pom_range", &ps_ssfx_pom.y, 0.0f, 40.0f); // range
        CMD4(CCC_Float, "ssfx_ui_pom_height", &ps_ssfx_pom.z, 0.01f, 0.05f); // height
        CMD4(CCC_Float, "ssfx_ui_pom_ao", &ps_ssfx_pom.w, 0.0f, 1.0f); // ao

        // --- ssr - ssfx_ssr_mcm.script
        // quality
        CMD3(CCC_ssfx_ui_list_i, "ssfx_ui_ssr_quality", &ps_ssfx_ssr_quality, tok_ssr_quality);
        // render_scale: his 0.3 .. 1 through 1.0/X ( range limited: the donor's ssfx_ssr accepts
        // the divisor only in 1..2 )
        CMD4(CCC_Float, "ssfx_ui_ssr_render_scale", &ps_ssfx_ssr.x, 1.0f, 2.0f);
        CMD4(CCC_Float, "ssfx_ui_ssr_blur", &ps_ssfx_ssr.y, 0.0f, 1.0f); // blur
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_ssr_use_noise", &ps_ssfx_ssr.w); // use_noise
        // general_int. The overall strength, which is .x ( screenspace_reflections.h:17 ); the
        // donor's comment on the vector lists the components in another order and is wrong
        CMD4(CCC_Float, "ssfx_ui_ssr_int", &ps_ssfx_ssr_2.x, 0.0f, 2.0f);
        CMD4(CCC_Float, "ssfx_ui_ssr_sky_int", &ps_ssfx_ssr_2.y, 0.0f, 2.0f); // sky_int
        CMD4(CCC_Float, "ssfx_ui_ssr_weapon_int", &ps_ssfx_ssr_2.z, 0.0f, 2.0f); // weapon_int
        // weapon_int_max
        CMD4(CCC_Float, "ssfx_ui_ssr_weapon_int_max", &ps_ssfx_ssr_2.w, 0.0f, 1.0f);

        // --- water - ssfx_water_mcm.script
        // ssr_quality
        CMD3(CCC_ssfx_ui_list, "ssfx_ui_water_ssr_quality", &ps_ssfx_water_quality.x,
             tok_water_ssr_quality);
        // ssr_res: his 0.2 .. 1 through 1.0/X ( transform from ssfx_water.script:60 )
        CMD4(CCC_Float, "ssfx_ui_water_ssr_res", &ps_ssfx_water.x, 1.0f, 5.0f);
        // parallax_quality
        CMD3(CCC_ssfx_ui_list, "ssfx_ui_water_parallax_quality", &ps_ssfx_water_quality.y,
             tok_water_parallax_quality);
        // parallax_height
        CMD4(CCC_Float, "ssfx_ui_water_parallax_height", &ps_ssfx_water_setup1.w, 0.01f, 0.1f);
        CMD4(CCC_Float, "ssfx_ui_water_blur", &ps_ssfx_water.y, 0.0f, 1.0f); // blur
        CMD4(CCC_Float, "ssfx_ui_water_blur_pattern", &ps_ssfx_water.z, 0.0f, 1.0f); // blur_pattern
        // distortion
        CMD4(CCC_Float, "ssfx_ui_water_distortion", &ps_ssfx_water_setup1.x, 0.0f, 2.0f);
        // turbidity: his 0 .. 1 through X * 10
        CMD4(CCC_Float, "ssfx_ui_water_turbidity", &ps_ssfx_water_setup1.y, 0.0f, 10.0f);
        // softborder
        CMD4(CCC_Float, "ssfx_ui_water_softborder", &ps_ssfx_water_setup1.z, 0.0f, 1.0f);
        // reflection_int
        CMD4(CCC_Float, "ssfx_ui_water_reflect", &ps_ssfx_water_setup2.x, 0.0f, 1.0f);
        // specular_int: his 0 .. 1 through X * 6
        CMD4(CCC_Float, "ssfx_ui_water_specular_int", &ps_ssfx_water_setup2.y, 0.0f, 6.0f);
        // caustics_int
        CMD4(CCC_Float, "ssfx_ui_water_caustics", &ps_ssfx_water_setup2.z, 0.0f, 1.0f);
        // ripples_int
        CMD4(CCC_Float, "ssfx_ui_water_ripples_int", &ps_ssfx_water_setup2.w, 0.0f, 1.0f);

        // --- ssfx_rain_main - ssfx_rain_mcm.script
        // quality
        CMD3(CCC_ssfx_ui_list, "ssfx_ui_rain_quality", &ps_ssfx_rain_1.w, tok_rain_quality);
        // max_drops
        CMD4(CCC_Float, "ssfx_ui_rain_max_drops", &ps_ssfx_rain_drops_setup.x, 1000.0f, 5000.0f);
        CMD4(CCC_Float, "ssfx_ui_rain_radius", &ps_ssfx_rain_drops_setup.y, 10.0f, 30.0f); // radius
        CMD4(CCC_Float, "ssfx_ui_rain_speed", &ps_ssfx_rain_1.z, 0.1f, 2.0f); // speed
        CMD4(CCC_Float, "ssfx_ui_rain_len", &ps_ssfx_rain_1.x, 0.1f, 5.0f); // len
        CMD4(CCC_Float, "ssfx_ui_rain_width", &ps_ssfx_rain_1.y, 0.01f, 0.5f); // width
        CMD4(CCC_Float, "ssfx_ui_rain_alpha", &ps_ssfx_rain_2.x, 0.0f, 1.0f); // alpha
        CMD4(CCC_Float, "ssfx_ui_rain_bright", &ps_ssfx_rain_2.y, 0.0f, 1.0f); // brightness
        CMD4(CCC_Float, "ssfx_ui_rain_refraction", &ps_ssfx_rain_2.z, 0.0f, 10.0f); // refraction
        CMD4(CCC_Float, "ssfx_ui_rain_reflection", &ps_ssfx_rain_2.w, 0.0f, 1.0f); // reflection
        CMD4(CCC_Float, "ssfx_ui_splash_alpha", &ps_ssfx_rain_3.x, 0.0f, 1.0f); // splash_alpha
        // splash_refraction
        CMD4(CCC_Float, "ssfx_ui_rain_splash_refraction", &ps_ssfx_rain_3.y, 0.0f, 10.0f);

        // --- ssfx_rain_hud_raindrops - ssfx_rain_mcm.script
        // density: his 0.5 .. 3 through 0.15 * (3.5 - X)
        CMD4(CCC_Float, "ssfx_ui_drops_density", &ps_ssfx_hud_drops_2.x, 0.075f, 0.45f);
        // reflection_str: his 0.5 .. 1.5 through 30 * X. ssfx_rain_hud_raindrops.script reads this
        // view back every frame, because it has to send all four components of ssfx_hud_drops_1 at
        // once
        CMD4(CCC_Float, "ssfx_ui_drops_refle", &ps_ssfx_hud_drops_1.z, 15.0f, 45.0f);
        // refraction_str: his 0.5 .. 1.5 through 0.05 * X. read back by the script, as above
        CMD4(CCC_Float, "ssfx_ui_drops_refra", &ps_ssfx_hud_drops_1.w, 0.025f, 0.075f);
        // animation_speed
        CMD4(CCC_Float, "ssfx_ui_drops_animation_speed", &ps_ssfx_ui_drops_animation_speed, 0.5f,
             1.5f);
        CMD4(CCC_Float, "ssfx_ui_drops_buildup", &ps_ssfx_ui_drops_buildup, 0.5f, 2.0f); // buildup
        CMD4(CCC_Float, "ssfx_ui_drops_drying", &ps_ssfx_ui_drops_drying, 0.5f, 2.0f); // drying
        // size: his 0.5 .. 1.5 through 2.0 - X
        CMD4(CCC_Float, "ssfx_ui_drops_size", &ps_ssfx_hud_drops_2.y, 0.5f, 1.5f);
        CMD4(CCC_Float, "ssfx_ui_drops_gloss", &ps_ssfx_hud_drops_2.w, 0.0f, 10.0f); // gloss
        // extra_gloss
        CMD4(CCC_Float, "ssfx_ui_drops_extragloss", &ps_ssfx_hud_drops_2.z, 0.0f, 1.0f);

        // --- ssfx_rain_footsteps - ssfx_rain_mcm.script
        CMD4(CCC_Float, "ssfx_ui_fs_main_vol", &ps_ssfx_ui_fs_main_vol, 0.0f, 1.0f); // main_vol
        CMD4(CCC_Float, "ssfx_ui_fs_vol_rnd", &ps_ssfx_ui_fs_vol_rnd, 0.0f, 1.0f); // vol_rnd
        // multi_no_rain
        CMD4(CCC_Float, "ssfx_ui_fs_multi_no_rain", &ps_ssfx_ui_fs_multi_no_rain, 0.0f, 1.0f);
        // multi_walk
        CMD4(CCC_Float, "ssfx_ui_fs_multi_walk", &ps_ssfx_ui_fs_multi_walk, 0.0f, 1.0f);
        CMD4(CCC_Float, "ssfx_ui_fs_multi_run", &ps_ssfx_ui_fs_multi_run, 0.0f, 2.0f); // multi_run
        CMD4(CCC_Float, "ssfx_ui_fs_jump_vol", &ps_ssfx_ui_fs_jump_vol, 0.0f, 1.0f); // jump_vol
        CMD4(CCC_Float, "ssfx_ui_fs_land_vol", &ps_ssfx_ui_fs_land_vol, 0.0f, 1.0f); // land_vol

        // --- wpn_dof - ssfx_weapons_dof_mcm.script
        // aim_fadestart
        CMD4(CCC_Float, "ssfx_ui_wdof_aim_fadestart", &ps_ssfx_ui_wdof_aim_fadestart, 0.0f, 0.3f);
        // aim_fadelen
        CMD4(CCC_Float, "ssfx_ui_wdof_aim_fadelen", &ps_ssfx_ui_wdof_aim_fadelen, 0.0f, 1.3f);
        CMD4(CCC_Float, "ssfx_ui_wdof_aim_blur", &ps_ssfx_ui_wdof_aim_blur, 0.0f, 2.0f); // aim_blur
        // aim_edgeblur
        CMD4(CCC_Float, "ssfx_ui_wdof_aim_edgeblur", &ps_ssfx_ui_wdof_aim_edgeblur, 0.0f, 1.0f);
        // fadestart
        CMD4(CCC_Float, "ssfx_ui_wdof_fadestart", &ps_ssfx_ui_wdof_fadestart, 0.0f, 0.3f);
        CMD4(CCC_Float, "ssfx_ui_wdof_fadelen", &ps_ssfx_ui_wdof_fadelen, 0.0f, 1.3f); // fadelen
        CMD4(CCC_Float, "ssfx_ui_wdof_blur", &ps_ssfx_ui_wdof_blur, 0.0f, 2.0f); // blur
        CMD4(CCC_Float, "ssfx_ui_wdof_edgeblur", &ps_ssfx_ui_wdof_edgeblur, 0.0f, 1.0f); // edgeblur
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_wdof_reloading", &ps_ssfx_ui_wdof_reloading); // reloading
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_wdof_pda", &ps_ssfx_ui_wdof_pda); // pda
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_wdof_inventory", &ps_ssfx_ui_wdof_inventory); // inventory
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_wdof_fdda", &ps_ssfx_ui_wdof_fdda); // fdda
        // looting_mutant
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_wdof_looting_mutant", &ps_ssfx_ui_wdof_looting_mutant);

        // --- il - ssfx_il_mcm.script
        // quality
        CMD3(CCC_ssfx_ui_list_i, "ssfx_ui_il_quality", &ps_ssfx_il_quality, tok_il_quality);
        // res: his 0.15 .. 1 through 1.0/X ( transform from ssfx_il.script:52 )
        CMD4(CCC_Float, "ssfx_ui_il_res", &ps_ssfx_il.x, 1.0f, 6.6667f);
        CMD4(CCC_Float, "ssfx_ui_il_dist", &ps_ssfx_il_setup1.x, 20.0f, 300.0f); // distance
        // blur: his 0 .. 1 through X * 5
        CMD4(CCC_Float, "ssfx_ui_il_blur", &ps_ssfx_il.w, 0.0f, 5.0f);
        CMD4(CCC_Float, "ssfx_ui_il_vibrance", &ps_ssfx_il.z, 0.0f, 2.0f); // vibrance
        CMD4(CCC_Float, "ssfx_ui_il_int", &ps_ssfx_il.y, 0.0f, 4.0f); // global_int
        CMD4(CCC_Float, "ssfx_ui_il_hud_int", &ps_ssfx_il_setup1.y, 0.0f, 1.0f); // hud_int
        CMD4(CCC_Float, "ssfx_ui_il_flora_int", &ps_ssfx_il_setup1.z, 0.0f, 1.0f); // flora_int

        // --- fog - ssfx_fog_mcm.script
        CMD4(CCC_Float, "ssfx_ui_fog_height", &ps_ssfx_fog.x, 1.0f, 20.0f); // height
        CMD4(CCC_Float, "ssfx_ui_fog_density", &ps_ssfx_fog.y, 0.0f, 5.0f); // density
        CMD4(CCC_Float, "ssfx_ui_fog_suncolor", &ps_ssfx_fog.z, 0.0f, 1.0f); // suncolor
        // scattering
        CMD4(CCC_Float, "ssfx_ui_fog_scattering", &ps_ssfx_fog_scattering, 0.0f, 1.0f);

        // --- florafixes - ssfx_florafixes_mcm.script
        CMD4(CCC_Float, "ssfx_ui_flora_sss_int", &ps_ssfx_florafixes_2.x, 0.0f, 10.0f); // sss_int
        // sss_color
        CMD4(CCC_Float, "ssfx_ui_flora_sss_color", &ps_ssfx_florafixes_2.y, 0.0f, 1.0f);
        // grass_specular
        CMD4(CCC_Float, "ssfx_ui_flora_grass_specular", &ps_ssfx_florafixes_1.x, 0.0f, 1.0f);
        // grass_specular_wet
        CMD4(CCC_Float, "ssfx_ui_flora_grass_specular_wet", &ps_ssfx_florafixes_1.y, 0.0f, 1.0f);
        // trees_specular
        CMD4(CCC_Float, "ssfx_ui_flora_trees_specular", &ps_ssfx_florafixes_1.z, 0.0f, 1.0f);
        // trees_specular_wet
        CMD4(CCC_Float, "ssfx_ui_flora_trees_specular_wet", &ps_ssfx_florafixes_1.w, 0.0f, 1.0f);

        // --- inter_grass - ssfx_interactive_grass_mcm.script
        CMD2(CCC_ssfx_ui_grass_bool, "ssfx_ui_grass_enable", &ps_ssfx_ui_grass_enable); // enable
        // max_entities
        CMD4(CCC_ssfx_ui_grass_float, "ssfx_ui_grass_max_entities", &ps_ssfx_ui_grass_max_entities,
             1.0f, 15.0f);
        // max_distance
        CMD4(CCC_Float, "ssfx_ui_grass_max_distance", &ps_ssfx_grass_interactive.z, 10.0f, 5000.0f);
        // enable_player
        CMD2(CCC_ssfx_ui_grass_bool, "ssfx_ui_grass_enable_player",
             &ps_ssfx_ui_grass_enable_player);
        // enable_mutants
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_grass_enable_mutants", &ps_ssfx_grass_interactive.w);
        // enable_anomalies
        CMD2(CCC_ssfx_ui_grass_bool, "ssfx_ui_grass_enable_anomalies",
             &ps_ssfx_ui_grass_enable_anomalies);
        // anomalies_distance
        CMD4(CCC_ssfx_ui_grass_float, "ssfx_ui_grass_anomalies_distance",
             &ps_ssfx_ui_grass_anomalies_distance, 10.0f, 60.0f);
        // radius
        CMD4(CCC_Float, "ssfx_ui_grass_radius", &ps_ssfx_int_grass_params_1.x, 0.5f, 2.5f);
        // horizontal_str
        CMD4(CCC_Float, "ssfx_ui_grass_horizontal_str", &ps_ssfx_int_grass_params_1.y, 0.5f, 2.5f);
        // vertical_str
        CMD4(CCC_Float, "ssfx_ui_grass_vertical_str", &ps_ssfx_int_grass_params_1.z, 0.5f, 2.5f);
        // explosions_str
        CMD4(CCC_Float, "ssfx_ui_grass_explosions_str", &ps_ssfx_int_grass_params_2.x, 0.0f, 2.0f);
        // explosions_speed
        CMD4(CCC_Float, "ssfx_ui_grass_explosions_speed", &ps_ssfx_int_grass_params_2.y, 1.0f,
             20.0f);
        // shooting_str
        CMD4(CCC_Float, "ssfx_ui_grass_shooting_str", &ps_ssfx_int_grass_params_2.z, 0.0f, 1.0f);
        // shooting_range
        CMD4(CCC_Float, "ssfx_ui_grass_shooting_range", &ps_ssfx_int_grass_params_2.w, 0.5f, 5.0f);

        // --- wind - ssfx_wind_mcm.script
        // min_speed. The floor under the weather's own wind ( screenspace_wind.h:44 ). Call of
        // Pripyat's weathers carry a wind_velocity of 0 .. 76 on a scale of 0 .. 1000, so on this
        // game the floor alone decides how hard the wind blows
        CMD4(CCC_Float, "ssfx_ui_wind_min_speed", &ps_ssfx_wind_trees.w, 0.0f, 1.0f);
        // grass_speed
        CMD4(CCC_Float, "ssfx_ui_wind_grass_speed", &ps_ssfx_wind_grass.x, 0.1f, 13.0f);
        // grass_turbulence
        CMD4(CCC_Float, "ssfx_ui_wind_grass_int", &ps_ssfx_wind_grass.y, 0.1f, 3.0f);
        CMD4(CCC_Float, "ssfx_ui_wind_grass_push", &ps_ssfx_wind_grass.z, 0.1f, 3.0f); // grass_push
        CMD4(CCC_Float, "ssfx_ui_wind_grass_wave", &ps_ssfx_wind_grass.w, 0.1f, 1.0f); // grass_wave
        // trees_speed
        CMD4(CCC_Float, "ssfx_ui_wind_tree_branch", &ps_ssfx_wind_trees.x, 0.1f, 13.0f);
        // trees_trunk
        CMD4(CCC_Float, "ssfx_ui_wind_tree_trunk", &ps_ssfx_wind_trees.y, 0.1f, 0.3f);
        CMD4(CCC_Float, "ssfx_ui_wind_tree_bend", &ps_ssfx_wind_trees.z, 0.1f, 2.0f); // trees_bend

        // --- terrain - ssfx_terrain_mcm.script
        // distance ( the donor's slider stops at 40; 100 here, user 2026-10-23 )
        CMD4(CCC_Float, "ssfx_ui_terrain_dist", &ps_ssfx_terrain_quality.x, 0.0f, 100.0f);
        // pom_quality
        CMD3(CCC_ssfx_ui_list, "ssfx_ui_terrain_pom_quality", &ps_ssfx_terrain_pom.x,
             tok_terrain_pom_quality);
        // pom_refine
        CMD4(CCC_Integer, "ssfx_ui_terra_pom_refine", &ps_ssfx_terrain_pom_refine, 0, 1);
        // pom_range
        CMD4(CCC_Float, "ssfx_ui_terrain_pom_range", &ps_ssfx_terrain_pom.y, 0.0f, 40.0f);
        // pom_height
        CMD4(CCC_Float, "ssfx_ui_terrain_pom_height", &ps_ssfx_terrain_pom.z, 0.01f, 0.1f);
        // pom_water_level
        CMD4(CCC_Float, "ssfx_ui_terrain_pom_water", &ps_ssfx_terrain_pom.w, 0.0f, 2.0f);
        // grass_align
        CMD4(CCC_Integer, "ssfx_ui_terra_grass_align", &ps_ssfx_terrain_grass_align, 0, 1);
        // grass_slope: his 0 .. 90 through X/90.0 ( transform from ssfx_terrain.script:53 )
        CMD4(CCC_Float, "ssfx_ui_terra_grass_slope", &ps_ssfx_terrain_grass_slope, 0.0f, 1.0f);

        // --- ssfx_bloom - ssfx_pp_mcm.script
        // use_weather
        CMD4(CCC_Integer, "ssfx_ui_bloom_use_weather", &ps_ssfx_bloom_use_presets, 0, 1);
        CMD4(CCC_Float, "ssfx_ui_bloom_threshold", &ps_ssfx_bloom_1.x, 1.0f, 10.0f); // threshold
        CMD4(CCC_Float, "ssfx_ui_bloom_exposure", &ps_ssfx_bloom_1.y, 1.0f, 100.0f); // exposure
        CMD4(CCC_Float, "ssfx_ui_bloom_blur", &ps_ssfx_bloom_2.x, 1.0f, 5.0f); // blur
        CMD4(CCC_Float, "ssfx_ui_bloom_vibrance", &ps_ssfx_bloom_2.y, 0.0f, 10.0f); // vibrance
        CMD4(CCC_Float, "ssfx_ui_bloom_sky", &ps_ssfx_bloom_1.w, 0.0f, 10.0f); // sky
        CMD4(CCC_Float, "ssfx_ui_bloom_lens", &ps_ssfx_bloom_2.z, 0.0f, 10.0f); // lens
        CMD4(CCC_Float, "ssfx_ui_bloom_dirt", &ps_ssfx_bloom_2.w, 0.0f, 10.0f); // dirt

        // --- ssfx_motionblur - ssfx_pp_mcm.script
        // quality
        CMD3(CCC_ssfx_ui_list, "ssfx_ui_mblur_quality", &ps_ssfx_motionblur.x, tok_mblur_quality);
        CMD4(CCC_Float, "ssfx_ui_mblur_int", &ps_ssfx_motionblur.y, 0.0f, 2.0f); // intensity
        CMD4(CCC_Float, "ssfx_ui_mblur_camera", &ps_ssfx_motionblur.w, 0.0f, 1.0f); // camera
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_mblur_hudonly", &ps_ssfx_motionblur.z); // hudonly

        // --- ssfx_taa - ssfx_pp_mcm.script
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_taa_enabled", &ps_ssfx_taa.x); // enabled
        CMD4(CCC_Float, "ssfx_ui_taa_sharp", &ps_ssfx_taa.z, 0.0f, 1.0f); // sharpness
        CMD4(CCC_Float, "ssfx_ui_taa_jitter", &ps_ssfx_taa.y, 0.0f, 1.0f); // jitter

        // --- shadows - ssfx_shadows_mcm.script
        // lod_min ( list from ssfx_shadows.script:43-55 )
        CMD3(CCC_ssfx_ui_list, "ssfx_ui_shw_lod_min", &ps_ssfx_shadows.x, tok_shw_lod_min);
        // lod_max ( list from ssfx_shadows.script:58-70 )
        CMD3(CCC_ssfx_ui_list, "ssfx_ui_shw_lod_max", &ps_ssfx_shadows.y, tok_shw_lod_max);
        // volumetric_force
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_shw_volumetric_force", &ps_ssfx_volumetric.x);
        // volumetric_int
        CMD4(CCC_Float, "ssfx_ui_volumetric_int", &ps_ssfx_volumetric.y, 0.0f, 3.0f);
        // volumetric_quality
        CMD3(CCC_ssfx_ui_list, "ssfx_ui_shw_volumetric_quality", &ps_ssfx_volumetric.z,
             tok_shw_volumetric_quality);

        // --- shw_cascades - ssfx_shadow_cascades_mcm.script
        // grass_shw_quality
        CMD3(CCC_ssfx_ui_list, "ssfx_ui_cas_grass_shw_quality", &ps_ssfx_grass_shadows.x,
             tok_cas_grass_shw_quality);
        // grass_shw_distance: his 0 .. 100 through X / 100 ( range limited:
        // ssfx_shadow_cascades.script:36-44 )
        CMD4(CCC_Float, "ssfx_ui_grass_shadows", &ps_ssfx_grass_shadows.y, 0.0f, 1.0f);
        // grass_shw_nondir_maxdistance
        CMD4(CCC_Float, "ssfx_ui_cas_grass_shw_nondir_maxdistance", &ps_ssfx_grass_shadows.z, 10.0f,
             50.0f);

        // --- sss - ssfx_sss_mcm.script
        // quality_dir
        CMD3(CCC_ssfx_ui_list, "ssfx_ui_sss_quality_dir", &ps_ssfx_sss_quality.x,
             tok_sss_quality_dir);
        // quality_point
        CMD3(CCC_ssfx_ui_list, "ssfx_ui_sss_quality_point", &ps_ssfx_sss_quality.y,
             tok_sss_quality_point);
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_sss_enable_dir", &ps_ssfx_sss_quality.z); // enable_dir
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_sss_enable_point", &ps_ssfx_sss_quality.w); // enable_point
        CMD4(CCC_Float, "ssfx_ui_sss_len_dir", &ps_ssfx_sss.x, 0.1f, 2.0f); // len_dir
        CMD4(CCC_Float, "ssfx_ui_sss_len_point", &ps_ssfx_sss.y, 0.1f, 2.0f); // len_point

        // --- ssfx_gloss - ssfx_wetness_mcm.script
        // min_gloss. ssfx_wetness.script reads this view back as the floor of the rain-driven gloss
        CMD4(CCC_Float, "ssfx_ui_gloss_min", &ps_ssfx_gloss_minmax.x, 0.01f, 1.0f);
        CMD4(CCC_Float, "ssfx_ui_gloss_max", &ps_ssfx_gloss_minmax.y, 0.01f, 1.0f); // max_gloss
        // auto_gloss
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_gloss_auto_gloss", &ps_ssfx_ui_gloss_auto_gloss);
        // auto_gloss_max
        CMD4(CCC_Float, "ssfx_ui_gloss_auto_gloss_max", &ps_ssfx_ui_gloss_auto_gloss_max, 0.01f,
             1.0f);
        CMD4(CCC_Float, "ssfx_ui_spec_int", &ps_ssfx_lightsetup_1.x, 0.0f, 1.0f); // specular_int
        // specular_color
        CMD4(CCC_Float, "ssfx_ui_spec_color", &ps_ssfx_lightsetup_1.y, 0.0f, 1.0f);

        // --- ssfx_wet_surf - ssfx_wetness_mcm.script
        // buildup_speed
        CMD4(CCC_Float, "ssfx_ui_wet_buildup", &ssfx_wetness_multiplier.x, 0.1f, 20.0f);
        CMD4(CCC_Float, "ssfx_ui_wet_dry", &ssfx_wetness_multiplier.y, 0.1f, 20.0f); // dry_speed
        // ripples_size: his 0.01 .. 1.9 through max(2.0 - X, 0.01)
        CMD4(CCC_Float, "ssfx_ui_wet_ripples_size", &ps_ssfx_wetsurfaces_1.x, 0.1f, 1.99f);
        // ripples_speed
        CMD4(CCC_Float, "ssfx_ui_wet_ripples_speed", &ps_ssfx_wetsurfaces_1.y, 0.01f, 2.0f);
        // ripples_min_speed
        CMD4(CCC_Float, "ssfx_ui_wet_ripples_min_speed", &ps_ssfx_wetsurfaces_1.z, 0.01f, 2.0f);
        // ripples_intensity
        CMD4(CCC_Float, "ssfx_ui_wet_ripples", &ps_ssfx_wetsurfaces_1.w, 0.01f, 2.0f);
        // waterfall_size: his 0.01 .. 2 through max(2.0 - X, 0.01)
        CMD4(CCC_Float, "ssfx_ui_wet_waterfall_size", &ps_ssfx_wetsurfaces_2.x, 0.01f, 1.99f);
        // waterfall_speed
        CMD4(CCC_Float, "ssfx_ui_wet_waterfall_speed", &ps_ssfx_wetsurfaces_2.y, 0.01f, 2.0f);
        // waterfall_min_speed
        CMD4(CCC_Float, "ssfx_ui_wet_waterfall_min_speed", &ps_ssfx_wetsurfaces_2.z, 0.01f, 2.0f);
        // waterfall_intensity
        CMD4(CCC_Float, "ssfx_ui_wet_waterfall", &ps_ssfx_wetsurfaces_2.w, 0.01f, 1.0f);

        // --- pseudo-PBR - no MCM module in the donor: Enhanced Shaders keeps all of these as fixed
        // numbers in its shaders. One view per number of the seven ssfx_es_* vectors, each over
        // the range its vector is registered with above, so no slider position is refused. The
        // two compiled-in switches ssfx_es_burley / ssfx_es_ggx are check boxes on their own
        // commands and need no view.
        CMD4(CCC_Float, "ssfx_ui_es_albedo_boost", &ps_ssfx_es_albedo.x, 0.0f, 1.0f);
        CMD4(CCC_Float, "ssfx_ui_es_albedo_amount", &ps_ssfx_es_albedo.y, 0.5f, 1.5f);
        CMD4(CCC_Float, "ssfx_ui_es_rough_low", &ps_ssfx_es_rough.x, 0.15f, 1.0f);
        CMD4(CCC_Float, "ssfx_ui_es_rough_high", &ps_ssfx_es_rough.y, 0.15f, 1.0f);
        CMD4(CCC_Float, "ssfx_ui_es_rough_pow", &ps_ssfx_es_rough.z, 0.75f, 4.0f);
        CMD4(CCC_Float, "ssfx_ui_es_spec_base", &ps_ssfx_es_spec.x, 0.0f, 0.1f);
        CMD4(CCC_Float, "ssfx_ui_es_spec_range", &ps_ssfx_es_spec.y, 0.0f, 2.0f);
        CMD4(CCC_Float, "ssfx_ui_es_spec_pow", &ps_ssfx_es_spec.z, 0.0f, 4.0f);
        CMD4(CCC_Float, "ssfx_ui_es_metal_boost", &ps_ssfx_es_metal.x, 0.0f, 0.5f);
        CMD4(CCC_Float, "ssfx_ui_es_metal_threshold", &ps_ssfx_es_metal.y, 0.01f, 1.0f);
        CMD4(CCC_Float, "ssfx_ui_es_shadow_soft", &ps_ssfx_es_light.x, 1.0f, 3.0f);
        CMD4(CCC_Float, "ssfx_ui_es_horizon", &ps_ssfx_es_light.y, 0.5f, 4.0f);
        CMD4(CCC_Float, "ssfx_ui_es_emissive", &ps_ssfx_es_light.z, 1.0f, 32.0f);
        CMD4(CCC_Float, "ssfx_ui_es_flora_spec", &ps_ssfx_es_flora.x, 0.0f, 0.2f);
        CMD4(CCC_Float, "ssfx_ui_es_flora_sky", &ps_ssfx_es_flora.y, 0.0f, 1.0f);
        CMD4(CCC_Float, "ssfx_ui_es_flora_sss", &ps_ssfx_es_flora.z, 0.0f, 2.0f);
        CMD4(CCC_Float, "ssfx_ui_es_water_spec", &ps_ssfx_es_water.x, 0.0f, 0.1f);
        CMD4(CCC_Float, "ssfx_ui_es_water_rough", &ps_ssfx_es_water.y, 0.05f, 0.5f);

        // --- Wind Leaves - its own window in the Weather_ATMOS addon; Wind_Leaves.script reads
        // these back ( get_console():get_bool, get_token ). debug_log switches the mod's printf
        // lines only.
        CMD3(CCC_ssfx_ui_list, "ssfx_ui_wl_haze_mode", &ps_ssfx_ui_wl_haze_mode, tok_wl_haze_mode);
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_wl_night_fog", &ps_ssfx_ui_wl_night_fog);
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_wl_calm_leaves", &ps_ssfx_ui_wl_calm_leaves);
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_wl_evening_leaves", &ps_ssfx_ui_wl_evening_leaves);
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_wl_storm_leaves", &ps_ssfx_ui_wl_storm_leaves);
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_wl_storm_dust", &ps_ssfx_ui_wl_storm_dust);
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_wl_tornado", &ps_ssfx_ui_wl_tornado);
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_wl_fireflies", &ps_ssfx_ui_wl_fireflies);
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_wl_insects", &ps_ssfx_ui_wl_insects);
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_wl_underground_fog", &ps_ssfx_ui_wl_underground_fog);
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_wl_indoor_fog", &ps_ssfx_ui_wl_indoor_fog);
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_wl_indoor_disable_all", &ps_ssfx_ui_wl_indoor_disable_all);
        CMD2(CCC_ssfx_ui_bool, "ssfx_ui_wl_debug_log", &ps_ssfx_ui_wl_debug_log);

        // The interactive-grass vectors start from the stored switches and values, not from the
        // zeroes the vectors are declared with.
        ssfx_ui_apply_inter_grass();
    }

    // Infernis PBR - the settings page ( infernis_pbr_console_vars.h ). The first two are compiled
    // into the shaders, so a change shows after the game is started again; the rest are sent to
    // the shaders every frame. Every range holds the value infernis_pbr_lighting.h ships with.
    CMD4(CCC_Integer, "pbr_enable", &ps_pbr_enable, 0, 1);
    CMD3(CCC_Token, "pbr_sslr_mode", &ps_pbr_sslr_mode, tok_pbr_sslr_mode);
    CMD4(CCC_Float, "pbr_sun_diffuse", &ps_pbr_cal_diffuse.x, 0.0f, 2.0f);
    CMD4(CCC_Float, "pbr_local_diffuse", &ps_pbr_cal_diffuse.y, 0.0f, 2.0f);
    CMD4(CCC_Float, "pbr_ambient_diffuse", &ps_pbr_cal_diffuse.z, 0.0f, 2.0f);
    // the strengths the author of the header tried for this one run from 0.72 to 3.5
    CMD4(CCC_Float, "pbr_metal_energy", &ps_pbr_cal_diffuse.w, 0.0f, 3.5f);
    CMD4(CCC_Float, "pbr_sun_specular", &ps_pbr_cal_specular.x, 0.0f, 2.0f);
    CMD4(CCC_Float, "pbr_local_specular", &ps_pbr_cal_specular.y, 0.0f, 2.0f);
    CMD4(CCC_Float, "pbr_ambient_specular", &ps_pbr_cal_specular.z, 0.0f, 2.0f);
    CMD4(CCC_Float, "pbr_metal_roughness_gain", &ps_pbr_cal_specular.w, 0.5f, 4.0f);
    CMD4(CCC_Float, "pbr_sun_peak", &ps_pbr_highlight.x, 0.0f, 4.0f);
    CMD4(CCC_Float, "pbr_local_peak", &ps_pbr_highlight.y, 0.0f, 8.0f);
    CMD4(CCC_Float, "pbr_aa_strength", &ps_pbr_highlight.z, 0.0f, 1.0f);
    CMD4(CCC_Float, "pbr_aa_variance", &ps_pbr_highlight.w, 0.0f, 0.5f);
    // 0.10 at most: the water glint of lmodel.h asks for 0.1 and has to pass this clamp untouched
    CMD4(CCC_Float, "pbr_min_roughness", &ps_pbr_surface.x, 0.04f, 0.10f);

    // Registered here rather than in the renderer because CEnvDescriptorMixer::lerp reads them.
    // Ported from Monolith xr_ioc_cmd.cpp:1087-1088, same ranges.
    CMD4(CCC_Float, "r2_sunshafts_min", &ps_r2_sun_shafts_min, 0.0f, 0.5f);
    CMD4(CCC_Float, "r2_sunshafts_value", &ps_r2_sun_shafts_value, 0.5f, 2.0f);

#ifdef DEBUG
    extern BOOL debug_destroy;
    CMD4(CCC_Integer, "debug_destroy", &debug_destroy, FALSE, TRUE);
#endif
};
