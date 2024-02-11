#pragma once
// Build config defines

// --- xrCore ---
// don't crash game when VERIFY fails
#define NON_FATAL_VERIFY

// --- xrRender --- 
// HQ Geometry fix by Shoker.
// It will work correctly only with skin.h 
// shader with HQ geometry fix support.
// #define HQ_GEOMETRY_FIX

// Use SHOC THM files format. If you want 
// to use textures from SHOC mods, I recommend
// turn it on.
// #define USE_SHOC_THM_FORMAT

// use dynamic sun movement. If this is not
// defined sun will move as configured in weather ltx files
// #define DYNAMIC_SUN_MOVEMENT

// limit FPS in menu to prevent video card overheat (by alpet) 
// TODO: Repair build when defined
// #define ECO_RENDER

// Use full cube Skybox maps.
// Taken from https://vk.cc/c4Pcff
// Requires changes for Skyboxes
// #define SKYBOX_FULL_MAPS

// --- xrGame ---
// restore collision with dead bodies (thanks malandrinus) (Note: Collides with AI and they can get stuck)
// #define DEAD_BODY_COLLISION
