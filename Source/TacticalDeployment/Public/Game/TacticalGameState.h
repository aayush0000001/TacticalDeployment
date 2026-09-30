// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Core/TacticalTypes.h"
#include "TacticalGameState.generated.h"

class ULagCompensationComponent;
class ASpikeBase;

/**
 * Everything clients need about the round, in one struct so a phase transition is one atomic
 * replicated change and one OnRep. Clients derive countdowns locally from PhaseEndServerTime:
 * zero bytes per second while a phase runs.
 */
USTRUCT(BlueprintType)
struct FTacticalRoundState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	ETacticalMatchPhase Phase = ETacticalMatchPhase::PreMatch;

	UPROPERTY(BlueprintReadOnly)
	double PhaseEndServerTime = 0.0;

	UPROPERTY(BlueprintReadOnly)
	uint8 RoundNumber = 0;

	UPROPERTY(BlueprintReadOnly)
	ETacticalTeam AttackingTeam = ETacticalTeam::TeamA;

	UPROPERTY(BlueprintReadOnly)
	bool bSpikePlanted = false;

	UPROPERTY(BlueprintReadOnly)
	uint8 TeamAScore = 0;

	UPROPERTY(BlueprintReadOnly)
	uint8 TeamBScore = 0;

	UPROPERTY(BlueprintReadOnly)
	ETacticalTeam LastRoundWinner = ETacticalTeam::Spectator;

	UPROPERTY(BlueprintReadOnly)
	ETacticalRoundEndReason LastRoundEndReason = ETacticalRoundEndReason::None;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMatchPhaseChanged, ETacticalMatchPhase /*Old*/, ETacticalMatchPhase /*New*/);

/**
 * Replicated round state + server-only hit registration history.
 * Written only by ATacticalGameMode; read by everyone.
 */
UCLASS()
class TACTICALDEPLOYMENT_API ATacticalGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ATacticalGameState();

	const FTacticalRoundState& GetRoundState() const { return RoundState; }
	ETacticalMatchPhase GetPhase() const { return RoundState.Phase; }
	ETacticalTeam GetAttackingTeam() const { return RoundState.AttackingTeam; }
	ETacticalTeam GetDefendingTeam() const { return GetOpposingTeam(RoundState.AttackingTeam); }

	/** Seconds left in the current phase, from the replicated end time (no per-second replication). */
	UFUNCTION(BlueprintPure, Category = "Round")
	float GetPhaseTimeRemaining() const;

	/** PreMatch and BuyPhase freeze movement (queried by the CMC on both server and client). */
	bool IsMovementLocked() const;

	/** Weapons are live only during ActionPhase. */
	bool IsCombatAllowed() const { return RoundState.Phase == ETacticalMatchPhase::ActionPhase; }

	bool IsShopOpen() const { return RoundState.Phase == ETacticalMatchPhase::BuyPhase || RoundState.Phase == ETacticalMatchPhase::BarrierPhase; }

	ULagCompensationComponent* GetLagCompensation() const { return LagCompensation; }

	ASpikeBase* GetSpike() const { return Spike; }

	FOnMatchPhaseChanged OnPhaseChanged;

	// --- Server-only mutation (called by ATacticalGameMode) -------------------------------

	void ServerSetPhase(ETacticalMatchPhase NewPhase, float Duration);
	void ServerSetPhaseEndTime(double EndServerTime);
	void ServerSetSpikePlanted(bool bPlanted);
	void ServerBeginRound(uint8 RoundNumber, ETacticalTeam AttackingTeam);
	void ServerRecordRoundResult(ETacticalTeam Winner, ETacticalRoundEndReason Reason);
	void ServerSetSpike(ASpikeBase* InSpike);

	uint8 GetScore(ETacticalTeam Team) const { return Team == ETacticalTeam::TeamA ? RoundState.TeamAScore : RoundState.TeamBScore; }

	//~ AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_RoundState(const FTacticalRoundState& Previous);

	void MarkRoundStateDirty(ETacticalMatchPhase PreviousPhase);

	UPROPERTY(ReplicatedUsing = OnRep_RoundState)
	FTacticalRoundState RoundState;

	UPROPERTY(Replicated)
	TObjectPtr<ASpikeBase> Spike;

	/** Server only: 1000 ms hitbox history for server-side rewind. Not replicated. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<ULagCompensationComponent> LagCompensation;
};
