// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/TacticalTypes.h"
#include "Game/SpikeRules.h"
#include "SpikeBase.generated.h"

class AGameStateBase;
class ATacticalCharacter;
class UBoxComponent;
class USphereComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class ESpikeState : uint8
{
	Carried,
	Dropped,
	Planted,
	Defused,
	Detonated,
};

/**
 * The bomb. Fully server-authoritative:
 *  - Plant: 4 s hold, carrier only, inside an ASpikeSiteVolume, ActionPhase, grounded, still.
 *  - Defuse: 7 s hold, defenders only, within range.
 *  - 50% defuse checkpoint: releasing at or after 3.5 s banks half the defuse; the next attempt
 *    (by anyone) resumes from 50% and needs only 3.5 s.
 *  - Detonation vs. defuse race is resolved by comparing exact completion timestamps, not by
 *    which check happens to run first in a frame.
 *
 * Relevancy: while carried, the spike shares its carrier's fog-of-war visibility (it would
 * otherwise reveal the carrier's position); dropped/planted it is relevant to everyone.
 */
UCLASS(Abstract)
class TACTICALDEPLOYMENT_API ASpikeBase : public AActor
{
	GENERATED_BODY()

public:
	ASpikeBase();

	// --- Queries (client + server) ----------------------------------------------------------

	ESpikeState GetState() const { return State; }
	ATacticalCharacter* GetCarrier() const { return Carrier; }
	const FSpikeInteractionState& GetInteraction() const { return Interaction; }
	float GetDefuseCheckpoint() const { return DefuseCheckpoint; }
	double GetDetonationServerTime() const { return DetonationServerTime; }

	/** Used by the client to predict its movement lock and by the server to validate. */
	bool CanCharacterInteract(const ATacticalCharacter* Character) const;

	UFUNCTION(BlueprintPure, Category = "Spike")
	float GetInteractionProgress() const;

	// --- Server -------------------------------------------------------------------------------

	void ServerGiveTo(ATacticalCharacter* NewCarrier);
	bool ServerTryBeginInteract(ATacticalCharacter* Character);
	void ServerEndInteract(ATacticalCharacter* Character);
	void ServerOnCharacterDied(ATacticalCharacter* Character);
	void ServerOnRoundEnded();

	//~ AActor
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;

	// --- Tuning -------------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, Category = "Spike", meta = (Units = "s"))
	float PlantDuration = 4.f;

	UPROPERTY(EditDefaultsOnly, Category = "Spike", meta = (Units = "s"))
	float DefuseDuration = 7.f;

	UPROPERTY(EditDefaultsOnly, Category = "Spike", meta = (ClampMin = "0", ClampMax = "1"))
	float DefuseCheckpointFraction = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Spike", meta = (Units = "cm"))
	float DefuseRange = 110.f;

	/** Planter/defuser may not drift further than this from where they started (anti-cheat). */
	UPROPERTY(EditDefaultsOnly, Category = "Spike", meta = (Units = "cm"))
	float MaxInteractionDrift = 8.f;

	UPROPERTY(EditDefaultsOnly, Category = "Spike", meta = (Units = "cm"))
	float DetonationRadius = 3000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Spike")
	float DetonationDamage = 500.f;

	UPROPERTY(EditDefaultsOnly, Category = "Spike")
	FName CarrySocket = TEXT("spike_socket");

protected:
	void ServerBeginPlant(ATacticalCharacter* Planter);
	void ServerBeginDefuse(ATacticalCharacter* Defuser);
	void ServerStopInteraction(bool bBankCheckpoint);
	void ServerCompletePlant();
	void ServerCompleteDefuse();
	void ServerDetonate();
	void ServerDrop();
	bool IsInterruptionRequired(double Now) const;
	bool IsInsidePlantSite(const FVector& Location) const;
	void SetState(ESpikeState NewState);
	void AttachToCarrier();
	void NotifyFogLoadoutChanged(ATacticalCharacter* Character) const;

	UFUNCTION()
	void OnPickupOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnRep_State();

	UFUNCTION()
	void OnRep_Carrier();

	UFUNCTION(BlueprintImplementableEvent, Category = "Spike")
	void OnSpikeStateChanged(ESpikeState NewState);

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USphereComponent> PickupSphere;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	ESpikeState State = ESpikeState::Dropped;

	UPROPERTY(ReplicatedUsing = OnRep_Carrier)
	TObjectPtr<ATacticalCharacter> Carrier;

	UPROPERTY(Replicated)
	FSpikeInteractionState Interaction;

	UPROPERTY(Replicated)
	float DefuseCheckpoint = 0.f;

	UPROPERTY(Replicated)
	double DetonationServerTime = 0.0;

	/** Server: where the current interactor stood when they began. */
	FVector InteractionAnchor = FVector::ZeroVector;
};

/** Designer-placed plant zone (A / B / C site). */
UCLASS()
class TACTICALDEPLOYMENT_API ASpikeSiteVolume : public AActor
{
	GENERATED_BODY()

public:
	ASpikeSiteVolume();

	bool ContainsPoint(const FVector& WorldLocation) const;

	UPROPERTY(EditAnywhere, Category = "Site")
	FName SiteName = TEXT("A");

protected:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UBoxComponent> Bounds;
};

/**
 * Barrier-phase wall. Not replicated: both server and clients toggle collision from the
 * replicated match phase, so predicted movement agrees on both sides for free.
 */
UCLASS()
class TACTICALDEPLOYMENT_API ATacticalSpawnBarrier : public AActor
{
	GENERATED_BODY()

public:
	ATacticalSpawnBarrier();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	void BindToGameState(AGameStateBase* GameState);
	void ApplyPhase(ETacticalMatchPhase Phase);

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UBoxComponent> Barrier;

	FDelegateHandle PhaseChangedHandle;
	FDelegateHandle GameStateSetHandle;
};
