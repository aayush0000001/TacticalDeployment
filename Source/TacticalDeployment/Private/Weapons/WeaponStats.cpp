// Copyright TacticalDeployment. All Rights Reserved.

#include "Weapons/WeaponStats.h"
#include "Curves/CurveVector.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "WeaponStats"

// ---------------------------------------------------------------------------------------
// FWeaponSprayState
// ---------------------------------------------------------------------------------------

void FWeaponSprayState::Recover(double Now, const UWeaponStats& Stats)
{
	// Time spent *not* firing: anything beyond one fire interval since the last shot.
	const float Idle = static_cast<float>(Now - LastShotTime) - Stats.GetFireInterval();
	if (Idle <= 0.f)
	{
		return;
	}

	FiringError = FMath::Max(0.f, FiringError - Idle * Stats.FiringErrorRecoveryRate);

	const float RecoveringFor = Idle - Stats.RecoilRecoveryDelay;
	if (RecoveringFor > 0.f)
	{
		SprayProgress = FMath::Max(0.f, SprayProgress - RecoveringFor * Stats.RecoilRecoveryRate);
	}
}

void FWeaponSprayState::CommitShot(double Now, const UWeaponStats& Stats)
{
	if (GetShotIndex() >= Stats.FiringErrorGraceShots)
	{
		FiringError = FMath::Min(Stats.MaxFiringError, FiringError + Stats.FiringErrorPerShot);
	}
	SprayProgress += 1.f;
	LastShotTime = Now;
}

// ---------------------------------------------------------------------------------------
// UWeaponStats
// ---------------------------------------------------------------------------------------

float UWeaponStats::GetZoneMultiplier(EHitZone Zone) const
{
	switch (Zone)
	{
	case EHitZone::Head: return HeadMultiplier;
	case EHitZone::Arms: return ArmMultiplier;
	case EHitZone::Legs: return LegMultiplier;
	case EHitZone::Body: return 1.f;
	default:             return 0.f;
	}
}

float UWeaponStats::GetRangeMultiplier(float DistanceCm) const
{
	for (const FDamageRangeBracket& Bracket : RangeBrackets)
	{
		if (DistanceCm <= Bracket.MaxDistance)
		{
			return Bracket.DamageMultiplier;
		}
	}
	return RangeBrackets.Num() > 0 ? RangeBrackets.Last().DamageMultiplier : 1.f;
}

FVector2D UWeaponStats::EvaluateRecoil(int32 ShotIndex) const
{
	if (!RecoilPattern)
	{
		return FVector2D::ZeroVector;
	}

	// Past the authored pattern, hold the last key: sustained spray settles into the final
	// offset (horizontal sway should be authored as keys, not randomness).
	float MinTime = 0.f;
	float MaxTime = 0.f;
	RecoilPattern->GetTimeRange(MinTime, MaxTime);
	const float Key = FMath::Clamp(static_cast<float>(ShotIndex), MinTime, MaxTime);

	const FVector Value = RecoilPattern->GetVectorValue(Key);
	return FVector2D(Value.X, Value.Y);
}

ETacticalMovementState UWeaponStats::ClassifyMovement(const FWeaponSpreadInputs& In, float InAccurateSpeedFraction)
{
	if (In.bIsAirborne)
	{
		return ETacticalMovementState::Airborne;
	}

	const float SpeedFraction = In.MaxRunSpeed > KINDA_SMALL_NUMBER ? In.HorizontalSpeed / In.MaxRunSpeed : 0.f;
	if (SpeedFraction <= InAccurateSpeedFraction)
	{
		return ETacticalMovementState::Stationary;
	}

	const float WalkFraction = In.MaxRunSpeed > KINDA_SMALL_NUMBER ? In.ShiftWalkSpeed / In.MaxRunSpeed : 1.f;
	return SpeedFraction <= WalkFraction + KINDA_SMALL_NUMBER ? ETacticalMovementState::Walking : ETacticalMovementState::Running;
}

float UWeaponStats::ComputeSpread(const FWeaponSpreadInputs& In) const
{
	// Spread keys off *measured velocity*, not input keys. This is what makes counter-strafing
	// a skill: tapping the opposite key kills velocity in a few ticks (see the CMC's friction
	// tuning), crossing AccurateSpeedFraction long before simply releasing the key would.
	float MovementPenalty = 0.f;

	if (In.bIsAirborne)
	{
		MovementPenalty = AirborneSpread;
	}
	else if (In.MaxRunSpeed > KINDA_SMALL_NUMBER)
	{
		const float SpeedFraction = In.HorizontalSpeed / In.MaxRunSpeed;
		const float WalkFraction = FMath::Max(In.ShiftWalkSpeed / In.MaxRunSpeed, AccurateSpeedFraction + KINDA_SMALL_NUMBER);

		if (SpeedFraction > AccurateSpeedFraction)
		{
			MovementPenalty = SpeedFraction <= WalkFraction
				? FMath::GetMappedRangeValueClamped(FVector2f(AccurateSpeedFraction, WalkFraction), FVector2f(0.f, WalkSpread), SpeedFraction)
				: FMath::GetMappedRangeValueClamped(FVector2f(WalkFraction, 1.f), FVector2f(WalkSpread, RunSpread), SpeedFraction);
		}
	}

	float Spread = BaseSpread + MovementPenalty + In.FiringError;
	if (In.bIsCrouched && !In.bIsAirborne)
	{
		Spread *= CrouchSpreadMultiplier;
	}
	return Spread;
}

const FPenetrationTierParams& UWeaponStats::GetPenetrationParams(EWallPenetrationTier Tier)
{
	// Power is in density-cm: 20 cm of drywall (0.4) costs 8, 10 cm of concrete (2.0) costs 20.
	static const FPenetrationTierParams Low    { 12.f, 20.f, 1 };
	static const FPenetrationTierParams Medium { 25.f, 40.f, 2 };
	static const FPenetrationTierParams High   { 50.f, 80.f, 3 };

	switch (Tier)
	{
	case EWallPenetrationTier::High:   return High;
	case EWallPenetrationTier::Medium: return Medium;
	default:                           return Low;
	}
}

FVector UWeaponStats::ApplyRecoilAndSpread(const FRotator& AimRotation, const FVector2D& RecoilDegrees, float SpreadHalfAngleDegrees, FRandomStream& Rng)
{
	FRotator Punched = AimRotation;
	Punched.Yaw += RecoilDegrees.X;
	Punched.Pitch = FMath::ClampAngle(Punched.Pitch + RecoilDegrees.Y, -89.f, 89.f);

	const FVector Forward = Punched.Vector();
	if (SpreadHalfAngleDegrees <= KINDA_SMALL_NUMBER)
	{
		return Forward;
	}
	return Rng.VRandCone(Forward, FMath::DegreesToRadians(SpreadHalfAngleDegrees));
}

FPrimaryAssetId UWeaponStats::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("WeaponStats"), GetFName());
}

#if WITH_EDITOR
EDataValidationResult UWeaponStats::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (!WeaponClass)
	{
		Context.AddError(LOCTEXT("MissingClass", "WeaponClass must be set."));
		Result = EDataValidationResult::Invalid;
	}
	for (int32 i = 1; i < RangeBrackets.Num(); ++i)
	{
		if (RangeBrackets[i].MaxDistance < RangeBrackets[i - 1].MaxDistance)
		{
			Context.AddError(LOCTEXT("UnsortedBrackets", "RangeBrackets must be sorted by ascending MaxDistance."));
			Result = EDataValidationResult::Invalid;
			break;
		}
	}
	if (RecoilPattern && !RecoilPattern->GetVectorValue(0.f).IsNearlyZero(0.01f))
	{
		Context.AddWarning(LOCTEXT("FirstShotRecoil", "RecoilPattern key 0 is not (0,0): the first bullet will not hit the crosshair."));
	}
	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
