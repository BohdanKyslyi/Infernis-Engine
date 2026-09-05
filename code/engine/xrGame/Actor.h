#pragma once

#include "../xrEngine/feel_touch.h"
#include "../xrEngine/feel_sound.h"
#include "../xrEngine/iinputreceiver.h"
#include "xrRender/KinematicsAnimated.h"
#include "actor_flags.h"
#include "actor_defs.h"
#include "fire_disp_controller.h"
#include "entity_alive.h"
#include "PHMovementControl.h"
#include "../xrphysics/PhysicsShell.h"
#include "InventoryOwner.h"
#include "../xrEngine/StatGraph.h"
#include "PhraseDialogManager.h"
#include "ui_defs.h"

#include "step_manager.h"
#include "script_export_space.h"

using namespace ACTOR_DEFS;

class CInfoPortion;
struct GAME_NEWS_DATA;
class CActorCondition;
class CCustomOutfit;
class CGameTaskRegistryWrapper;
class CGameNewsRegistryWrapper;
class CCharacterPhysicsSupport;
class CActorCameraManager;
// refs
class ENGINE_API CCameraBase;
class ENGINE_API CBoneInstance;
class ENGINE_API CBlend;
class CWeaponList;
class CEffectorBobbing;
class CHolderCustom;
class CUsableScriptObject;

struct SShootingEffector;
struct SSleepEffector;
class CSleepEffectorPP;
class CInventoryBox;

class CHudItem;
class CArtefact;

struct SActorMotions;
struct SActorVehicleAnims;
class CActorCondition;
class SndShockEffector;
class CActorFollowerMngr;

struct CameraRecoil;
class CCameraShotEffector;
class CActorInputHandler;

class CActorMemory;
class CActorStatisticMgr;

class CLocationManager;
class CItemUseController;

class CActor : public CEntityAlive,
               public IInputReceiver,
               public Feel::Touch,
               public CInventoryOwner,
               public CPhraseDialogManager,
               public CStepManager,
               public Feel::Sound
#ifdef DEBUG
               , public pureRender
#endif
{
    friend class CActorCondition;

private:
    using inherited = CEntityAlive;

public:
    CActor();
    virtual ~CActor() override;

    [[nodiscard]] CItemUseController* GetItemUseController() const { return m_item_use; }

public:
    [[nodiscard]] virtual BOOL AlwaysTheCrow() override { return TRUE; }

    [[nodiscard]] virtual CAttachmentOwner* cast_attachment_owner() override { return this; }
    [[nodiscard]] virtual CInventoryOwner* cast_inventory_owner() override { return this; }
    [[nodiscard]] virtual CActor* cast_actor() override { return this; }
    [[nodiscard]] virtual CGameObject* cast_game_object() override { return this; }
    [[nodiscard]] virtual IInputReceiver* cast_input_receiver() override { return this; }
    
    [[nodiscard]] virtual CCharacterPhysicsSupport* character_physics_support() override { return m_pPhysics_support; }
    [[nodiscard]] virtual const CCharacterPhysicsSupport* character_physics_support() const override { return m_pPhysics_support; }
    [[nodiscard]] virtual CPHDestroyable* ph_destroyable() override;
    
    [[nodiscard]] CHolderCustom* Holder() const { return m_holder; }

public:
    virtual void Load(LPCSTR section) override;
    virtual void shedule_Update(u32 T) override;
    virtual void UpdateCL() override;
    virtual void OnEvent(NET_Packet& P, u16 type) override;

    // Render
    virtual void renderable_Render() override;
    [[nodiscard]] virtual BOOL renderable_ShadowGenerate() override;
    virtual void feel_sound_new(CObject* who, int type, CSound_UserDataPtr user_data, const Fvector& Position, float power) override;
    [[nodiscard]] virtual Feel::Sound* dcast_FeelSound() override { return this; }
    
    float m_snd_noise = 0.0f;
#ifdef DEBUG
    virtual void OnRender() override;
#endif

public:
    [[nodiscard]] virtual bool OnReceiveInfo(shared_str info_id) const override;
    virtual void OnDisableInfo(shared_str info_id) const override;

    virtual void NewPdaContact(CInventoryOwner*) override;
    virtual void LostPdaContact(CInventoryOwner*) override;

#ifdef DEBUG
    void DumpTasks();
#endif

    struct SDefNewsMsg {
        GAME_NEWS_DATA* news_data;
        u32 time;
        bool operator<(const SDefNewsMsg& other) const { return time > other.time; }
    };
    xr_vector<SDefNewsMsg> m_defferedMessages;
    void UpdateDefferedMessages();

public:
    void AddGameNews_deffered(GAME_NEWS_DATA& news_data, u32 delay);
    virtual void AddGameNews(GAME_NEWS_DATA& news_data, bool bShowInHud = true);

protected:
    CActorStatisticMgr* m_statistic_manager = nullptr;

public:
    virtual void StartTalk(CInventoryOwner* talk_partner);
    void RunTalkDialog(CInventoryOwner* talk_partner, bool disable_break);
    CActorStatisticMgr& StatisticMgr() { return *m_statistic_manager; }
    
    CGameNewsRegistryWrapper* game_news_registry = nullptr;
    CCharacterPhysicsSupport* m_pPhysics_support = nullptr;

    [[nodiscard]] virtual LPCSTR Name() const override { return CInventoryOwner::Name(); }

public:
    // PhraseDialogManager
    virtual void ReceivePhrase(DIALOG_SHARED_PTR& phrase_dialog) override;
    virtual void UpdateAvailableDialogs(CPhraseDialogManager* partner) override;
    virtual void TryToTalk();
    bool OnDialogSoundHandlerStart(CInventoryOwner* inv_owner, LPCSTR phrase);
    bool OnDialogSoundHandlerStop(CInventoryOwner* inv_owner);

    virtual void reinit() override;
    virtual void reload(LPCSTR section) override;
    [[nodiscard]] virtual bool use_bolts() const override;

    virtual void OnItemTake(CInventoryItem* inventory_item) override;
    virtual void OnItemRuck(CInventoryItem* inventory_item, const SInvItemPlace& previous_place) override;
    virtual void OnItemBelt(CInventoryItem* inventory_item, const SInvItemPlace& previous_place) override;
    virtual void OnItemDrop(CInventoryItem* inventory_item, bool just_before_destroy) override;
    virtual void OnItemDropUpdate() override;
    virtual void OnPlayHeadShotParticle(NET_Packet P);

    virtual void Die(CObject* who) override;
    virtual void Hit(SHit* pHDS) override;
    virtual void PHHit(SHit& H) override;
    virtual void HitSignal(float P, Fvector& vLocalDir, CObject* who, s16 element) override;
    
    void HitSector(CObject* who, CObject* weapon);
    void HitMark(float P, Fvector dir, CObject* who, s16 element, Fvector position_in_bone_space, float impulse, ALife::EHitType hit_type);
    void Feel_Grenade_Update(float rad);

    [[nodiscard]] virtual float GetMass() override;
    [[nodiscard]] virtual float Radius() const override;
    virtual void g_PerformDrop();

    [[nodiscard]] virtual bool use_default_throw_force() override;
    [[nodiscard]] virtual float missile_throw_force() override;
    [[nodiscard]] virtual bool unlimited_ammo() override;

    [[nodiscard]] virtual bool NeedToDestroyObject() const override;
    [[nodiscard]] virtual ALife::_TIME_ID TimePassedAfterDeath() const;

public:
    virtual void UpdateArtefactsOnBeltAndOutfit();
    float HitArtefactsOnBelt(float hit_power, ALife::EHitType hit_type);
    float GetProtection_ArtefactsOnBelt(ALife::EHitType hit_type);

protected:
    ref_sound m_HeavyBreathSnd;
    ref_sound m_BloodSnd;
    ref_sound m_DangerSnd;

protected:
    float m_hit_slowmo = 0.0f;
    float m_hit_probability = 1.0f;
    s8 m_block_sprint_counter = 0;

    SndShockEffector* m_sndShockEffector = nullptr;
    xr_vector<ref_sound> sndHit[ALife::eHitTypeMax];
    ref_sound sndDie[SND_DIE_COUNT];

    float m_fLandingTime = 0.0f;
    float m_fJumpTime = 0.0f;
    float m_fFallTime = 0.0f;
    float m_fCamHeightFactor = 0.87f;

    BOOL b_DropActivated = FALSE;
    float f_DropPower = 0.0f;

    s32 m_ZoomRndSeed = 0;
    s32 m_ShotRndSeed = 0;

    bool m_bOutBorder = false;
    u32 m_feel_touch_characters = 0;

private:
    void SwitchOutBorder(bool new_border_state);

public:
    bool m_bAllowDeathRemove = false;
	void SetReverseGravity(bool state);
    bool m_bReverseGravity = false;
    float m_fReverseGravityProgress = 0.0f; 
	
    struct SOrbitObject {
        u16 id;                 
        float target_radius;    
        float base_speed;      
        float phase;            
        Fvector spin_axis;      
        float height_offset;    
		Fvector start_pos;      
        float time_captured;    
    };

    xr_vector<SOrbitObject> m_OrbitObjects;
    u8 m_AnomalyState = 0; 
    Fvector m_AnomalyCenter;
	
	float m_ActorCaptureTime = 0.0f;
    float m_AnomalyRadius = 15.0f;

    void StartOrbitAnomaly(Fvector center, float radius);
    void SetOrbitAttack();
    void StopOrbitAnomaly();
    void UpdateOrbitAnomaly(); 

    void SetZoomRndSeed(s32 Seed = 0);
    [[nodiscard]] s32 GetZoomRndSeed() const { return m_ZoomRndSeed; }
    void SetShotRndSeed(s32 Seed = 0);
    [[nodiscard]] s32 GetShotRndSeed() const { return m_ShotRndSeed; }

public:
    void detach_Vehicle();
    void steer_Vehicle(float angle);
    void attach_Vehicle(CHolderCustom* vehicle);

    [[nodiscard]] virtual bool can_attach(const CInventoryItem* inventory_item) const override;

protected:
    CItemUseController* m_item_use = nullptr;
    CHolderCustom* m_holder = nullptr;
    u16 m_holderID = u16(-1);
    bool use_Holder(CHolderCustom* holder);
    bool use_Vehicle(CHolderCustom* object);
    bool use_MountedWeapon(CHolderCustom* object);
    void ActorUse();

protected:
    BOOL m_bAnimTorsoPlayed = FALSE;
    static void AnimTorsoPlayCallBack(CBlend* B);

    SRotation r_torso;
    float r_torso_tgt_roll = 0.0f;
    SRotation unaffected_r_torso;

    float r_model_yaw_dest = 0.0f;
    float r_model_yaw = 0.0f;       
    float r_model_yaw_delta = 0.0f; 

public:
    SActorMotions* m_anims = nullptr;

    CBlend* m_current_legs_blend = nullptr;
    CBlend* m_current_torso_blend = nullptr;
    CBlend* m_current_jump_blend = nullptr;
    MotionID m_current_legs;
    MotionID m_current_torso;
    MotionID m_current_head;

    void SetCallbacks();
    void ResetCallbacks();
    static void Spin0Callback(CBoneInstance*);
    static void Spin1Callback(CBoneInstance*);
    static void ShoulderCallback(CBoneInstance*);
    static void HeadCallback(CBoneInstance*);
    static void VehicleHeadCallback(CBoneInstance*);

    [[nodiscard]] virtual const SRotation Orientation() const override { return r_torso; }
    SRotation& Orientation() { return r_torso; }

    void g_SetAnimation(u32 mstate_rl);
    void g_SetSprintAnimation(u32 mstate_rl, MotionID& head, MotionID& torso, MotionID& legs);

public:
    virtual void OnHUDDraw(CCustomHUD* hud) override;
    [[nodiscard]] BOOL HUDview() const;

    [[nodiscard]] virtual float ffGetFov() const override { return 90.f; }
    [[nodiscard]] virtual float ffGetRange() const override { return 500.f; }

public:
    CActorCameraManager& Cameras() {
        VERIFY(m_pActorEffector);
        return *m_pActorEffector;
    }
    [[nodiscard]] IC CCameraBase* cam_Active() const { return cameras[cam_active]; }
    [[nodiscard]] IC CCameraBase* cam_FirstEye() const { return cameras[eacFirstEye]; }

protected:
    virtual void cam_Set(EActorCameras style);
    void cam_Update(float dt, float fFOV);
    void cam_Lookout(const Fmatrix& xform, float camera_height);
    void camUpdateLadder(float dt);
    void cam_SetLadder();
    void cam_UnsetLadder();

    // �������� CONST
    [[nodiscard]] float currentFOV();

    CCameraBase* cameras[eacMaxCam] = {nullptr};
    EActorCameras cam_active = eacFirstEye;
    float fPrevCamPos = 0.0f;
    float current_ik_cam_shift = 0.0f;
    Fvector vPrevCamDir = {0.f, 0.f, 1.f};
    float fCurAVelocity = 0.0f;
    CEffectorBobbing* pCamBobbing = nullptr;

    CActorCameraManager* m_pActorEffector = nullptr;
    static float f_Ladder_cam_limit;

public:
    virtual void feel_touch_new(CObject* O) override;
    virtual void feel_touch_delete(CObject* O) override;
    virtual BOOL feel_touch_contact(CObject* O) override;
    virtual BOOL feel_touch_on_contact(CObject* O) override;

    [[nodiscard]] CGameObject* ObjectWeLookingAt() const { return m_pObjectWeLookingAt; }
    [[nodiscard]] CInventoryOwner* PersonWeLookingAt() const { return m_pPersonWeLookingAt; }
    [[nodiscard]] LPCSTR GetDefaultActionForObject() const { return *m_sDefaultObjAction; }

protected:
    CUsableScriptObject* m_pUsableObject = nullptr;
    CInventoryOwner* m_pPersonWeLookingAt = nullptr;
    CHolderCustom* m_pVehicleWeLookingAt = nullptr;
    CGameObject* m_pObjectWeLookingAt = nullptr;
    CInventoryBox* m_pInvBoxWeLookingAt = nullptr;

    shared_str m_sDefaultObjAction;
    shared_str m_sCharacterUseAction;
    shared_str m_sDeadCharacterUseAction;
    shared_str m_sDeadCharacterUseOrDragAction;
    shared_str m_sDeadCharacterDontUseAction;
    shared_str m_sCarCharacterUseAction;
    shared_str m_sInventoryItemUseAction;
    shared_str m_sInventoryBoxUseAction;

    bool m_bPickupMode = false;
    float m_fFeelGrenadeRadius = 10.0f;
    float m_fFeelGrenadeTime = 1.0f; 
    float m_fPickupInfoRadius = 0.0f;

    void PickupModeUpdate();
    void PickupInfoDraw(CObject* object);
    void PickupModeUpdate_COD();

public:
    void g_cl_CheckControls(u32 mstate_wf, Fvector& vControlAccel, float& Jump, float dt);
    void g_cl_ValidateMState(float dt, u32 mstate_wf);
    void g_cl_Orientate(u32 mstate_rl, float dt);
    void g_sv_Orientate(u32 mstate_rl, float dt);
    void g_Orientate(u32 mstate_rl, float dt);
    bool g_LadderOrient();

    // �������� CONST
    [[nodiscard]] bool CanAccelerate();
    [[nodiscard]] bool CanJump();
    [[nodiscard]] bool CanMove();
    [[nodiscard]] float CameraHeight();
    [[nodiscard]] bool CanSprint();
    [[nodiscard]] bool CanRun();
    [[nodiscard]] bool is_jump();

    void StopAnyMove();

    [[nodiscard]] inline bool AnyAction() const { return (mstate_real & mcAnyAction) != 0; }
    [[nodiscard]] inline bool AnyMove() const { return (mstate_real & mcAnyMove) != 0; }
    [[nodiscard]] inline u32 MovingState() const { return mstate_real; }

protected:
    u32 mstate_wishful = 0;
    u32 mstate_old = 0;
    u32 mstate_real = 0;

    BOOL m_bJumpKeyPressed = FALSE;

    float m_fWalkAccel = 0.0f;
    float m_fJumpSpeed = 0.0f;
    float m_fRunFactor = 2.0f;
    float m_fRunBackFactor = 0.0f;
    float m_fWalkBackFactor = 0.0f;
    float m_fCrouchFactor = 0.2f;
    float m_fClimbFactor = 1.0f;
    float m_fSprintFactor = 4.0f;

    float m_fWalk_StrafeFactor = 1.0f;
    float m_fRun_StrafeFactor = 1.0f;

public:
    [[nodiscard]] Fvector GetMovementSpeed() const { return NET_SavedAccel; }

public:
    virtual void IR_OnMouseMove(int x, int y) override;
    virtual void IR_OnKeyboardPress(int dik) override;
    virtual void IR_OnKeyboardRelease(int dik) override;
    virtual void IR_OnKeyboardHold(int dik) override;
    virtual void IR_OnMouseWheel(int direction) override;
    [[nodiscard]] virtual float GetLookFactor();

public:
    virtual void g_WeaponBones(int& L, int& R1, int& R2);
    virtual void g_fireParams(const CHudItem* pHudItem, Fvector& P, Fvector& D);
    [[nodiscard]] virtual bool g_stateFire() override { return !((mstate_wishful & mcLookout) && !IsGameTypeSingle()); }
    [[nodiscard]] virtual BOOL g_State(SEntityState& state) const override;
    [[nodiscard]] virtual float GetWeaponAccuracy() const override;
    
    [[nodiscard]] float GetFireDispertion() const { return m_fdisp_controller.GetCurrentDispertion(); }
    [[nodiscard]] bool IsZoomAimingMode() const { return m_bZoomAimingMode; }
    
    [[nodiscard]] virtual float MaxCarryWeight() const override;
    [[nodiscard]] float MaxWalkWeight() const;
    [[nodiscard]] float get_additional_weight() const;

protected:
    CFireDispertionController m_fdisp_controller;
    void SetZoomAimingMode(bool val) { m_bZoomAimingMode = val; }
    bool m_bZoomAimingMode = false;

    float m_fDispBase = 0.0f;
    float m_fDispAim = 0.0f;
    float m_fDispVelFactor = 0.0f;
    float m_fDispAccelFactor = 0.0f;
    float m_fDispCrouchFactor = 0.0f;
    float m_fDispCrouchNoAccelFactor = 0.0f;

protected:
    int m_r_hand;
    int m_l_finger1;
    int m_r_finger2;
    int m_head;
    int m_eye_left;
    int m_eye_right;
    int m_l_clavicle;
    int m_r_clavicle;
    int m_spine2;
    int m_spine1;
    int m_spine;
    int m_neck;

protected:
    void ConvState(u32 mstate_rl, string128* buf);

public:
    virtual BOOL net_Spawn(CSE_Abstract* DC) override;
    virtual void net_Export(NET_Packet& P) override; 
    virtual void net_Import(NET_Packet& P) override; 
    virtual void net_Destroy() override;
    [[nodiscard]] virtual BOOL net_Relevant() override; 
    virtual void net_Relcase(CObject* O) override; 
    virtual void xr_stdcall on_requested_spawn(CObject* object);
    
    virtual void save(NET_Packet& output_packet) override;
    virtual void load(IReader& input_packet) override;
    virtual void net_Save(NET_Packet& P) override;
    [[nodiscard]] virtual BOOL net_SaveRelevant() override;

protected:
    xr_deque<net_update> NET;
    Fvector NET_SavedAccel;
    net_update NET_Last;
    BOOL NET_WasInterpolating = FALSE; 
    u32 NET_Time = 0;             

    void net_Import_Base(NET_Packet& P);
    void net_Import_Physic(NET_Packet& P);
    void net_Import_Base_proceed();
    void net_Import_Physic_proceed();

    [[nodiscard]] virtual bool can_validate_position_on_spawn() override { return false; }
    
    xr_deque<net_update_A> NET_A;

    float SCoeff[3][4];          
    float HCoeff[3][4];          
    Fvector IPosS, IPosH, IPosL; 

#ifdef DEBUG
    using VIS_POSITION = xr_deque<Fvector>;
    VIS_POSITION LastPosS;
    VIS_POSITION LastPosH;
    VIS_POSITION LastPosL;
#endif

    SPHNetState LastState;
    SPHNetState RecalculatedState;
    SPHNetState PredictedState;

    InterpData IStart;
    InterpData IRec;
    InterpData IEnd;

    bool m_bInInterpolation = false;
    bool m_bInterpolate = false;
    u32 m_dwIStartTime = 0;
    u32 m_dwIEndTime = 0;
    u32 m_dwILastUpdateTime = 0;

    using PH_STATES = xr_deque<SPHNetState>;
    PH_STATES m_States;
    u16 m_u16NumBones = 0;
    
    void net_ExportDeadBody(NET_Packet& P);
    void CalculateInterpolationParams();
    virtual void make_Interpolation();
    
#ifdef DEBUG
    virtual void OnRender_Network();
#endif

public:
    void g_Physics(Fvector& accel, float jump, float dt);
    virtual void ForceTransform(const Fmatrix& m) override;
    void SetPhPosition(const Fmatrix& pos);
    virtual void PH_B_CrPr() override; 
    virtual void PH_I_CrPr() override; 
    virtual void PH_A_CrPr() override; 
    virtual void MoveActor(Fvector NewPos, Fvector NewDir);

    virtual void SpawnAmmoForWeapon(CInventoryItem* pIItem);
    virtual void RemoveAmmoForWeapon(CInventoryItem* pIItem);
    virtual void spawn_supplies() override;
    [[nodiscard]] virtual bool human_being() const override { return true; }

    [[nodiscard]] virtual shared_str GetDefaultVisualOutfit() const { return m_DefaultVisualOutfit; }
    virtual void SetDefaultVisualOutfit(shared_str DefaultOutfit) { m_DefaultVisualOutfit = DefaultOutfit; }
    virtual void UpdateAnimation() { g_SetAnimation(mstate_real); }

    virtual void ChangeVisual(shared_str NewVisual);
    virtual void OnChangeVisual() override;

    virtual void RenderIndicator(Fvector dpos, float r1, float r2, const ui_shader& IndShader);
    virtual void RenderText(LPCSTR Text, Fvector dpos, float* pdup, u32 color);

    void set_input_external_handler(CActorInputHandler* handler);
    [[nodiscard]] bool input_external_handler_installed() const { return (m_input_external_handler != nullptr); }

    IC void lock_accel_for(u32 time) { m_time_lock_accel = Device.dwTimeGlobal + time; }

private:
    CActorInputHandler* m_input_external_handler = nullptr;
    u32 m_time_lock_accel = 0;

protected:
    CStatGraph* pStatGraph = nullptr;

    shared_str m_DefaultVisualOutfit;
    LPCSTR invincibility_fire_shield_3rd = nullptr;
    LPCSTR invincibility_fire_shield_1st = nullptr;
    shared_str m_sHeadShotParticle;
    u32 last_hit_frame = 0;
    
#ifdef DEBUG
    friend class CLevelGraph;
#endif
    Fvector m_AutoPickUp_AABB;
    Fvector m_AutoPickUp_AABB_Offset;

    void Check_for_AutoPickUp();
    void SelectBestWeapon(CObject* O);

public:
    void SetWeaponHideState(u16 State, bool bSet);

private: 
    virtual void HideAllWeapons(bool v) override { SetWeaponHideState(INV_STATE_BLOCK_ALL, v); }

public:
    void SetCantRunState(bool bSet);

private:
    CActorCondition* m_entity_condition = nullptr;

protected:
    virtual CEntityConditionSimple* create_entity_condition(CEntityConditionSimple* ec) override;

public:
    [[nodiscard]] IC CActorCondition& conditions() const;
    [[nodiscard]] virtual DLL_Pure* _construct() override;
    [[nodiscard]] virtual bool natural_weapon() const override { return false; }
    [[nodiscard]] virtual bool natural_detector() const override { return false; }
    [[nodiscard]] virtual bool use_center_to_aim() const override;

protected:
    u16 m_iLastHitterID = u16(-1);
    u16 m_iLastHittingWeaponID = u16(-1);
    s16 m_s16LastHittedElement = 0;
    Fvector m_vLastHitDir;
    Fvector m_vLastHitPos;
    float m_fLastHealth = 0.0f;
    bool m_bWasHitted = false;
    bool m_bWasBackStabbed = false;

    virtual bool Check_for_BackStab_Bone(u16 element);

public:
    virtual void SetHitInfo(CObject* who, CObject* weapon, s16 element, Fvector Pos, Fvector Dir) override;

    virtual void OnHitHealthLoss(float NewHealth) override;
    virtual void OnCriticalHitHealthLoss() override;
    virtual void OnCriticalWoundHealthLoss() override;
    virtual void OnCriticalRadiationHealthLoss() override;

    [[nodiscard]] virtual bool InventoryAllowSprint();
    virtual void OnNextWeaponSlot();
    virtual void OnPrevWeaponSlot();
    void SwitchNightVision();
    void SwitchTorch();
    
#ifdef DEBUG
    void NoClipFly(int cmd);
#endif 

public:
    virtual void on_weapon_shot_start(CWeapon* weapon) override;
    virtual void on_weapon_shot_update() override;
    virtual void on_weapon_shot_stop() override;
    virtual void on_weapon_shot_remove(CWeapon* weapon) override;
    virtual void on_weapon_hide(CWeapon* weapon) override;
    Fvector weapon_recoil_delta_angle();
    Fvector weapon_recoil_last_delta();

protected:
    virtual void update_camera(CCameraShotEffector* effector);
    [[nodiscard]] virtual bool is_on_ground() override;

private:
    CActorMemory* m_memory = nullptr;

public:
    [[nodiscard]] IC CActorMemory& memory() const {
        VERIFY(m_memory);
        return (*m_memory);
    }

    void OnDifficultyChanged();

    [[nodiscard]] IC float HitProbability() const { return m_hit_probability; }
    [[nodiscard]] virtual CVisualMemoryManager* visual_memory() const override;
    virtual BOOL BonePassBullet(int boneID) override;
    virtual void On_B_NotCurrentEntity() override;

private:
    collide::rq_results RQR;
    BOOL CanPickItem(const CFrustum& frustum, const Fvector& from, CObject* item);
    xr_vector<ISpatial*> ISpatialResult;

private:
    CLocationManager* m_location_manager = nullptr;

public:
    [[nodiscard]] IC const CLocationManager& locations() const {
        VERIFY(m_location_manager);
        return (*m_location_manager);
    }

private:
    ALife::_OBJECT_ID m_holder_id;

public:
    [[nodiscard]] virtual bool register_schedule() const override { return false; }
    [[nodiscard]] virtual bool is_ai_obstacle() const override;

    float GetRestoreSpeed(ALife::EConditionRestoreType const& type);

public:
    virtual void On_SetEntity() override;
    virtual void On_LostEntity() override {};

    void DisableHitMarks(bool disable) { m_disabled_hitmarks = disable; }
    [[nodiscard]] bool DisableHitMarks() const { return m_disabled_hitmarks; }

    void set_inventory_disabled(bool is_disabled) { m_inventory_disabled = is_disabled; }
    [[nodiscard]] bool inventory_disabled() const { return m_inventory_disabled; }

private:
    void set_state_box(u32 mstate);

private:
    bool m_disabled_hitmarks = false;
    bool m_inventory_disabled = false;

    DECLARE_SCRIPT_REGISTER_FUNCTION
};
add_to_type_list(CActor)
#undef script_type_list
#define script_type_list save_type_list(CActor)

extern bool isActorAccelerated(u32 mstate, bool ZoomMode);

[[nodiscard]] IC CActorCondition& CActor::conditions() const {
    VERIFY(m_entity_condition);
    return (*m_entity_condition);
}

extern CActor* g_actor;
[[nodiscard]] CActor* Actor();
extern const float s_fFallTime;
