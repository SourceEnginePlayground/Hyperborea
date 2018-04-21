#include "cbase.h"
#include "hud.h"
#include "in_buttons.h"
#include "beamdraw.h"
#include "c_weapon__stubs.h"
#include "clienteffectprecachesystem.h"

#include "view_shared.h"
#include "iviewrender.h"
#include "view.h"

class C_BeamQuadratic : public CDefaultClientRenderable
{
public:
	C_BeamQuadratic();
	void Update(C_BaseEntity* owner);

	// IClientRenderable
	virtual const Vector& GetRenderOrigin(void) { return WorldPosition; }
	virtual const QAngle& GetRenderAngles(void) { return vec3_angle; }
	virtual bool ShouldDraw(void) { return true; }
	virtual bool IsTransparent(void) { return true; }
	virtual bool ShouldReceiveProjectedTextures(int flags) { return false; }
	virtual int DrawModel(int flags);
	virtual void DrawPhysgunBeamQuadratic(const Vector& start, const Vector& control, const Vector& end, float width, const Vector& color, float scrollOffset, float flHDRColorScale = 1.0f);

	//	virtual bool IgnoresZBuffer(void) const { return true; }
	virtual const matrix3x4_t& RenderableToWorldTransform()
	{
		static matrix3x4_t mat;
		SetIdentityMatrix(mat);
		PositionMatrix(GetRenderOrigin(), mat);
		return mat;
	}

	// Returns the bounds relative to the origin (render bounds)
	virtual void GetRenderBounds(Vector& mins, Vector& maxs)
	{
		// bogus. But it should draw if you can see the end point
		//	mins.Init(-32, -32, -32);
		//	maxs.Init(32, 32, 32);

		ClearBounds(mins, maxs);
		AddPointToBounds(WorldPosition, mins, maxs);
		AddPointToBounds(TargetPosition, mins, maxs);
		mins -= GetRenderOrigin();
		maxs -= GetRenderOrigin();
	}

	C_BaseEntity* Owner;
	Vector TargetPosition;
	Vector WorldPosition;
	int Active;
	int ViewModelIndex;
};


class C_WeaponPhysGun : public C_BaseCombatWeapon
{
	DECLARE_CLASS(C_WeaponPhysGun, C_BaseCombatWeapon);
public:
	C_WeaponPhysGun()
	{

	}

	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();

	int KeyInput(int down, ButtonCode_t keynum, const char* pszCurrentBinding)
	{
		if (gHUD.m_iKeyBits & IN_ATTACK)
		{
			switch (keynum)
			{
			case MOUSE_WHEEL_UP:
				gHUD.m_iKeyBits |= IN_WEAPON1;
				return 0;

			case MOUSE_WHEEL_DOWN:
				gHUD.m_iKeyBits |= IN_WEAPON2;
				return 0;
			}
		}

		// Allow engine to process
		return BaseClass::KeyInput(down, keynum, pszCurrentBinding);
	}

	void OnDataChanged(DataUpdateType_t updateType)
	{
		BaseClass::OnDataChanged(updateType);
		Beam.Update(this);
	}

private:
	C_WeaponPhysGun(const C_WeaponPhysGun&);
	C_BeamQuadratic Beam;
};