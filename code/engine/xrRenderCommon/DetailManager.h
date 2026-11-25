// DetailManager.h: interface for the CDetailManager class.
#ifndef DetailManagerH
#define DetailManagerH
#pragma once

#include "xrCore/xrpool.h"
#include "detailformat.h"
#include "detailmodel.h"
#include <atomic>
#include <mutex>

#ifdef _EDITOR
constexpr int dm_max_decompress = 14;
class CCustomObject;
typedef u32 ObjClassID;

typedef xr_list<CCustomObject*> ObjectList;
typedef ObjectList::iterator ObjectIt;
typedef xr_map<ObjClassID, ObjectList> ObjectMap;
typedef ObjectMap::iterator ObjectPairIt;
#else
constexpr int dm_max_decompress = 7;
#endif

// Screen Space Shaders: grass draw radius.
// Half-width of the grass cache in 2 m slots, so the radius in metres is dm_size * 2 and the
// distance at which a blade has shrunk to nothing is dm_fade below. Call of Pripyat shipped 24
// ( 48 m ), which is why grass appeared to end a short way in front of the actor. Monolith makes
// this a runtime variable driven by r__detail_radius ( xrRender_console.cpp:562-569, guarded by
// DETAIL_RADIUS ) and computes it as floor( R / 4 ) * 2; 76 is what that formula returns for
// r__detail_radius 152, so the value here is the donor's for a 152 m radius.
// It has to stay a constant in this engine: dm_cache_line feeds a fixed array in
// DetailManager_CACHE.cpp:145 and dm_cache_size sizes the inline svector in DetailManager.h,
// neither of which may become runtime sized. It also has to stay even, or dm_cache1_line below
// stops dividing exactly.
#if RENDER == R_R4
constexpr int dm_size = 76;
#else
// The other renderers keep the game's own cache, Infernis DetailManager.h:25 - 24 slots, 48 m.
constexpr int dm_size = 24;
#endif
constexpr int dm_cache1_count = 4;                            
constexpr int dm_cache1_line = dm_size * 2 / dm_cache1_count; 
constexpr int dm_max_objects = 64;
constexpr int dm_obj_in_slot = 4;
constexpr int dm_cache_line = dm_size + 1 + dm_size;
constexpr int dm_cache_size = dm_cache_line * dm_cache_line;
constexpr float dm_fade = static_cast<float>(2 * dm_size) - 0.5f;
constexpr float dm_slot_size = DETAIL_SLOT_SIZE;

class ECORE_API CDetailManager {
public:
    // Screen Space Shaders. Monolith DetailManager.h:68-71.
    // fade_distance is how far a blade may stand from the camera before it starts shrinking;
    // -1 switches the measurement over to light_position, which is how grass fades inside the
    // shadow map of a lamp. details_clear() empties the visibility lists once per frame.
    float fade_distance = 99999;
    Fvector light_position;

    void details_clear();

    struct SlotItem {
        float scale;
        float scale_calculated;
        Fmatrix mRotY;
        u32 vis_ID;
        float c_hemi;
        float c_sun;
        // Screen Space Shaders: the per-blade data the grass shader reads out of "exdata".
        // normal is the terrain normal under the blade ( grass lighting, and the alignment when
        // ssfx_terrain_grass_align is on ), alpha is the blade's own opacity. distance and
        // position are what the fade above is measured against.
        // Ported from Monolith DetailManager.h:82-86.
        float distance;
        Fvector position;
        Fvector normal;
        float alpha;
        float alpha_target;
#if RENDER == R_R1
        Fvector c_rgb;
#endif
    };
    using SlotItemVec = xr_vector<SlotItem*>;
    
    struct SlotPart {           
        u32 id;                 
        SlotItemVec items;      
        SlotItemVec r_items[3]; 
    };
    
    enum SlotType {
        stReady = 0, 
        stPending,   

        stFORCEDWORD = 0xffffffff
    };
    
    struct Slot { 
        struct {
            u32 empty : 1;
            u32 type : 1;
            u32 frame : 30;
        };
        int sx, sz;
        vis_data vis;
        SlotPart G[dm_obj_in_slot];
        // Screen Space Shaders. Monolith DetailManager.h:125. Raised when the slot drops out of
        // fade range, so every blade in it starts from zero opacity when it comes back.
        bool hidden;

        Slot() {
            frame = 0;
            empty = 1;
            type = stReady;
            sx = sz = 0;
            vis.clear();
        }
    };
    
    struct CacheSlot1 {
        u32 empty;
        vis_data vis;
        Slot** slots[dm_cache1_count * dm_cache1_count];
        CacheSlot1() {
            empty = 1;
            vis.clear();
        }
    };

    typedef xr_vector<xr_vector<SlotItemVec*>> vis_list;
    typedef svector<CDetail*, dm_max_objects> DetailVec;
    typedef DetailVec::iterator DetailIt;
    typedef poolSS<SlotItem, 4096> PSS;

public:
    int dither[16][16];

public:
    struct SSwingValue {
        float rot1;
        float rot2;
        float amp1;
        float amp2;
        float speed;
        void lerp(const SSwingValue& v1, const SSwingValue& v2, float factor);
    };
    SSwingValue swing_desc[2];
    SSwingValue swing_current;
    float m_time_rot_1;
    float m_time_rot_2;
    float m_time_pos;
    float m_global_time_old;

public:
    IReader* dtFS;
    DetailHeader dtH;
    DetailSlot* dtSlots; 
    DetailSlot DS_empty;

public:
    DetailVec objects;
    vis_list m_visibles[3]; 

#ifndef _EDITOR
    xrXRC xrc;
#endif
    CacheSlot1 cache_level1[dm_cache1_line][dm_cache1_line];
    Slot* cache[dm_cache_line][dm_cache_line]; 
    svector<Slot*, dm_cache_size> cache_task;  
    Slot cache_pool[dm_cache_size];            
    int cache_cx;
    int cache_cz;

    PSS poolSI; 

    void UpdateVisibleM();
    void UpdateVisibleS();

public:
#ifdef _EDITOR
    virtual ObjectList* GetSnapList() = 0;
#endif

    [[nodiscard]] IC bool UseVS() const { return HW.Caps.geometry_major >= 1; }

    ref_geom soft_Geom;
    void soft_Load();
    void soft_Unload();
    void soft_Render();

    ref_geom hw_Geom;
    u32 hw_BatchSize;
    ID3DVertexBuffer* hw_VB;
    ID3DIndexBuffer* hw_IB;
    ref_constant hwc_consts;
    ref_constant hwc_wave;
    ref_constant hwc_wind;
    ref_constant hwc_array;
    ref_constant hwc_s_consts;
    ref_constant hwc_s_xform;
    ref_constant hwc_s_array;
    
    void hw_Load();
    void hw_Load_Geom();
    void hw_Load_Shaders();
    void hw_Unload();
    void hw_Render();
#if defined(USE_DX10) || defined(USE_DX11)
    void hw_Render_dump(const Fvector4& consts, const Fvector4& wave, const Fvector4& wind,
                        const Fvector4& prev_wave, const Fvector4& prev_wind, u32 var_id, u32 lod_id);
#else 
    void hw_Render_dump(ref_constant array, u32 var_id, u32 lod_id, u32 c_base);
#endif 

public:
    DetailSlot& QueryDB(int sx, int sz);

    void cache_Initialize();
    void cache_Update(int sx, int sz, Fvector& view, int limit);
    void cache_Task(int gx, int gz, Slot* D);
    Slot* cache_Query(int sx, int sz);
    void cache_Decompress(Slot* D);
    BOOL cache_Validate();
    
    int cg2w_X(int x) { return cache_cx - dm_size + x; }
    int cg2w_Z(int z) { return cache_cz - dm_size + (dm_cache_line - 1 - z); }
    int w2cg_X(int x) { return x - cache_cx + dm_size; }
    int w2cg_Z(int z) { return cache_cz - dm_size + (dm_cache_line - 1 - z); }

    void Load();
    void Unload();
    void Render();

    // MT stuff
    std::recursive_mutex MT;
    std::atomic<u32> m_frame_calc{0};
    std::atomic<u32> m_frame_rendered{0};
#if RENDER == R_R4
    // The grass density ( r__detail_density ) the cache was laid out with: with SSS, MT_CALC lays
    // the cached grass out again once the slider gives another value ( user 2026-11-04 ).
    float m_density_laid{0.f};

    // Written by Ivan Danko, 2026-11-18: FPS plan step 3 "grass on all cores" ( "mt_grass", on by
    // default, xrEngine\xr_ioc_cmd.cpp ). Before, the drawing thread worked every blade out by itself
    // in hw_Render_dump ( dx10DetailManager_VS.cpp ): its opacity, its fade, its matrix, its colour and
    // its normal, one blade after another, and drew them a batch ( hw_BatchSize ) at a time. Now
    // hw_Render first works out the blades of all three variants ( the still grass and the two waves )
    // at once on all cores into the pieces below ( mt_prepare ), and the drawing thread only copies the
    // finished values into the constant buffer and draws: the same blades, the same values, the same
    // draw calls. A piece = up to 32 lists of one grass type of one variant, in the order the draw takes
    // them. m_mt_lists_stale: the visibility lists still held the last frame's entries when MT_CALC
    // filled them ( a list could then stand in them twice ) - such a frame is drawn the old way.
    // Створено (Ivan Danko, 2026-11-18): крок 3 плану FPS "трава на всіх ядрах" ( "mt_grass",
    // увімкнено за замовчуванням, xrEngine\xr_ioc_cmd.cpp ). Раніше потік малювання сам рахував кожну
    // травинку в hw_Render_dump ( dx10DetailManager_VS.cpp ): її непрозорість, згасання, матрицю, колір
    // і нормаль, одну за одною, і малював їх пачками ( hw_BatchSize ). Тепер hw_Render спершу рахує
    // травинки всіх трьох варіантів ( нерухома трава й дві хвилі ) разом на всіх ядрах у шматки нижче
    // ( mt_prepare ), а потік малювання лише копіює готові значення в буфер констант і малює: ті самі
    // травинки, ті самі значення, ті самі виклики малювання. Шматок = до 32 списків одного типу трави
    // одного варіанта в тому порядку, в якому їх бере малювання. m_mt_lists_stale: списки видимості ще
    // тримали записи минулого кадру, коли MT_CALC їх заповнював ( список міг би тоді стояти в них
    // двічі ), - такий кадр малюється по-старому.
    struct mt_piece {
        u32 var_id = 0;          // the variant ( m_visibles )
        u32 object = 0;          // the grass type
        u32 list_begin = 0;      // its lists [ list_begin, list_end ) in m_visibles[ var_id ][ object ]
        u32 list_end = 0;
        u32 blades = 0;          // the blades worked out
        xr_vector<Fvector4> arr; // 4 a blade: "array" ( the matrix rows times the scale, the colour )
        xr_vector<Fvector4> ex;  // 1 a blade: "exdata" ( the terrain normal, the opacity )
    };
    xr_vector<mt_piece> m_mt_pieces;        // grows only; this call's pieces come first
    u32 m_mt_first[3][dm_max_objects] = {}; // the first piece of a variant's grass type
    u32 m_mt_count[3][dm_max_objects] = {}; // its pieces
    bool m_mt_ready = false;                // this hw_Render call worked the blades out ( first pass )
    bool m_mt_lists_stale = false;          // see above ( MT_CALC, DetailManager.cpp )
    u32 m_mt_blades = 0;                    // "cpu_frame_stats 1": the blades this call drew
    u32 m_mt_draws = 0;                     // "cpu_frame_stats 1": its draw calls
    void mt_prepare();
    void mt_prepare_piece(mt_piece& piece);
#endif

    void __stdcall MT_CALC();
    ICF void MT_SYNC() {
        if (m_frame_calc.load(std::memory_order_acquire) == RDEVICE.dwFrame)
            return;
        MT_CALC();
    }

    CDetailManager();
    virtual ~CDetailManager();
};

#endif // DetailManagerH