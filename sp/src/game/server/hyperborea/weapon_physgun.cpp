//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "beam_shared.h"
#include "player.h"
#include "gamerules.h"
#include "basecombatweapon.h"
#include "baseviewmodel.h"
#include "vphysics/constraints.h"
#include "physics.h"
#include "in_buttons.h"
#include "IEffects.h"
#include "engine/IEngineSound.h"
#include "ndebugoverlay.h"
#include "physics_saverestore.h"
#include "player_pickup.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar phys_gunmass("phys_gunmass", "200");
ConVar phys_gunvel("phys_gunvel", "800");
ConVar phys_gunforce("phys_gunforce", "500000");
ConVar phys_guntorque("phys_guntorque", "100");

class CWeaponPhysGun;
class CGravControllerPoint : public IMotionEvent
{
	DECLARE_SIMPLE_DATADESC();

public:
	CGravControllerPoint();
	~CGravControllerPoint();
	void AttachEntity(CBaseEntity* entity, IPhysicsObject* physicsObject, const Vector& position);
	void DetachEntity();
	void SetMaxVelocity(float value)
	{
		MaxVelocity = value;
	}

	void SetTargetPosition(const Vector& target)
	{
		TargetPosition = target;
		if (AttachedEntity == nullptr)
		{
			WorldPosition = target;
		}
		TimeToArrive = gpGlobals->frametime;
	}

	void SetAutoAlign(const Vector& localDir, const Vector& localPos, const Vector& worldAlignDir, const Vector& worldAlignPos)
	{
		Align = true;
		LocalAlignNormal = -localDir;
		LocalAlignPosition = localPos;
		TargetAlignNormal = worldAlignDir;
		TargetAlignPosition = worldAlignPos;
	}

	void ClearAutoAlign()
	{
		Align = false;
	}

	IMotionEvent::simresult_e Simulate(IPhysicsMotionController* controller, IPhysicsObject* object, float deltaTime, Vector& linear, AngularImpulse& angular);
	Vector LocalPosition;
	Vector TargetPosition;
	Vector WorldPosition;
	Vector LocalAlignNormal;
	Vector LocalAlignPosition;
	Vector TargetAlignNormal;
	Vector TargetAlignPosition;
	bool Align;
	float SaveDamping;
	float MaxVelocity;
	float MaxAcceleration;
	Vector MaxAngularAcceleration;
	EHANDLE AttachedEntity;
	QAngle TargetRotation;
	float TimeToArrive;

	IPhysicsMotionController* PhysicsMotionController;
};

BEGIN_SIMPLE_DATADESC(CGravControllerPoint)
DEFINE_FIELD(LocalPosition, FIELD_VECTOR),
DEFINE_FIELD(TargetPosition, FIELD_POSITION_VECTOR),
DEFINE_FIELD(WorldPosition, FIELD_POSITION_VECTOR),
DEFINE_FIELD(LocalAlignNormal, FIELD_VECTOR),
DEFINE_FIELD(LocalAlignPosition, FIELD_VECTOR),
DEFINE_FIELD(TargetAlignNormal, FIELD_VECTOR),
DEFINE_FIELD(TargetAlignPosition, FIELD_POSITION_VECTOR),
DEFINE_FIELD(Align, FIELD_BOOLEAN),
DEFINE_FIELD(SaveDamping, FIELD_FLOAT),
DEFINE_FIELD(MaxVelocity, FIELD_FLOAT),
DEFINE_FIELD(MaxAcceleration, FIELD_FLOAT),
DEFINE_FIELD(MaxAngularAcceleration, FIELD_VECTOR),
DEFINE_FIELD(AttachedEntity, FIELD_EHANDLE),
DEFINE_FIELD(TargetRotation, FIELD_VECTOR),
DEFINE_FIELD(TimeToArrive, FIELD_FLOAT),

// Physptrs can't be saved in embedded classes... this is to silence classcheck
// DEFINE_PHYSPTR(PhysicsMotionController),
END_DATADESC()

CGravControllerPoint::CGravControllerPoint()
{
	AttachedEntity = nullptr;
}

CGravControllerPoint::~CGravControllerPoint()
{
	DetachEntity();
}

void CGravControllerPoint::AttachEntity(CBaseEntity* entity, IPhysicsObject* physicsObject, const Vector& position)
{
	AttachedEntity = entity;
	physicsObject->WorldToLocal(&LocalPosition, position);
	WorldPosition = position;
	physicsObject->GetDamping(NULL, &SaveDamping);
	float Damping = 2;
	physicsObject->SetDamping(NULL, &Damping);

	PhysicsMotionController = physenv->CreateMotionController(this);
	PhysicsMotionController->AttachObject(physicsObject, true);
	PhysicsMotionController->SetPriority(IPhysicsMotionController::HIGH_PRIORITY);

	SetTargetPosition(position);
	MaxAcceleration = phys_gunforce.GetFloat() * physicsObject->GetInvMass();
	TargetRotation = entity->GetAbsAngles();

	float Torque = phys_guntorque.GetFloat();
	MaxAngularAcceleration = Torque * physicsObject->GetInvInertia();
}

void CGravControllerPoint::DetachEntity()
{
	CBaseEntity* CurrentEntity = AttachedEntity;
	if (CurrentEntity)
	{
		IPhysicsObject* CurrentPhysicsObject = CurrentEntity->VPhysicsGetObject();
		if (CurrentPhysicsObject)
		{
			// on the odd chance that it's gone to sleep while under anti-gravity
			CurrentPhysicsObject->Wake();
			CurrentPhysicsObject->SetDamping(NULL, &SaveDamping);
		}
	}
	AttachedEntity = nullptr;
	physenv->DestroyMotionController(PhysicsMotionController);
	PhysicsMotionController = nullptr;

	// UNDONE: Does this help the networking?
	TargetPosition = vec3_origin;
	WorldPosition = vec3_origin;
}

void AxisAngleQAngle(const Vector& axis, float angle, QAngle& outAngles)
{
	// map back to HL rotation axes
	outAngles.z = axis.x * angle;
	outAngles.x = axis.y * angle;
	outAngles.y = axis.z * angle;
}

IMotionEvent::simresult_e CGravControllerPoint::Simulate(IPhysicsMotionController* controller, IPhysicsObject* object, float deltaTime, Vector& linear, AngularImpulse& angular)
{
	Vector Velocity;
	AngularImpulse AngularVelocity;

	float FracRemainingSimTime = 1.0;
	if (TimeToArrive > 0)
	{
		FracRemainingSimTime *= deltaTime / TimeToArrive;
		if (FracRemainingSimTime > 1)
		{
			FracRemainingSimTime = 1;
		}
	}

	TimeToArrive -= deltaTime;
	if (TimeToArrive < 0)
	{
		TimeToArrive = 0;
	}

	float InvDeltaTime = (1.0f / deltaTime);
	Vector World;
	object->LocalToWorld(&World, LocalPosition);
	WorldPosition = World;
	object->GetVelocity(&Velocity, &AngularVelocity);
	//pObject->GetVelocityAtPoint( world, &vel );
	float damping = 1.0;
	World += Velocity * deltaTime * damping;
	Vector Delta = (TargetPosition - World) * FracRemainingSimTime * InvDeltaTime;
	Vector AlignDir;
	linear = vec3_origin;
	angular = vec3_origin;

	if (Align)
	{
		QAngle Angles;
		Vector Origin;
		Vector Axis;
		AngularImpulse torque;

		object->GetShadowPosition(&Origin, &Angles);
		// align local normal to target normal
		VMatrix tmp = SetupMatrixOrgAngles(Origin, Angles);
		Vector worldNormal = tmp.VMul3x3(LocalAlignNormal);
		Axis = CrossProduct(worldNormal, TargetAlignNormal);
		float trig = VectorNormalize(Axis);
		float alignRotation = RAD2DEG(asin(trig));
		Axis *= alignRotation;
		if (alignRotation < 10)
		{
			float dot = DotProduct(worldNormal, TargetAlignNormal);
			// probably 180 degrees off
			if (dot < 0)
			{
				if (worldNormal.x < 0.5)
				{
					Axis.Init(10, 0, 0);
				}
				else
				{
					Axis.Init(0, 0, 10);
				}
				alignRotation = 10;
			}
		}

		// Solve for the rotation around the target normal (at the local align pos) that will 
		// move the grabbed spot to the destination.
		Vector worldRotCenter = tmp.VMul4x3(LocalAlignPosition);
		Vector rotSrc = World - worldRotCenter;
		Vector rotDest = TargetPosition - worldRotCenter;

		// Get a basis in the plane perpendicular to TargetAlignNormal
		Vector srcN = rotSrc;
		VectorNormalize(srcN);
		Vector tangent = CrossProduct(srcN, TargetAlignNormal);
		float len = VectorNormalize(tangent);

		// needs at least ~5 degrees, or forget rotation (0.08 ~= sin(5))
		if (len > 0.08)
		{
			Vector binormal = CrossProduct(TargetAlignNormal, tangent);

			// Now project the src & dest positions into that plane
			Vector planeSrc(DotProduct(rotSrc, tangent), DotProduct(rotSrc, binormal), 0);
			Vector planeDest(DotProduct(rotDest, tangent), DotProduct(rotDest, binormal), 0);

			float rotRadius = VectorNormalize(planeSrc);
			float destRadius = VectorNormalize(planeDest);
			if (rotRadius > 0.1)
			{
				if (destRadius < rotRadius)
				{
					destRadius = rotRadius;
				}
				//float ratio = rotRadius / destRadius;
				float angleSrc = atan2(planeSrc.y, planeSrc.x);
				float angleDest = atan2(planeDest.y, planeDest.x);
				float angleDiff = angleDest - angleSrc;
				angleDiff = RAD2DEG(angleDiff);
				Axis += TargetAlignNormal * angleDiff;
				World = TargetPosition;// + rotDest * (1-ratio);
				//				NDebugOverlay::Line( worldRotCenter, worldRotCenter-TargetAlignNormal*50, 255, 0, 0, false, 0.1 );
				//				NDebugOverlay::Line( worldRotCenter, worldRotCenter+tangent*50, 0, 255, 0, false, 0.1 );
				//				NDebugOverlay::Line( worldRotCenter, worldRotCenter+binormal*50, 0, 0, 255, false, 0.1 );
			}
		}

		torque = WorldToLocalRotation(tmp, Axis, 1);
		torque *= FracRemainingSimTime * InvDeltaTime;
		torque -= AngularVelocity * 1.0;	 // damping
		for (int i = 0; i < 3; i++)
		{
			if (torque[i] > 0)
			{
				if (torque[i] > MaxAngularAcceleration[i])
					torque[i] = MaxAngularAcceleration[i];
			}
			else
			{
				if (torque[i] < -MaxAngularAcceleration[i])
					torque[i] = -MaxAngularAcceleration[i];
			}
		}
		torque *= InvDeltaTime;
		angular += torque;
		// Calculate an acceleration that pulls the object toward the constraint
		// When you're out of alignment, don't pull very hard
		float factor = fabsf(alignRotation);
		if (factor < 5)
		{
			factor = clamp(factor, 0, 5) * (1 / 5);
			AlignDir = TargetAlignPosition - worldRotCenter;
			// Limit movement to the part along TargetAlignNormal if worldRotCenter is on the backside of 
			// of the target plane (one inch epsilon)!
			float planeForward = DotProduct(AlignDir, TargetAlignNormal);
			if (planeForward > 1)
			{
				AlignDir = TargetAlignNormal * planeForward;
			}
			Vector accel = AlignDir * InvDeltaTime * FracRemainingSimTime * (1 - factor) * 0.20 * InvDeltaTime;
			float mag = accel.Length();
			if (mag > MaxAcceleration)
			{
				accel *= (MaxAcceleration / mag);
			}
			linear += accel;
		}
		linear -= Velocity * damping * InvDeltaTime;
		// UNDONE: Factor in the change in worldRotCenter due to applied torque!
	}
	else
	{
		// clamp future velocity to max speed
		Vector nextVel = Delta + Velocity;
		float nextSpeed = nextVel.Length();
		if (nextSpeed > MaxVelocity)
		{
			nextVel *= (MaxVelocity / nextSpeed);
			Delta = nextVel - Velocity;
		}

		Delta *= InvDeltaTime;

		float linearAccel = Delta.Length();
		if (linearAccel > MaxAcceleration)
		{
			Delta *= MaxAcceleration / linearAccel;
		}

		Vector accel;
		AngularImpulse angAccel;
		object->CalculateForceOffset(Delta, World, &accel, &angAccel);

		linear += accel;
		angular += angAccel;
	}

	return SIM_GLOBAL_ACCELERATION;
}

class CWeaponPhysGun : public CBaseCombatWeapon
{
	DECLARE_DATADESC();

public:
	DECLARE_CLASS(CWeaponPhysGun, CBaseCombatWeapon);

	CWeaponPhysGun();
	void Spawn();
	void OnRestore();
	void Precache();

	void PrimaryAttack();
	void WeaponIdle();
	void ItemPostFrame();
	virtual bool Holster(CBaseCombatWeapon* switchingTo)
	{
		EffectDestroy();
		return BaseClass::Holster(switchingTo);
	}

	bool HasAnyAmmo();

	void AttachObject(CBaseEntity* object, const Vector& start, const Vector& end, float distance);
	void DetachObject();

	void EffectCreate();
	void EffectUpdate();
	void EffectDestroy();

	void SoundCreate();
	void SoundDestroy();
	void SoundStop();
	void SoundStart();
	void SoundUpdate();

	int ObjectCaps()
	{
		int caps = BaseClass::ObjectCaps();
		if (Active)
		{
			caps |= FCAP_DIRECTIONAL_USE;
		}
		return caps;
	}

	CBaseEntity* GetBeamEntity();

	DECLARE_SERVERCLASS();

private:
	CNetworkVar(int, Active);
	bool UseDown;
	EHANDLE Object;
	float Distance;
	float MovementLength;
	float LastYaw;
	int SoundState;
	CNetworkVar(int, ViewModelIndex);
	Vector OriginalObjectPosition;

	CGravControllerPoint GravCallback;
};

IMPLEMENT_SERVERCLASS_ST(CWeaponPhysGun, DT_WeaponPhysGun)
SendPropVector(SENDINFO_NAME(GravCallback.TargetPosition, TargetPosition), -1, SPROP_COORD),
SendPropVector(SENDINFO_NAME(GravCallback.WorldPosition, WorldPosition), -1, SPROP_COORD),
SendPropInt(SENDINFO(Active), 1, SPROP_UNSIGNED),
SendPropModelIndex(SENDINFO(ViewModelIndex)),
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS(weapon_physgun, CWeaponPhysGun);
PRECACHE_WEAPON_REGISTER(weapon_physgun);

BEGIN_DATADESC(CWeaponPhysGun)
DEFINE_FIELD(Active, FIELD_INTEGER),
DEFINE_FIELD(UseDown, FIELD_BOOLEAN),
DEFINE_FIELD(Object, FIELD_EHANDLE),
DEFINE_FIELD(Distance, FIELD_FLOAT),
DEFINE_FIELD(MovementLength, FIELD_FLOAT),
DEFINE_FIELD(LastYaw, FIELD_FLOAT),
DEFINE_FIELD(SoundState, FIELD_INTEGER),
DEFINE_FIELD(ViewModelIndex, FIELD_INTEGER),
DEFINE_FIELD(OriginalObjectPosition, FIELD_POSITION_VECTOR),
DEFINE_EMBEDDED(GravCallback),
// Physptrs can't be saved in embedded classes..
DEFINE_PHYSPTR(GravCallback.PhysicsMotionController),
END_DATADESC()

enum physgun_soundstate { SS_SCANNING, SS_LOCKEDON };
enum physgun_soundIndex { SI_LOCKEDON = 0, SI_SCANNING = 1, SI_LIGHTOBJECT = 2, SI_HEAVYOBJECT = 3, SI_ON, SI_OFF };

CWeaponPhysGun::CWeaponPhysGun()
{
	Active = false;
	m_bFiresUnderwater = true;
}

void CWeaponPhysGun::Spawn()
{
	BaseClass::Spawn();
	//	SetModel(GetWorldModel());
	FallInit();
}

void CWeaponPhysGun::OnRestore()
{
	BaseClass::OnRestore();

	if (GravCallback.PhysicsMotionController)
	{
		GravCallback.PhysicsMotionController->SetEventHandler(&GravCallback);
	}
}

void CWeaponPhysGun::Precache()
{
	BaseClass::Precache();

	PrecacheScriptSound("Weapon_Physgun.Scanning");
	PrecacheScriptSound("Weapon_Physgun.LockedOn");
	PrecacheScriptSound("Weapon_Physgun.Scanning");
	PrecacheScriptSound("Weapon_Physgun.LightObject");
	PrecacheScriptSound("Weapon_Physgun.HeavyObject");
}

void CWeaponPhysGun::EffectCreate()
{
	EffectUpdate();
	Active = true;
}

void CWeaponPhysGun::EffectUpdate()
{
	Vector Start, Angles, Forward, Right;
	trace_t Trace;

	CBasePlayer* CurrentOwner = ToBasePlayer(GetOwner());
	if (CurrentOwner == nullptr)
		return;

	ViewModelIndex = CurrentOwner->entindex();
	// Make sure I've got a view model
	CBaseViewModel* CurrentViewmodel = CurrentOwner->GetViewModel();
	if (CurrentViewmodel != nullptr)
	{
		ViewModelIndex = CurrentViewmodel->entindex();
	}

	CurrentOwner->EyeVectors(&Forward, &Right, NULL);

	Start = CurrentOwner->Weapon_ShootPosition();
	Vector End = Start + Forward * 4096;

	UTIL_TraceLine(Start, End, MASK_SHOT, CurrentOwner, COLLISION_GROUP_NONE, &Trace);
	End = Trace.endpos;
	float TraceDistance = Trace.fraction * 4096;
	if (Trace.fraction != 1)
	{
		// too close to the player, drop the object
		if (TraceDistance < 36)
		{
			DetachObject();
			return;
		}
	}

	if (Object == NULL && Trace.DidHitNonWorldEntity())
	{
		CBaseEntity* CurrentEntity = Trace.m_pEnt;
		// inform the object what was hit
		ClearMultiDamage();
		CurrentEntity->DispatchTraceAttack(CTakeDamageInfo(CurrentOwner, CurrentOwner, 0, DMG_PHYSGUN), Forward, &Trace);
		ApplyMultiDamage();
		AttachObject(CurrentEntity, Start, Trace.endpos, TraceDistance);
		LastYaw = CurrentOwner->EyeAngles().y;
	}

	// Add the incremental player yaw to the target transform
	matrix3x4_t CurMatrix, IncMatrix, NextMatrix;
	AngleMatrix(GravCallback.TargetRotation, CurMatrix);
	AngleMatrix(QAngle(0, CurrentOwner->EyeAngles().y - LastYaw, 0), IncMatrix);
	ConcatTransforms(IncMatrix, CurMatrix, NextMatrix);
	MatrixAngles(NextMatrix, GravCallback.TargetRotation);
	LastYaw = CurrentOwner->EyeAngles().y;

	CBaseEntity* CurrentObject = Object;
	if (CurrentObject != nullptr)
	{
		if (UseDown)
		{
			if (CurrentOwner->m_afButtonPressed & IN_USE)
			{
				UseDown = false;
			}
		}
		else
		{
			if (CurrentOwner->m_afButtonPressed & IN_USE)
			{
				UseDown = true;
			}
		}

		if (UseDown)
		{
			CurrentOwner->SetPhysicsFlag(PFLAG_DIROVERRIDE, true);
			if (CurrentOwner->m_nButtons & IN_FORWARD)
			{
				Distance = UTIL_Approach(1024, Distance, gpGlobals->frametime * 100);
			}
			if (CurrentOwner->m_nButtons & IN_BACK)
			{
				Distance = UTIL_Approach(40, Distance, gpGlobals->frametime * 100);
			}
		}

		if (CurrentOwner->m_nButtons & IN_WEAPON1)
		{
			Distance = UTIL_Approach(1024, Distance, Distance * 0.1);
		}
		if (CurrentOwner->m_nButtons & IN_WEAPON2)
		{
			Distance = UTIL_Approach(40, Distance, Distance * 0.1);
		}

		// Send the object a physics damage message (0 damage). Some objects interpret this 
		// as something else being in control of their physics temporarily.
		CurrentObject->TakeDamage(CTakeDamageInfo(this, CurrentOwner, 0, DMG_PHYSGUN));

		Vector NewPosition = Start + Forward * Distance;
		// 24 is a little larger than 16 * sqrt(2) (extent of player bbox)
		// HACKHACK: We do this so we can "ignore" the player and the object we're manipulating
		// If we had a filter for tracelines, we could simply filter both ents and start from "start"
		Vector AwayfromPlayer = Start + Forward * 24;

		UTIL_TraceLine(Start, AwayfromPlayer, MASK_SOLID, CurrentOwner, COLLISION_GROUP_NONE, &Trace);
		if (Trace.fraction == 1)
		{
			UTIL_TraceLine(AwayfromPlayer, NewPosition, MASK_SOLID, CurrentObject, COLLISION_GROUP_NONE, &Trace);
			Vector Dir = Trace.endpos - NewPosition;
			float TraceDistance = VectorNormalize(Dir);
			float MaxDist = GravCallback.MaxVelocity * gpGlobals->frametime;
			if (TraceDistance >  MaxDist)
			{
				NewPosition += Dir * MaxDist;
			}
			else
			{
				NewPosition = Trace.endpos;
			}
		}
		else
		{
			NewPosition = Trace.endpos;
		}

		GravCallback.SetTargetPosition(NewPosition);
		Vector Dir = (NewPosition - CurrentObject->GetLocalOrigin());
		MovementLength = Dir.Length();
	}
	else
	{
		GravCallback.SetTargetPosition(End);
	}
	
	GravCallback.ClearAutoAlign();

	NetworkStateChanged();
}

void CWeaponPhysGun::SoundCreate()
{
	SoundState = SS_SCANNING;
	SoundStart();
}

void CWeaponPhysGun::SoundDestroy()
{
	SoundStop();
}

void CWeaponPhysGun::SoundStop()
{
	if (SoundState == SS_SCANNING)
	{
		GetOwner()->StopSound("Weapon_Physgun.Scanning");
	}
	else if (SoundState == SS_LOCKEDON)
	{
		GetOwner()->StopSound("Weapon_Physgun.Scanning");
		GetOwner()->StopSound("Weapon_Physgun.LockedOn");
		GetOwner()->StopSound("Weapon_Physgun.LightObject");
		GetOwner()->StopSound("Weapon_Physgun.HeavyObject");
	}
}

//-----------------------------------------------------------------------------
// Purpose: returns the linear fraction of value between low & high (0.0 - 1.0) * scale
//			e.g. UTIL_LineFraction( 1.5, 1, 2, 1 ); will return 0.5 since 1.5 is
//			halfway between 1 and 2
// Input  : value - a value between low & high (clamped)
//			low - the value that maps to zero
//			high - the value that maps to "scale"
//			scale - the output scale
// Output : parametric fraction between low & high
//-----------------------------------------------------------------------------
static float UTIL_LineFraction(float value, float low, float high, float scale)
{
	if (value < low)
		value = low;
	if (value > high)
		value = high;

	float Delta = high - low;
	if (Delta == 0)
		return 0;

	return scale * (value - low) / Delta;
}

void CWeaponPhysGun::SoundStart()
{
	CPASAttenuationFilter Filter(GetOwner());
	Filter.MakeReliable();

	if (SoundState == SS_SCANNING)
	{
		EmitSound(Filter, GetOwner()->entindex(), "Weapon_Physgun.Scanning");
	}
	else if (SoundState == SS_LOCKEDON)
	{
		EmitSound(Filter, GetOwner()->entindex(), "Weapon_Physgun.LockedOn");
		EmitSound(Filter, GetOwner()->entindex(), "Weapon_Physgun.Scanning");
		EmitSound(Filter, GetOwner()->entindex(), "Weapon_Physgun.LightObject");
		EmitSound(Filter, GetOwner()->entindex(), "Weapon_Physgun.HeavyObject");
	}
}

void CWeaponPhysGun::SoundUpdate()
{
	int NewState = SS_SCANNING;
	if (Object)
		NewState = SS_LOCKEDON;

	if (NewState != SoundState)
	{
		SoundStop();
		SoundState = NewState;
		SoundStart();
	}

	if (SoundState == SS_LOCKEDON)
	{
		CPASAttenuationFilter Filter(GetOwner());
		Filter.MakeReliable();

		float Height = Object->GetAbsOrigin().z - OriginalObjectPosition.z;

		// go from pitch 90 to 150 over a height of 500
		int Pitch = 90 + (int)UTIL_LineFraction(Height, 0, 500, 60);

		CSoundParameters SoundParameters;
		if (GetParametersForSound("Weapon_Physgun.LockedOn", SoundParameters, NULL))
		{
			EmitSound_t EmitParameters(SoundParameters);
			EmitParameters.m_nFlags = SND_CHANGE_VOL | SND_CHANGE_PITCH;
			EmitParameters.m_nPitch = Pitch;

			EmitSound(Filter, GetOwner()->entindex(), EmitParameters);
		}

		// attenutate the movement sounds over 200 units of movement
		float SoundDistance = UTIL_LineFraction(MovementLength, 0, 200, 1.0);

		// blend the "mass" sounds between 50 and 500 kg
		IPhysicsObject* CurrentPhysicsObject = Object->VPhysicsGetObject();

		float Fade = UTIL_LineFraction(CurrentPhysicsObject->GetMass(), 50, 500, 1.0);

		if (GetParametersForSound("Weapon_Physgun.LightObject", SoundParameters, NULL))
		{
			EmitSound_t EmitParameters(SoundParameters);
			EmitParameters.m_nFlags = SND_CHANGE_VOL;
			EmitParameters.m_flVolume = Fade * SoundDistance;

			EmitSound(Filter, GetOwner()->entindex(), EmitParameters);
		}

		if (GetParametersForSound("Weapon_Physgun.HeavyObject", SoundParameters, NULL))
		{
			EmitSound_t EmitParameters(SoundParameters);
			EmitParameters.m_nFlags = SND_CHANGE_VOL;
			EmitParameters.m_flVolume = (1.0 - Fade) * SoundDistance;

			EmitSound(Filter, GetOwner()->entindex(), EmitParameters);
		}
	}
}

CBaseEntity* CWeaponPhysGun::GetBeamEntity()
{
	CBasePlayer* CurrentOwner = ToBasePlayer(GetOwner());
	if (CurrentOwner == nullptr)
		return nullptr;

	CBaseViewModel* CurrentViewmodel = CurrentOwner->GetViewModel();
	if (CurrentViewmodel != nullptr)
		return CurrentViewmodel;

	return CurrentOwner;
}

void CWeaponPhysGun::EffectDestroy()
{
	Active = false;
	SoundStop();
	DetachObject();
}

void CWeaponPhysGun::DetachObject()
{
	if (Object != nullptr)
	{
		CBasePlayer* CurrentOwner = ToBasePlayer(GetOwner());
		Pickup_OnPhysGunDrop(Object, CurrentOwner, DROPPED_BY_CANNON);

		GravCallback.DetachEntity();
		Object = nullptr;
	}
}

void CWeaponPhysGun::AttachObject(CBaseEntity* object, const Vector& start, const Vector& end, float distance)
{
	Object = object;
	UseDown = false;
	IPhysicsObject* CurrentPhysicsObject = object ? (object->VPhysicsGetObject()) : nullptr;
	if (CurrentPhysicsObject != nullptr && object->GetMoveType() == MOVETYPE_VPHYSICS)
	{
		Distance = distance;

		GravCallback.AttachEntity(object, CurrentPhysicsObject, end);
		float Mass = CurrentPhysicsObject->GetMass();
		float Velocity = phys_gunvel.GetFloat();
		if (Mass > phys_gunmass.GetFloat())
		{
			Velocity = (Velocity * phys_gunmass.GetFloat()) / Mass;
		}
		GravCallback.SetMaxVelocity(Velocity);

		OriginalObjectPosition = object->GetAbsOrigin();

		CurrentPhysicsObject->Wake();

		CBasePlayer* CurrentOwner = ToBasePlayer(GetOwner());
		if (CurrentOwner != nullptr)
		{
			Pickup_OnPhysGunPickup(object, CurrentOwner);
		}
	}
	else
	{
		Object = nullptr;
	}
}

void CWeaponPhysGun::PrimaryAttack()
{
	if (!Active)
	{
		SendWeaponAnim(ACT_VM_PRIMARYATTACK);
		EffectCreate();
		SoundCreate();
	}
	else
	{
		EffectUpdate();
		SoundUpdate();
	}
}

void CWeaponPhysGun::WeaponIdle()
{
	SendWeaponAnim(ACT_VM_IDLE);
	if (Active)
	{
		EffectDestroy();
		SoundDestroy();
	}
}

void CWeaponPhysGun::ItemPostFrame()
{
	CBasePlayer* CurrentOwner = ToBasePlayer(GetOwner());
	if (CurrentOwner == nullptr)
		return;

	if (CurrentOwner->m_nButtons & IN_ATTACK)
	{
		PrimaryAttack();
	}
	else
	{
		WeaponIdle();
		return;
	}
}

bool CWeaponPhysGun::HasAnyAmmo()
{
	return true;
}