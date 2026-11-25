////////////////////////////////////////////////////////////////////////////
//	Module 		: script_sound_script.cpp
//	Created 	: 06.02.2004
//  Modified 	: 06.02.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script sound class script export
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "script_particles.h"

using namespace luabind;

#pragma optimize("s", on)
void CScriptParticles::script_register(lua_State* L) {
    module(L)[class_<CScriptParticles>("particles_object")
                  .def(constructor<LPCSTR>())
                  // Ported by Ivan Danko, 2026-11-17, from Monolith (author themrdemonized): FPS plan step 2 - play / play_at_pos with the HUD
                  // mode flag of Anomaly (Monolith). Adapted: two overloads each, so the calls without
                  // the flag keep working.
                  // Перенесення з Monolith (автор themrdemonized) - Ivan Danko, 2026-11-17: крок 2 плану FPS - play / play_at_pos з
                  // прапорцем режиму HUD з Anomaly (Monolith). Адаптовано: по два варіанти, тож
                  // виклики без прапорця працюють і далі.
                  .def("play", (void (CScriptParticles::*)())(&CScriptParticles::Play))
                  .def("play", (void (CScriptParticles::*)(bool))(&CScriptParticles::Play))
                  .def("play_at_pos",
                       (void (CScriptParticles::*)(const Fvector&))(&CScriptParticles::PlayAtPos))
                  .def("play_at_pos",
                       (void (CScriptParticles::*)(const Fvector&, bool))(&CScriptParticles::PlayAtPos))
                  .def("stop", &CScriptParticles::Stop)
                  .def("stop_deffered", &CScriptParticles::StopDeffered)

                  .def("playing", &CScriptParticles::IsPlaying)
                  .def("looped", &CScriptParticles::IsLooped)

                  .def("move_to", &CScriptParticles::MoveTo)
                  // Ported by Ivan Danko, 2026-11-17, from Monolith (author themrdemonized): FPS plan step 2 - the Lua functions of Anomaly
                  // (Monolith script_particles_script.cpp).
                  // Перенесення з Monolith (автор themrdemonized) - Ivan Danko, 2026-11-17: крок 2 плану FPS - функції Lua з Anomaly
                  // (Monolith script_particles_script.cpp).
                  .def("set_position", &CScriptParticles::XFORMMoveTo)
                  .def("set_direction", &CScriptParticles::SetDirection)
                  .def("set_orientation", &CScriptParticles::SetOrientation)
                  .def("set_hud_mode", &CScriptParticles::SetHudMode)
                  .def("last_position", &CScriptParticles::LastPosition)

                  .def("load_path", &CScriptParticles::LoadPath)
                  .def("start_path", &CScriptParticles::StartPath)
                  .def("stop_path", &CScriptParticles::StopPath)
                  .def("pause_path", &CScriptParticles::PausePath)];
}
