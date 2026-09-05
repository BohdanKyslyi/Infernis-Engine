#ifndef HangingLampH
#define HangingLampH
#pragma once

#include "gameobject.h"
#include "physicsshellholder.h"
#include "PHSkeleton.h"
#include "script_export_space.h"

// Попередні оголошення (Forward declarations)
class CLAItem;
class CPhysicsElement;
class CSE_ALifeObjectHangingLamp;

class CHangingLamp : public CPhysicsShellHolder, public CPHSkeleton {
    using inherited = CPhysicsShellHolder;

private:
    // Параметри освітлення
    u16 light_bone{ BI_NONE };
    u16 ambient_bone{ BI_NONE };

    ref_light light_render{};
    ref_light light_ambient{};
    ref_glow glow_render{};
    
    CLAItem* lanim{ nullptr };
    float ambient_power{ 0.f };
    float fBrightness{ 1.f };
    
    // Стан об'єкта
    float fHealth{ 100.f };
    BOOL m_bState{ TRUE };

    void CreateBody(CSE_ALifeObjectHangingLamp* lamp);
    
    [[nodiscard]] inline bool Alive() const { return fHealth > 0.f; }

public:
    CHangingLamp();
    ~CHangingLamp() override;

    // Керування станом
    void TurnOn();
    void TurnOff();
    void RespawnInit();

    // Життєвий цикл об'єкта
    void Load(LPCSTR section) override;
    BOOL net_Spawn(CSE_Abstract* DC) override;
    void net_Destroy() override;
    void shedule_Update(u32 dt) override;
    void UpdateCL() override;

    // Фізика
    void SpawnInitPhysics(CSE_Abstract* D) override;
    void CopySpawnInit() override;
    CPhysicsShellHolder* PPhysicsShellHolder() override { return PhysicsShellHolder(); }

    // Збереження та завантаження
    void net_Save(NET_Packet& P) override;
    BOOL net_SaveRelevant() override;
    void save(NET_Packet& output_packet) override;
    void load(IReader& input_packet) override;

    // Рендеринг
    BOOL renderable_ShadowGenerate() override { return TRUE; }
    BOOL renderable_ShadowReceive() override { return TRUE; }

    // Взаємодія
    void Hit(SHit* pHDS) override;
    void net_Export(NET_Packet& P) override;
    void net_Import(NET_Packet& P) override;
    BOOL UsedAI_Locations() override;

    // Просторові характеристики
    void Center(Fvector& C) const override;
    [[nodiscard]] float Radius() const override;

    DECLARE_SCRIPT_REGISTER_FUNCTION
};

add_to_type_list(CHangingLamp)
#undef script_type_list
#define script_type_list save_type_list(CHangingLamp)

#endif // HangingLampH