#pragma once

class CActor;
class IKinematics;

// Renders a dedicated lower-body model in first person and drives it with the
// actor's existing third-person skeleton. The visual is selected from the
// currently active player HUD section, so changing an outfit/actor_hud also
// changes the legs without duplicating actor animation logic.
class CActorLegsController {
public:
    CActorLegsController();
    ~CActorLegsController();

    void Load(const shared_str& player_hud_section);
    void Render();

private:
    void DestroyModel();
    bool VisualExists(LPCSTR visual_name) const;
    bool SyncBones(CActor* actor);

private:
    IKinematics* m_model;
    shared_str m_visual_name;
    Fmatrix m_transform;
    float m_forward_offset;
    float m_vertical_offset;
    bool m_attach_to_camera;
    bool m_reported_skeleton_mismatch;
};
