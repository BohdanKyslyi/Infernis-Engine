#include "pch_script.h"
#include "UIButton.h"
#include "UI3tButton.h"
#include "UICheckButton.h"
#include "UIRadioButton.h"
#include "UISpinNum.h"
#include "UISpinText.h"
#include "UITrackBar.h"

using namespace luabind;

#pragma optimize("s", on)
void CUIButton::script_register(lua_State* L) {
    module(L)[class_<CUIButton, CUIStatic>("CUIButton").def(constructor<>()),

              class_<CUI3tButton, CUIButton>("CUI3tButton").def(constructor<>()),

              class_<CUICheckButton, CUI3tButton>("CUICheckButton")
                  .def(constructor<>())
                  .def("GetCheck", &CUICheckButton::GetCheck)
                  .def("SetCheck", &CUICheckButton::SetCheck)
                  .def("SetDependControl", &CUICheckButton::SetDependControl),

              class_<CUICustomSpin, CUIWindow>("CUICustomSpin")
                  .def("GetText", &CUICustomSpin::GetText),

              class_<CUISpinNum, CUICustomSpin>("CUISpinNum").def(constructor<>()),

              class_<CUISpinFlt, CUICustomSpin>("CUISpinFlt").def(constructor<>()),

              class_<CUISpinText, CUICustomSpin>("CUISpinText").def(constructor<>()),

              class_<CUITrackBar, CUIWindow>("CUITrackBar")
                  .def(constructor<>())
                  .def("GetCheck", &CUITrackBar::GetCheck)
                  .def("SetCheck", &CUITrackBar::SetCheck)
                  .def("GetIValue", &CUITrackBar::GetIValue)
                  .def("GetFValue", &CUITrackBar::GetFValue)
                  // Ported by Ivan Danko, 2026-11-18, from Monolith (author themrdemonized): item 13
                  // "Mods" - a script sets the value, step and direction of a slider ( the "Mods"
                  // window, ui_mods_menu.script )
                  // Перенесення з Monolith (автор themrdemonized) - Ivan Danko, 2026-11-18: пункт 13
                  // «Моди» - скрипт задає значення, крок і напрямок повзунка ( вікно «Моди»,
                  // ui_mods_menu.script )
                  .def("SetIValue", &CUITrackBar::SetIValue)
                  .def("SetFValue", &CUITrackBar::SetFValue)
                  .def("SetStep", &CUITrackBar::SetStep)
                  .def("GetInvert", &CUITrackBar::GetInvert)
                  .def("SetInvert", &CUITrackBar::SetInvert)
                  .def("SetOptIBounds", &CUITrackBar::SetOptIBounds)
                  .def("SetOptFBounds", &CUITrackBar::SetOptFBounds)
                  .def("SetCurrentValue", &CUITrackBar::SetCurrentOptValue)];
}