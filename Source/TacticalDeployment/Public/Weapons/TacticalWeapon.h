// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weapons/WeaponStats.h"
#include "Engine/NetSerialization.h"
#include "TacticalWeapon.generated.h"

class ATacticalCharacter;
class FScopedLagCompensation;
class USkeletalMeshComponent;

/**
 * Cosmetic shot notification for everyone except the shooter (who predicted it locally).
 * A property, not a multicast: fog-of-war filtering applies to it for free, and under
 * sustained fire it coalesces to the latest shot instead of queueing RPCs.
 */
USTRUCT()
struct FShotNotify
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize ImpactPoint = FVector::ZeroVector;

	/** Increments per shot so identical impact points still fire OnRep. */
	UPROPERTY()
	uint8 ShotCounter = 0;
};

/**
 * Hitscan weapon.
 *
 * Client:  predicts cosmetics (tracer, muzzle flash, view kick) and sends
 *          Server_FireWeapon(ViewTime, StartTrace, EndTrace) - aim only, no recoil/spread.
 * Server:  validates (alive, phase, ammo, cadence, eye position), advances the authoritative
 *          spray state, applies deterministic recoil + server-seeded spread, then resolves the
 *          shot in a lag-compensated scope with wall penetration.
 *
 * The client never tells the server where the bullet went, only where the crosshair was.
 * No-recoil / no-spread cheats therefore have nothing to tamper with.
 */
UCLASS(Abstract)
class TACTICALDEPLOYMENT_API ATacticalWeapon : public AActor
{
	GENERATED_BODY()

public:
	ATacticalWeapon();

	void InitializeFromStats(const UWeaponStats* InStats);
	const UWeaponStats* GetStats() const { return Stats; }

	void OnEquipped(ATacticalCharacter* NewOwner);
	void OnUnequipped();

	// --- Local input (owning client) ------------------------------------------------------

	void StartFire();
	void StopFire();
	void StartReload();

	//~ AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/**
	 * @param ViewTime    Client's estimate of the server time of the world it rendered.
	 * @param StartTrace  Eye position.
	 * @param EndTrace    StartTrace + aim direction * MaxRange (crosshair only, no recoil/spread).
	 * @param ShotIndex   Client's predicted spray index; used only for desync telemetry.
	 */
	UFUNCTION(Server, Reliable)
	void Server_FireWeapon(double ViewTime, FVector_NetQuantize100 StartTrace, FVector_NetQuantize EndTrace, uint8 ShotIndex);

	UFUNCTION(Server, Reliable)
	void Server_Reload();

	UFUNCTION()
	void OnRep_ShotNotify();

	UFUNCTION()
	void OnRep_AmmoInMagazine();

	/** Local fire loop tick (owning client). */
	void LocalFireShot();

	/** Server: validate + resolve one shot. */
	void ServerResolveShot(double ViewTime, const FVector& Start, const FVector& AimDirection);

	/** Server: walk the bullet through surfaces, lag-compensated. Returns the final impact point. */
	FVector TraceWithPenetration(const FScopedLagCompensation& Rewind, const FVector& Start, const FVector& Direction);

	bool FindExitPoint(const FHitResult& EntryHit, const FVector& Direction, float MaxThickness, FVector& OutExit) const;

	void ApplyHitDamage(ATacticalCharacter* Victim, EHitZone Zone, float TravelledDistance, float PenetrationScale);

	bool ServerValidateFire(double ServerNow, double ViewTime, const FVector& Start) const;

	FWeaponSpreadInputs BuildSpreadInputs(float FiringError) const;

	void FinishReload();

	/** Cosmetic hook (tracers, muzzle flash, sounds). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon")
	void PlayFireEffects(const FVector& ImpactPoint);

	// --- Components -----------------------------------------------------------------------

	/** Viewmodel: attached to the owner's Mesh1P, only the owner sees it. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh1P;

	/** World model: attached to the owner's Mesh3P, everyone but the owner sees it. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh3P;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FName Grip1PSocket = TEXT("GripPoint");

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FName Grip3PSocket = TEXT("weapon_r");

	/** Allowed distance between the client's reported eye and the server's, in cm. */
	UPROPERTY(EditDefaultsOnly, Category = "Anti-Cheat")
	float MaxEyeLocationError = 48.f;

	// --- Replicated -----------------------------------------------------------------------

	UPROPERTY(Replicated)
	TObjectPtr<const UWeaponStats> Stats;

	/** Owner only: ammo is nobody else's business. */
	UPROPERTY(ReplicatedUsing = OnRep_AmmoInMagazine)
	int32 AmmoInMagazine = 0;

	UPROPERTY(ReplicatedUsing = OnRep_ShotNotify)
	FShotNotify ShotNotify;

	// --- Server-only authoritative state --------------------------------------------------

	FWeaponSprayState ServerSpray;
	FRandomStream ServerSpreadStream; // Seeded with a server secret: spread is not predictable client-side.
	double LastServerClientViewTime = -1.0;
	double ServerEquipCompleteTime = 0.0;
	bool bServerReloading = false;
	FTimerHandle ServerReloadTimer;

	// --- Owning-client prediction state ---------------------------------------------------

	FWeaponSprayState LocalSpray;
	FRandomStream LocalCosmeticStream;
	FTimerHandle LocalFireTimer;
	double LocalLastFireTime = -1.0e9;
	int32 LocalPredictedAmmo = 0;
	bool bLocalTriggerHeld = false;

	UPROPERTY(Transient)
	TObjectPtr<ATacticalCharacter> OwnerCharacter;
};
