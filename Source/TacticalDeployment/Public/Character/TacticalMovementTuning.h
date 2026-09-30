// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

// Single source of truth for ground-movement tuning and the max-speed rule. Read by
// UTacticalCharacterMovementComponent and by the movement model in Tools/Simulation.

#include "CoreMinimal.h"

namespace TacticalMovementTuning
{
	inline constexpr float RunSpeed = 675.f;       // cm/s (MaxWalkSpeed)
	inline constexpr float ShiftWalkSpeed = 405.f; // Silent walk (60%).
	inline constexpr float CrouchSpeed = 230.f;

	// Snappy ground model. With input opposing velocity, CalcVelocity bends velocity toward the
	// input direction at rate GroundFriction and then adds MaxAcceleration, so a counter-strafe
	// sheds speed several times faster than releasing the key (which only applies braking).
	inline constexpr float MaxAcceleration = 5200.f;
	inline constexpr float GroundFriction = 12.f;
	inline constexpr float BrakingDeceleration = 3400.f;
	inline constexpr float BrakingFriction = 4.f;
	inline constexpr float BrakingFrictionFactor = 1.f;
	/** The engine clamps BrakingSubStepTime to [1/75, 1/20]; 1/75 is the finest it allows. */
	inline constexpr float BrakingSubStepTime = 1.f / 75.f;

	inline constexpr float JumpZVelocity = 440.f;
	inline constexpr float GravityScale = 1.25f;
	inline constexpr float AirControl = 0.25f;

	struct FMaxSpeedInputs
	{
		float BaseMaxSpeed = RunSpeed; // What the CMC reports for the current mode (crouch included).
		bool bOnGround = true;
		bool bCrouching = false;
		bool bWantsShiftWalk = false;
		bool bInteractLocked = false;  // Planting / defusing.
		bool bPhaseLocked = false;     // PreMatch / BuyPhase.
		float WeaponMultiplier = 1.f;
		float TaggingScalar = 1.f;
	};

	inline float ComputeMaxSpeed(const FMaxSpeedInputs& In)
	{
		if (In.bPhaseLocked || (In.bInteractLocked && In.bOnGround))
		{
			return 0.f;
		}
		float Speed = In.BaseMaxSpeed;
		if (In.bOnGround && In.bWantsShiftWalk && !In.bCrouching)
		{
			Speed = FMath::Min(Speed, ShiftWalkSpeed);
		}
		return Speed * In.WeaponMultiplier * In.TaggingScalar;
	}
}
