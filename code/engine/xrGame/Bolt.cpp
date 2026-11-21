#include "stdafx.h"
#include "bolt.h"
#include "actor.h"
#include "game_object_space.h"
#include "script_callback_ex.h"
#include "script_game_object.h"
#include "ParticlesObject.h"
#include "../xrphysics/PhysicsShell.h"
#include "xr_level_controller.h"

CBolt::CBolt(void) { m_thrower_id = u16(-1); }

CBolt::~CBolt(void) {}

void CBolt::OnH_A_Chield() {
    inherited::OnH_A_Chield();
    CObject* o = H_Parent()->H_Parent();
    if (o)
        SetInitiator(o->ID());
}

void CBolt::Throw() {
    CMissile* l_pBolt = smart_cast<CMissile*>(m_fake_missile);
    if (!l_pBolt)
        return;
    l_pBolt->set_destroy_time(u32(m_dwDestroyTimeMax / phTimefactor));
    auto* actor = OnServer() && Local() ? smart_cast<CActor*>(H_Parent()) : nullptr;
    inherited::Throw();
    spawn_fake_missile();
    if (actor)
        actor->callback(GameObject::eOnActorBoltThrow)(lua_game_object());
}

bool CBolt::Useful() const { return false; }

bool CBolt::Action(u16 cmd, u32 flags) {
    if (inherited::Action(cmd, flags))
        return true;
    /*
            switch(cmd)
            {
            case kDROP:
                    {
                            if(flags&CMD_START)
                            {
                                    m_throw = false;
                                    if(State() == MS_IDLE) State(MS_THREATEN);
                            }
                            else if(State() == MS_READY || State() == MS_THREATEN)
                            {
                                    m_throw = true;
                                    if(State() == MS_READY) State(MS_THROW);
                            }
                    }
                    return true;
            }
    */
    return false;
}

void CBolt::activate_physic_shell() {
    inherited::activate_physic_shell();
    m_pPhysicsShell->SetAirResistance(.0001f);
}

void CBolt::SetInitiator(u16 id) { m_thrower_id = id; }
u16 CBolt::Initiator() { return m_thrower_id; }