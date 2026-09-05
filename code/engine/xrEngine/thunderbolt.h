#ifndef ThunderboltH
#define ThunderboltH
#pragma once

#include "xrRender/FactoryPtr.h"
#include "xrRender/LensFlareRender.h"
#include "xrRender/ThunderboltDescRender.h"
#include "xrRender/ThunderboltRender.h"

#ifndef _EDITOR
#include "render.h"
#endif

#ifdef INGAME_EDITOR
#define INGAME_EDITOR_VIRTUAL virtual
#else 
#define INGAME_EDITOR_VIRTUAL
#endif 

class CEnvironment;
class ENGINE_API IRender_DetailModel;
class ENGINE_API CLAItem;

struct SThunderboltDesc {
    FactoryPtr<IThunderboltDescRender> m_pRender;
    ref_sound snd;
    
    struct SFlare {
        float fOpacity{0.f};
        Fvector2 fRadius{0.f, 0.f};
        std::string texture;
        shared_str shader;
        FactoryPtr<IFlareRender> m_pFlare;
        
        SFlare() = default;
    };
    
    SFlare* m_GradientTop{nullptr};
    SFlare* m_GradientCenter{nullptr};
    shared_str name;
    CLAItem* color_anim{nullptr};

public:
    SThunderboltDesc() = default;
    INGAME_EDITOR_VIRTUAL ~SThunderboltDesc();
    
    void load(CInifile& pIni, shared_str const& sect);
    INGAME_EDITOR_VIRTUAL void create_top_gradient(CInifile& pIni, shared_str const& sect);
    INGAME_EDITOR_VIRTUAL void create_center_gradient(CInifile& pIni, shared_str const& sect);
};

#undef INGAME_EDITOR_VIRTUAL

struct SThunderboltCollection {
    using DescVec = xr_vector<SThunderboltDesc*>;
    DescVec palette;
    std::string section;

public:
    SThunderboltCollection() = default;
    ~SThunderboltCollection();
    
    void load(CInifile* pIni, CInifile* thunderbolts, LPCSTR sect);
    
    [[nodiscard]] SThunderboltDesc* GetRandomDesc() const {
        VERIFY(!palette.empty());
        return palette[Random.randI(palette.size())];
    }
};

class ENGINE_API CEffect_Thunderbolt {
    friend class dxThunderboltRender;

protected:
    using CollectionVec = xr_vector<SThunderboltCollection*>;
    CollectionVec collection;

    SThunderboltDesc* current{nullptr};
    Fmatrix current_xform;
    Fvector3 current_direction;
    Fvector lightning_center{0.f, 0.f, 0.f}; 
    float lightning_size{0.f};
    float lightning_phase{0.f};

private:
    static const int MAX_STRIKES = 8; 

    struct SStrike {
        bool active{false};
        SThunderboltDesc* desc{nullptr};
        Fmatrix xform;
        Fvector direction;
        Fvector center;
        float size{0.f};
        float phase{0.f};
        float life_time{0.f};
        float current_time{0.f};
#ifndef _EDITOR
        ref_light light;
#endif
        bool using_light{false};
    };

    SStrike m_Strikes[MAX_STRIKES]; 

    FactoryPtr<IThunderboltRender> m_pRender;
    
    enum EState { stIdle, stWorking };
    EState state{stIdle};

    float next_lightning_time{0.f};
    BOOL bEnabled{FALSE};

    // --- Налаштування з конфігу ---
    bool m_bEnableCustomLightning{false};
    bool m_bEnableMultiStrikes{false};
    int m_iMultiStrikeChance{25};
    int m_iMaxSimultaneous{3};
    int m_iSameLocationChance{40};
    int m_iBranchingChance{50};

    float m_fHeightFactor{0.8f};
    float m_fSizeMultiplier{2.5f};
    int m_iProbNear{20};
    int m_iProbMedium{40};
    float m_fDistNearMin{0.f}, m_fDistNearMax{5.f};
    float m_fDistMediumMin{10.f}, m_fDistMediumMax{40.f};
    float m_fDistFarMin{40.f}, m_fDistFarMax{150.f};
    
    shared_str m_sStrikeParticle; 
    ref_sound m_StrikeSound;

    float m_fThunderRangeMin{120.f};
    float m_fThunderRangeMax{1000.f};
    float m_fThunderFadeDist{300.f};
    float m_fThunderPitchDrop{0.35f};
    float m_fThunderVolDrop{0.25f};

    float m_fStrikeSndRangeMin{8.f};
    float m_fStrikeSndRangeMax{35.f};

    float m_fStrikeDamagePower{2.5f};  
    float m_fStrikeDamageRadius{20.0f};

private:
    static BOOL RayPick(const Fvector& s, const Fvector& d, float& range);
    void Bolt(const std::string& id, const float period, const float life_time);

public:
    CEffect_Thunderbolt();
    ~CEffect_Thunderbolt();

    void OnFrame(const std::string& id, const float period, const float duration);
    void Render();

    std::string AppendDef(CEnvironment& environment, CInifile* pIni, CInifile* thunderbolts, LPCSTR sect);
    void ForceStrike(LPCSTR id, const Fvector& target_pos);
};

#endif // ThunderboltH