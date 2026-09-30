// Copyright TacticalDeployment. All Rights Reserved.

#include "Game/TacticalGameState.h"
#include "Combat/LagCompensationComponent.h"
#include "Game/RoundRules.h"
#include "Game/SpikeBase.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

ATacticalGameState::ATacticalGameState()
{
	LagCompensation = CreateDefaultSubobject<ULagCompensationComponent>(TEXT("LagCompensation"));

	// Base server-time sync only drives UI countdowns; hit registration uses the
	// PlayerController's NTP-style clock instead.
	ServerWorldTimeSecondsUpdateFrequency = 1.f;
}

void ATacticalGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(ATacticalGameState, RoundState, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(ATacticalGameState, Spike, Params);
}

float ATacticalGameState::GetPhaseTimeRemaining() const
{
	return FMath::Max(0.f, static_cast<float>(RoundState.PhaseEndServerTime - GetServerWorldTimeSeconds()));
}

bool ATacticalGameState::IsMovementLocked() const
{
	return RoundRules::IsMovementLocked(RoundState.Phase);
}

void ATacticalGameState::MarkRoundStateDirty(ETacticalMatchPhase PreviousPhase)
{
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalGameState, RoundState, this);
	if (PreviousPhase != RoundState.Phase)
	{
		OnPhaseChanged.Broadcast(PreviousPhase, RoundState.Phase); // Server-side listeners (barriers).
	}
}

void ATacticalGameState::ServerSetPhase(ETacticalMatchPhase NewPhase, float Duration)
{
	check(HasAuthority());
	const ETacticalMatchPhase Previous = RoundState.Phase;
	RoundState.Phase = NewPhase;
	RoundState.PhaseEndServerTime = GetServerWorldTimeSeconds() + Duration;
	MarkRoundStateDirty(Previous);
}

void ATacticalGameState::ServerSetPhaseEndTime(double EndServerTime)
{
	check(HasAuthority());
	RoundState.PhaseEndServerTime = EndServerTime;
	MarkRoundStateDirty(RoundState.Phase);
}

void ATacticalGameState::ServerSetSpikePlanted(bool bPlanted)
{
	check(HasAuthority());
	RoundState.bSpikePlanted = bPlanted;
	MarkRoundStateDirty(RoundState.Phase);
}

void ATacticalGameState::ServerBeginRound(uint8 RoundNumber, ETacticalTeam AttackingTeam)
{
	check(HasAuthority());
	RoundState.RoundNumber = RoundNumber;
	RoundState.AttackingTeam = AttackingTeam;
	RoundState.bSpikePlanted = false;
	RoundState.LastRoundEndReason = ETacticalRoundEndReason::None;
	MarkRoundStateDirty(RoundState.Phase);
}

void ATacticalGameState::ServerRecordRoundResult(ETacticalTeam Winner, ETacticalRoundEndReason Reason)
{
	check(HasAuthority());
	if (Winner == ETacticalTeam::TeamA)
	{
		++RoundState.TeamAScore;
	}
	else if (Winner == ETacticalTeam::TeamB)
	{
		++RoundState.TeamBScore;
	}
	RoundState.LastRoundWinner = Winner;
	RoundState.LastRoundEndReason = Reason;
	MarkRoundStateDirty(RoundState.Phase);
}

void ATacticalGameState::ServerSetSpike(ASpikeBase* InSpike)
{
	check(HasAuthority());
	Spike = InSpike;
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalGameState, Spike, this);
}

void ATacticalGameState::OnRep_RoundState(const FTacticalRoundState& Previous)
{
	if (Previous.Phase != RoundState.Phase)
	{
		OnPhaseChanged.Broadcast(Previous.Phase, RoundState.Phase); // Client-side listeners (barriers, HUD).
	}
}
