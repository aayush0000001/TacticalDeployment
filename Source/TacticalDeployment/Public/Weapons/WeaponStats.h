// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/TacticalTypes.h"
#include "WeaponStats.generated.h"

class ATacticalWeapon;
class UCurveVector;

/** Valorant-style stepped damage falloff: first bracket whose MaxDistance >= distance wins. */
USTRUCT(BlueprintType)
struct FDamageRangeBracket
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (Units = "cm"))
	float MaxDistance = 3000.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (ClampMin = "0"))
	float DamageMultiplier = 1.f;
};

/** Per-tier wall penetration budget. Power is spent as (thickness_cm * material density). */
struct FPenetrationTierParams
{
	float Power;         // Total density-cm the round can pass through.
	float MaxThickness;  // Longest single-surface probe, cm.
	int32 MaxSurfaces;   // Hard cap on surfaces traversed.
};

/** Everything the deterministic spread model needs. Built from authoritative state on the server. */
struct FWeaponSpreadInputs
{
	float HorizontalSpeed = 0.f;
	float MaxRunSpeed = 675.f;
	float ShiftWalkSpeed = 405.f;
	float FiringError = 0.f;
	bool bIsAirborne = false;
	bool bIsCrouched = false;
};

/**
 * Deterministic spray bookkeeping. Pure function of (shot timestamps, stats), so the client's
 * cosmetic prediction and the server's authoritative evaluation produce the same bullet index
 * for the same cadence. Only the server's result ever deals damage.
 */
struct TACTICALDEPLOYMENT_API FWeaponSprayState
{
	double LastShotTime = -1.0e9;
	float SprayProgress = 0.f; // Fractional bullet index into the recoil pattern.
	float FiringError = 0.f;   // Degrees of extra spread from sustained fire.

	/** Decays spray/firing error for the idle time since the previous shot. Call before evaluating a shot. */
	void Recover(double Now, const class UWeaponStats& Stats);

	/** Advances the pattern after a shot has been evaluated. */
	void CommitShot(double Now, const class UWeaponStats& Stats);

	int32 GetShotIndex() const { return FMath::FloorToInt32(SprayProgress + KINDA_SMALL_NUMBER); }

	void Reset() { *this = FWeaponSprayState(); }
};

/**
 * Data-driven weapon definition. Immutable at runtime (Const): one asset is shared by every
 * instance of a weapon, both on the server and on clients, so there is nothing to replicate
 * except the asset reference itself.
 */
UCLASS(BlueprintType, Const)
class TACTICALDEPLOYMENT_API UWeaponStats : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// --- Identity / economy -----------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	TSubclassOf<ATacticalWeapon> WeaponClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity", meta = (ClampMin = "0"))
	int32 Cost = 2900;

	// --- Damage -----------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0"))
	float BaseDamage = 40.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0"))
	float HeadMultiplier = 4.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0"))
	float ArmMultiplier = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0"))
	float LegMultiplier = 0.85f;

	/** Sorted ascending by MaxDistance. Empty = no falloff. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage")
	TArray<FDamageRangeBracket> RangeBrackets;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (Units = "cm"))
	float MaxRange = 12000.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage")
	EWallPenetrationTier WallPenetrationTier = EWallPenetrationTier::Medium;

	// --- Handling ---------------------------------------------------------------------

	/** Rounds per second. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "0.1"))
	float FireRate = 9.75f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling")
	bool bAutomatic = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "1"))
	int32 MagazineSize = 25;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (Units = "s"))
	float ReloadTime = 2.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (Units = "s"))
	float EquipTime = 1.0f;

	/** Scales MaxWalkSpeed while this weapon is held (knife > pistol > rifle > sniper). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "0.1", ClampMax = "1.5"))
	float MovementSpeedMultiplier = 1.f;

	// --- Recoil (deterministic) -------------------------------------------------------

	/**
	 * Absolute (cumulative) recoil offset keyed by bullet index in the current spray.
	 * X = horizontal pull (yaw, degrees, +right). Y = vertical climb (pitch, degrees, +up).
	 * Key 0 should be (0,0) so the first bullet goes exactly where the crosshair is.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
	TObjectPtr<UCurveVector> RecoilPattern;

	/** Idle time after the last shot before the pattern starts rewinding. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (Units = "s"))
	float RecoilRecoveryDelay = 0.1f;

	/** Bullet indices recovered per second once recovery starts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = "0"))
	float RecoilRecoveryRate = 20.f;

	/** Fraction of the recoil offset shown as view kick (visual only; never affects ControlRotation). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = "0", ClampMax = "1"))
	float CameraKickFraction = 0.35f;

	// --- Spread (conditional accuracy) ------------------------------------------------

	/** Cone half-angle (degrees) when standing still, first shot. 0 = pinpoint first-shot accuracy. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0"))
	float BaseSpread = 0.1f;

	/** Below this fraction of run speed the player counts as stationary (counter-strafe window). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0", ClampMax = "1"))
	float AccurateSpeedFraction = 0.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0"))
	float WalkSpread = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0"))
	float RunSpread = 5.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0"))
	float AirborneSpread = 10.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0", ClampMax = "1"))
	float CrouchSpreadMultiplier = 0.8f;

	/** Shots fired before firing error starts accumulating (tap/burst window). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0"))
	int32 FiringErrorGraceShots = 3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0"))
	float FiringErrorPerShot = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0"))
	float MaxFiringError = 3.f;

	/** Degrees of firing error removed per idle second. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0"))
	float FiringErrorRecoveryRate = 12.f;

	// --- Tagging ----------------------------------------------------------------------

	/** Fraction of MaxWalkSpeed removed on the victim when this weapon hits (0.7 = 70% slow). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tagging", meta = (ClampMin = "0", ClampMax = "1"))
	float TaggingSlow = 0.7f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tagging", meta = (Units = "s"))
	float TaggingDuration = 0.5f;

public:
	FORCEINLINE float GetFireInterval() const { return 1.f / FMath::Max(FireRate, 0.1f); }

	float GetZoneMultiplier(EHitZone Zone) const;
	float GetRangeMultiplier(float DistanceCm) const;

	/** Recoil offset (X yaw, Y pitch, degrees) for a bullet index. */
	FVector2D EvaluateRecoil(int32 ShotIndex) const;

	/** Cone half-angle in degrees for the given authoritative movement/firing state. */
	float ComputeSpread(const FWeaponSpreadInputs& Inputs) const;

	static const FPenetrationTierParams& GetPenetrationParams(EWallPenetrationTier Tier);
	const FPenetrationTierParams& GetPenetrationParams() const { return GetPenetrationParams(WallPenetrationTier); }

	static ETacticalMovementState ClassifyMovement(const FWeaponSpreadInputs& Inputs, float AccurateSpeedFraction);

	/** Aim rotation -> final bullet direction. Recoil is deterministic; spread draws from Rng. */
	static FVector ApplyRecoilAndSpread(const FRotator& AimRotation, const FVector2D& RecoilDegrees, float SpreadHalfAngleDegrees, FRandomStream& Rng);

	//~ UPrimaryDataAsset
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
