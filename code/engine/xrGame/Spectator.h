#pragma once

#include "../xrEngine/feel_touch.h"
#include "../xrEngine/iinputreceiver.h"

#include "entity.h"
#include "actor_flags.h"

class CActor;

class CSpectator : public CGameObject, public IInputReceiver {
private:
    using inherited = CGameObject;
    
    CTimer m_timer; 
    float m_fTimeDelta = EPS_S; 

public:
    enum EActorCameras {
        eacFreeFly = 0,
        eacFirstEye,
        eacLookAt,
        eacFreeLook,
        eacFixedLookAt,
        eacMaxCam
    };

private:
    CCameraBase* cameras[eacMaxCam] = {nullptr};
    EActorCameras cam_active = eacFreeLook;
    EActorCameras m_last_camera = eacFreeLook;

    int look_idx = 0;
    CActor* m_pActorToLookAt = nullptr;
    shared_str m_last_player_name;

    static constexpr float cam_inert_value = 0.7f; 
    float prev_cam_inert_value = 0.0f;

    void cam_Set(EActorCameras style);
    void cam_Update(CActor* A = nullptr);
    bool SelectNextPlayerToLook(bool const search_next);
    void FirstEye_ToPlayer(CObject* pObject);

public:
    CSpectator();
    virtual ~CSpectator() override;

    // IInputReceiver Overrides
    virtual void IR_OnMouseMove(int x, int y) override;
    virtual void IR_OnKeyboardPress(int dik) override;
    virtual void IR_OnKeyboardRelease(int dik) override;
    virtual void IR_OnKeyboardHold(int dik) override;

    // CGameObject / CObject Overrides
    virtual void shedule_Update(u32 T) override;
    virtual void UpdateCL() override;
    virtual BOOL net_Spawn(CSE_Abstract* DC) override;
    virtual void net_Destroy() override;
    virtual void net_Relcase(CObject* O) override;

    virtual void Center(Fvector& C) const override { C.set(Position()); }
    [[nodiscard]] virtual float Radius() const override { return EPS; }
    
    [[nodiscard]] virtual CGameObject* cast_game_object() override { return this; }
    [[nodiscard]] virtual IInputReceiver* cast_input_receiver() override { return this; }

    void GetSpectatorString(string1024& pStr) const;

    virtual void On_SetEntity() override;
    virtual void On_LostEntity() override;

    [[nodiscard]] inline EActorCameras GetActiveCam() const { return cam_active; }
};