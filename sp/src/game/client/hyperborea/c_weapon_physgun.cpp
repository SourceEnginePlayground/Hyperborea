#include "cbase.h"
#include "c_weapon_physgun.h"
#include "dlight.h"
#include "iefx.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

CLIENTEFFECT_REGISTER_BEGIN(PrecacheEffectPhysGun)
CLIENTEFFECT_MATERIAL("sprites/physbeam")
CLIENTEFFECT_MATERIAL("effects/combinemuzzle2")
CLIENTEFFECT_REGISTER_END()

STUB_WEAPON_CLASS_IMPLEMENT(weapon_physgun, C_WeaponPhysGun);
IMPLEMENT_CLIENTCLASS_DT(C_WeaponPhysGun, DT_WeaponPhysGun, CWeaponPhysGun)
	RecvPropVector(RECVINFO_NAME(Beam.TargetPosition, TargetPosition)),
	RecvPropVector(RECVINFO_NAME(Beam.WorldPosition, WorldPosition)),
	RecvPropInt(RECVINFO_NAME(Beam.Active, Active)),
	RecvPropInt(RECVINFO_NAME(Beam.ViewModelIndex, ViewModelIndex)),
END_RECV_TABLE()

C_BeamQuadratic::C_BeamQuadratic()
{
	Owner = nullptr;
}

void C_BeamQuadratic::Update(C_BaseEntity* owner)
{
	Owner = owner;

	if (Active)
	{
		if (m_hRenderHandle == INVALID_CLIENT_RENDER_HANDLE)
		{
			ClientLeafSystem()->AddRenderable(this, RENDER_GROUP_VIEW_MODEL_TRANSLUCENT); // RENDER_GROUP_TRANSLUCENT_ENTITY
		}
		else
		{
			ClientLeafSystem()->SetRenderGroup(m_hRenderHandle, RENDER_GROUP_VIEW_MODEL_TRANSLUCENT);
			ClientLeafSystem()->RenderableChanged(m_hRenderHandle);
		}
	}
	else if (!Active && m_hRenderHandle != INVALID_CLIENT_RENDER_HANDLE)
	{
		ClientLeafSystem()->RemoveRenderable(m_hRenderHandle);
	}
}

void C_BeamQuadratic::DrawPhysgunBeamQuadratic(const Vector& start, const Vector& control, const Vector& end, float width, const Vector& color, float scrollOffset, float flHDRColorScale)
{
	int Subdivisions = 48;

	CMatRenderContextPtr pRenderContext(g_pMaterialSystem);
	CBeamSegDraw beamDraw;
	beamDraw.Start(pRenderContext, Subdivisions + 1, NULL);

	BeamSeg_t Segment;
	Segment.m_flAlpha = 1.0;
	Segment.m_flWidth = width;

	float t = 0;
	float u = fmod(scrollOffset, 1);
	float dt = 1.0 / (float)Subdivisions;
	for (int i = 0; i <= Subdivisions; i++, t += dt)
	{
		float omt = (1 - t);
		float p0 = omt * omt;
		float p1 = 2 * t * omt;
		float p2 = t * t;

		Segment.m_vPos = p0 * start + p1 * control + p2 * end;
		Segment.m_flTexCoord = u - t;
	//	if (i == 0 || i == Subdivisions)
		if (i == Subdivisions)
		{
			// HACK: fade out the ends a bit
			Segment.m_vColor = vec3_origin;
		}
		else
		{
			Segment.m_vColor = color;
		}

		beamDraw.NextSeg(&Segment);
	}
	beamDraw.End();
}

int	C_BeamQuadratic::DrawModel(int flags)
{
	Vector Points[3];
	QAngle tmpAngle;

	if (!Active)
		return 0;

	C_BaseViewModel* CurrentViewmodel = dynamic_cast<C_BaseViewModel*>(cl_entitylist->GetEnt(ViewModelIndex));
	if (CurrentViewmodel == nullptr)
		return 0;

	CurrentViewmodel->GetAttachment(CurrentViewmodel->LookupAttachment("muzzle"), Points[0]);

	Points[1] = 0.5 * (TargetPosition + Points[0]);
	
	// a little noise 11t & 13t should be somewhat non-periodic looking
	Points[1].z += 4 * sin(gpGlobals->curtime * 11) + 5 * cos(gpGlobals->curtime * 13);
	Points[2] = WorldPosition;

	IMaterial* MaterialBeam = materials->FindMaterial("sprites/physbeam", TEXTURE_GROUP_CLIENT_EFFECTS);
	Vector Color(1, 1, 1);

	float ScrollOffset = gpGlobals->curtime - (int)gpGlobals->curtime;
	materials->GetRenderContext()->Bind(MaterialBeam);
	DrawPhysgunBeamQuadratic(Points[0], Points[1], Points[2], 3.5f, Color, ScrollOffset);

	IMaterial* MaterialGlow = materials->FindMaterial("effects/combinemuzzle2", TEXTURE_GROUP_CLIENT_EFFECTS);
	CMatRenderContextPtr pRenderContext(g_pMaterialSystem);
	pRenderContext->Bind(MaterialGlow);
	float scale = random->RandomFloat(1.6f, 1.9f);
	float color[3];
	color[0] = 1.0f;
	color[1] = 1.0f;
	color[2] = 1.0f;
	DrawHalo(MaterialGlow, Points[0], random->RandomFloat(6.0f*scale, 6.5f*scale), color);
	DrawHalo(MaterialGlow, Points[2], random->RandomFloat(2.0f*scale, 3.5f*scale), color);

	dlight_t* MuzzleLight;
	MuzzleLight = effects->CL_AllocDlight(0);
	MuzzleLight->color.r = random->RandomInt(180, 255);
	MuzzleLight->color.g = random->RandomInt(0, 60);
	MuzzleLight->color.b = random->RandomInt(0, 100);
	MuzzleLight->radius = 64;
	MuzzleLight->minlight = 128.0 / 256.0f;
	MuzzleLight->origin = Points[0];
	MuzzleLight->die = gpGlobals->curtime; // + 0.1f;

	dlight_t* BeamLight;
	BeamLight = effects->CL_AllocDlight(1);
	BeamLight->color.r = MuzzleLight->color.r;
	BeamLight->color.g = MuzzleLight->color.g;
	BeamLight->color.b = MuzzleLight->color.b;
	BeamLight->radius = random->RandomInt(64, 100);
	BeamLight->minlight = 128.0 / 256.0f;
	BeamLight->origin = Points[2];
	BeamLight->die = gpGlobals->curtime; // + 0.1f;

	return 1;
}
