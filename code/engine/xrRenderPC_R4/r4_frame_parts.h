#pragma once
// Written by Ivan Danko, 2026-11-14: item 9 "full measurement" - the render parts of a frame for
// "cpu_frame_stats 1" ( DX11 only ). CRender::Calculate opens a view ( the main one or the 3D
// scope's lens ), each mark ends a part: the work since the boundary before belongs to it - its CPU
// time and a GPU timestamp are taken there ( xrRenderCommon\dxRenderDeviceRender.cpp ). The end of
// CRender::Render counts the view's parts; a view that returned early ( the menu, no level ) is not
// counted. The lens view is one part, its own marks do nothing.
// Створено (Ivan Danko, 2026-11-14): пункт 9 "повний замір" - частини рендеру кадру для
// "cpu_frame_stats 1" ( тільки DX11 ). CRender::Calculate відкриває вид ( основний або лінзи 3D
// прицілу ), кожна позначка завершує частину: робота від попередньої межі належить їй - там
// беруться її час процесора й мітка часу відеокарти ( xrRenderCommon\dxRenderDeviceRender.cpp ).
// Кінець CRender::Render зараховує частини виду; вид, що завершився раніше ( меню, немає рівня ),
// не зараховується. Вид лінзи - одна частина, його власні позначки нічого не роблять.

enum r4_frame_part : u32 {
    R4P_VISIBILITY,  // visibility, HOM, the z-prefill, the main calc
    R4P_GPU_WAIT,    // the wait for the GPU ( the sync point )
    R4P_SCENE,       // the scene into the G-buffer, the light tests, the wallmarks, the rain
    R4P_GRASS,       // the grass ( Details )
    R4P_SSS,         // Screen Space Shaders' contact shadows
    R4P_SUN,         // the sun and its shadows
    R4P_EMISSIVE,    // the emissive geometry
    R4P_LIGHTS,      // the lights and the volumetric blur
    R4P_AO_IL,       // the ambient occlusion and the indirect light
    R4P_COMBINE,     // the combine, the reflections and the water
    R4P_TRANSPARENT, // the forward ( transparent ) geometry
    R4P_POST,        // the post-process
    R4P_LENS,        // the 3D scope's lens view, whole
    R4P_COUNT
};

void r4_frame_parts_pass_begin();   // CRender::Calculate: a view starts
void r4_frame_parts_mark(u32 part); // a part ends here
void r4_frame_parts_pass_end();     // CRender::Render reached its end
