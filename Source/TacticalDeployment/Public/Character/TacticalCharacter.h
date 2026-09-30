// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Character/TakeDamageTagging.h"
#include "Combat/LagCompensationComponent.h"
#include "Core/TacticalTypes.h"
#include "TacticalCharacter.generated.h"

class ATacticalCharacter;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UTacticalCharacterMovementComponent;
class ATacticalWeapon;
class UWeaponStats;
struct FInputActionValue;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTacticalCharacterDied, ATacticalCharacter* /*Victim*/, AController* /*Killer*/);

/**
 * 5v5 tactical character.
 *
 * Rendering split (Pillar 6):
 *   Mesh1P  arms + viewmodel, bOnlyOwnerSee, attached to FirstPersonCamera, never casts shadows.
 *   Mesh3P  (ACharacter::Mesh) full body, bOwnerNoSee, drives the lag-compensated hitboxes on the
 *           server and the AimOffset every other client sees.
 *
 * Network roles:
 *   Autonomous proxy: predicts its own movement (UTacticalCharacterMovementComponent), predicts
 *                     weapon cosmetics, sends inputs + fire requests.
 *   Server:           authoritative movement, health, hit registration, fog-of-war relevancy.
 *   Simulated proxy:  interpolated movement + 16-bit aim pitch for the AimOffset. Only exists on a
 *                     client while the server's fog of war says it may.
 */
UCLASS(config = Game)
class TACTICALDEPLOYMENT_API ATacticalCharacter : public ACharacter, public ITakeDamageTagging
{
	GENERATED_BODY()

public:
	ATacticalCharacter(const FObjectInitializer& ObjectInitializer);

	// --- Components ---------------------------------------------------------------------

	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }
	USkeletalMeshComponent* GetMesh1P() const { return Mesh1P; }
	USkeletalMeshComponent* GetMesh3P() const { return GetMesh(); }
	UTacticalCharacterMovementComponent* GetTacticalMovement() const;

	// --- State --------------------------------------------------------------------------

	ETacticalTeam GetTeam() const;
	bool IsAlive() const { return !bIsDead; }
	float GetHealth() const { return Health; }
	float GetArmor() const { return Armor; }

	const TArray<FHitboxDefinition>& GetHitboxDefinitions() const { return Hitboxes; }

	FOnTacticalCharacterDied OnDied;

	// --- Weapons ------------------------------------------------------------------------

	ATacticalWeapon* GetCurrentWeapon() const { return CurrentWeapon; }
	const TArray<TObjectPtr<ATacticalWeapon>>& GetInventory() const { return Inventory; }
	float GetEquippedMovementMultiplier() const;

	/** Server: spawn + equip a weapon from its stats asset. */
	ATacticalWeapon* ServerGiveWeapon(const UWeaponStats* Stats);

	/** Server: add armor purchased in the shop. */
	void ServerSetArmor(float NewArmor);

	/** Cosmetic view kick from recoil (owning client only, never touches ControlRotation). */
	void SetViewKickTarget(const FRotator& Target) { ViewKickTarget = Target; }

	// --- Aim ----------------------------------------------------------------------------

	/** Full-precision on the owner/server; 16-bit replicated pitch on simulated proxies. */
	virtual FRotator GetBaseAimRotation() const override;

	// --- Damage -------------------------------------------------------------------------

	/** Server only. Armor absorption, tagging, death. */
	void ApplyBulletDamage(float Damage, EHitZone Zone, const UWeaponStats* Weapon, AController* InstigatorController, AActor* DamageCauser);

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	//~ ITakeDamageTagging
	virtual void ApplyDamageTag(const FDamageTagParams& Params) override;
	virtual float GetTaggingSpeedScalar() const override;

	// --- Fog of war ---------------------------------------------------------------------

	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;

	/** Server: something audible happened here (gunshot, landing, running footsteps). */
	void ReportNoise(float Radius);
	double GetLastNoiseTime() const { return LastNoiseTime; }
	float GetLastNoiseRadius() const { return LastNoiseRadius; }

	// --- Spike interaction --------------------------------------------------------------

	UFUNCTION(Server, Reliable)
	void Server_BeginInteract();

	UFUNCTION(Server, Reliable)
	void Server_EndInteract();

	UFUNCTION(Client, Reliable)
	void Client_InteractionEnded();

	//~ AActor / APawn / ACharacter
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	virtual void Landed(const FHitResult& Hit) override;

protected:
	void Die(AController* Killer);

	void EquipWeapon(ATacticalWeapon* Weapon);

	// Input handlers
	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_CrouchPressed();
	void Input_CrouchReleased();
	void Input_WalkPressed();
	void Input_WalkReleased();
	void Input_FirePressed();
	void Input_FireReleased();
	void Input_Reload();
	void Input_InteractPressed();
	void Input_InteractReleased();

	UFUNCTION()
	void OnRep_CurrentWeapon(ATacticalWeapon* PreviousWeapon);

	UFUNCTION()
	void OnRep_TaggingState();

	UFUNCTION()
	void OnRep_IsDead();

	// --- Components ---------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> Mesh1P;

	// --- Authoring ----------------------------------------------------------------------

	/** Lag-compensated hitboxes, glued to Mesh3P bones. Authored in the character Blueprint. */
	UPROPERTY(EditDefaultsOnly, Category = "Hitboxes")
	TArray<FHitboxDefinition> Hitboxes;

	UPROPERTY(EditDefaultsOnly, Category = "Health")
	float MaxHealth = 100.f;

	/** Fraction of incoming damage the armor pool soaks. */
	UPROPERTY(EditDefaultsOnly, Category = "Health", meta = (ClampMin = "0", ClampMax = "1"))
	float ArmorAbsorption = 0.66f;

	UPROPERTY(EditDefaultsOnly, Category = "Fog of War", meta = (Units = "cm"))
	float LandingNoiseRadius = 2200.f;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> CrouchAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> WalkAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> ReloadAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	// --- Replicated (all push-model) ----------------------------------------------------

	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon)
	TObjectPtr<ATacticalWeapon> CurrentWeapon;

	/** Owner-only: enemies never learn your loadout beyond the weapon in your hands. */
	UPROPERTY(Replicated)
	TArray<TObjectPtr<ATacticalWeapon>> Inventory;

	/** Skip-owner, 2 bytes. Drives the 3P AimOffset and matches the server's hitbox pose. */
	UPROPERTY(Replicated)
	uint16 ReplicatedAimPitch = 32768;

	/** Owner-only: the autonomous proxy needs it to predict its own slowed movement. */
	UPROPERTY(ReplicatedUsing = OnRep_TaggingState)
	FTaggingState ReplicatedTagging;

	/** Owner-only: enemy health is not information the client is entitled to. */
	UPROPERTY(Replicated)
	float Health = 100.f;

	UPROPERTY(Replicated)
	float Armor = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_IsDead)
	bool bIsDead = false;

	// --- Server-only --------------------------------------------------------------------

	double LastNoiseTime = -1.0e9;
	float LastNoiseRadius = 0.f;

	// --- Local-only ---------------------------------------------------------------------

	FRotator ViewKick = FRotator::ZeroRotator;
	FRotator ViewKickTarget = FRotator::ZeroRotator;
};
