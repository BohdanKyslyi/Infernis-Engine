#pragma once

// Screen Space Shaders: Monolith xrGame\raypick.h ( 2026.9.12 ), for the script class ray_pick
// ( level_script.cpp, registered only while SSS runs ) that Wind Leaves of the Weather_ATMOS addon
// casts to find a roof over the player. As the donor's, but for #pragma once, level.h ( Level() in
// set() below; the stdafx.h of Infernis does not declare it ) and the Russian comments in English.

#include "pch_script.h"
#include "gameobject.h"
#include "script_game_object.h"
#include "../xrcdb/xr_collide_defs.h"
#include "material_manager.h"
#include "level.h"

struct script_rq_result
{
	CScriptGameObject* O;
	float range;
	int element;

	// Material of tri of ray query result
	str_c pMaterialName;
	u32 pMaterialFlags;

	// physics part
	float fPHFriction; // ?
	float fPHDamping; // ?
	float fPHSpring; // ?
	float fPHBounceStartVelocity; // ?
	float fPHBouncing; // ?
					   // shoot&bounce&visibility&flotation
	float fFlotationFactor; // 0.f - 1.f (1.f - fully passable)
	float fShootFactor; // 0.f - 1.f (1.f - fully shoot-through)
	float fShootFactorMP; // 0.f - 1.f (1.f - fully shoot-through)
	float fBounceDamageFactor; // 0.f - 100.f
	float fInjuriousSpeed; // 0.f - ... (0.f - takes no health (speed of the health loss))
	float fVisTransparencyFactor; // 0.f - 1.f (1.f - fully transparent)
	float fSndOcclusionFactor; // 0.f - 1.f (1.f - fully audible)
	float fDensityFactor;

	script_rq_result()
	{
		O = nullptr;
		range = 0;
		element = 0;
	};

	void reset() {
		O = nullptr;
		range = 0;
		element = 0;

		pMaterialName = nullptr;
		pMaterialFlags = 0;

		fPHFriction = 0;
		fPHDamping = 0;
		fPHSpring = 0;
		fPHBounceStartVelocity = 0;
		fPHBouncing = 0;
		fFlotationFactor = 0;
		fShootFactor = 0;
		fShootFactorMP = 0;
		fBounceDamageFactor = 0;
		fInjuriousSpeed = 0;
		fVisTransparencyFactor = 0;
		fSndOcclusionFactor = 0;
		fDensityFactor = 0;
	}

	void set(collide::rq_result& R)
	{
		if (R.O)
		{
			CGameObject* go = smart_cast<CGameObject *>(R.O);
			if (go)
				O = go->lua_game_object();
		}
		range = R.range;
		element = R.element;

		// demonized: set material params of ray pick result
		//Msg("no object, check material");
		auto pTri = Level().ObjectSpace.GetStaticTris() + R.element;
		auto pMaterial = GMLib.GetMaterialByIdx(pTri->material);
		auto pMaterialFlagsRQ = pMaterial->Flags;
		//pTri = pTri;
		//pMaterial = pMaterial;
		pMaterialFlags = pMaterialFlagsRQ.flags;
		pMaterialName = pMaterial->m_Name.c_str();

		fPHFriction = pMaterial->fPHFriction;
		fPHDamping = pMaterial->fPHDamping;
		fPHSpring = pMaterial->fPHSpring;
		fPHBounceStartVelocity = pMaterial->fPHBounceStartVelocity;
		fPHBouncing = pMaterial->fPHBouncing;
		fFlotationFactor = pMaterial->fFlotationFactor;
		fShootFactor = pMaterial->fShootFactor;
		fShootFactorMP = pMaterial->fShootFactorMP;
		fBounceDamageFactor = pMaterial->fBounceDamageFactor;
		fInjuriousSpeed = pMaterial->fInjuriousSpeed;
		fVisTransparencyFactor = pMaterial->fVisTransparencyFactor;
		fSndOcclusionFactor = pMaterial->fSndOcclusionFactor;
		fDensityFactor = pMaterial->fDensityFactor;
	};
};

// class for performing ray pick
struct CRayPick
{
	Fvector start_position;
	Fvector direction;
	float range;
	collide::rq_target flags;
	script_rq_result result;
	xr_vector<CObject*> ignore;

	CRayPick();
	CRayPick(const Fvector& P, const Fvector& D, float R, collide::rq_target F, CScriptGameObject* I);

	IC void set_position(Fvector& P) { start_position = P; };
	IC void set_direction(Fvector& D) { direction = D; };
	IC void set_range(float R) { range = R; };
	IC void set_flags(collide::rq_target F) { flags = F; };
	void set_ignore_object(CScriptGameObject* I) { 
		if (I) {
			CObject* obj = smart_cast<CObject*>(&(I->object()));
			if (obj && std::find(ignore.begin(), ignore.end(), obj) == ignore.end()) {
				ignore.push_back(obj);
			}
		}
	};

	bool query();
	Fvector get_normal();

	IC script_rq_result get_result() { return result; };
	IC CScriptGameObject* get_object() { return result.O; };
	IC float get_distance() { return result.range; };
	IC int get_element() { return result.element; };
};
