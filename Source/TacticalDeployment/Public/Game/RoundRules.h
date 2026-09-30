// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

// Engine-light round rules. ATacticalGameMode orchestrates (spawning, economy, actors) and asks
// these functions every rules question, so the simulation harness plays the exact same match logic.

#include "CoreMinimal.h"
#include "Core/TacticalTypes.h"

namespace RoundRules
{
	/** Sides swap once, after HalftimeAfterRound. */
	inline ETacticalTeam GetAttackingTeam(int32 RoundNumber, int32 HalftimeAfterRound)
	{
		return RoundNumber <= HalftimeAfterRound ? ETacticalTeam::TeamA : ETacticalTeam::TeamB;
	}

	/** First to RoundsToWin; from (RoundsToWin-1)-all onwards it is overtime: win by two. */
	inline bool HasTeamWonMatch(int32 Score, int32 OtherScore, int32 RoundsToWin)
	{
		if (Score < RoundsToWin)
		{
			return false;
		}
		return OtherScore >= RoundsToWin - 1 ? (Score - OtherScore) >= 2 : true;
	}

	/**
	 * Winner by elimination, or ETacticalTeam::Spectator if the round continues.
	 * Attackers wiped after planting do NOT lose: defenders still have to defuse.
	 * Both teams wiped at once: attackers win only if the spike is down.
	 */
	inline ETacticalTeam EvaluateElimination(int32 AttackersAlive, int32 DefendersAlive, bool bSpikePlanted, ETacticalTeam AttackingTeam)
	{
		const ETacticalTeam DefendingTeam = GetOpposingTeam(AttackingTeam);
		if (AttackersAlive == 0 && DefendersAlive == 0)
		{
			return bSpikePlanted ? AttackingTeam : DefendingTeam;
		}
		if (DefendersAlive == 0)
		{
			return AttackingTeam;
		}
		if (AttackersAlive == 0 && !bSpikePlanted)
		{
			return DefendingTeam;
		}
		return ETacticalTeam::Spectator;
	}

	inline bool IsMovementLocked(ETacticalMatchPhase Phase)
	{
		return Phase == ETacticalMatchPhase::PreMatch || Phase == ETacticalMatchPhase::BuyPhase || Phase == ETacticalMatchPhase::MatchEnded;
	}

	inline bool IsCombatAllowed(ETacticalMatchPhase Phase) { return Phase == ETacticalMatchPhase::ActionPhase; }

	inline bool IsShopOpen(ETacticalMatchPhase Phase)
	{
		return Phase == ETacticalMatchPhase::BuyPhase || Phase == ETacticalMatchPhase::BarrierPhase;
	}

	inline bool AreBarriersUp(ETacticalMatchPhase Phase)
	{
		return Phase == ETacticalMatchPhase::PreMatch || Phase == ETacticalMatchPhase::BuyPhase || Phase == ETacticalMatchPhase::BarrierPhase;
	}

	enum class EPhaseTimerAction : uint8
	{
		None,
		StartNewRound,       // PreMatch expired.
		EnterBarrierPhase,   // BuyPhase expired.
		EnterActionPhase,    // BarrierPhase expired.
		DefendersWinOnTime,  // ActionPhase expired with no plant.
		FinishPostRound,     // PostRound expired: next round or match end.
	};

	/** While the spike is planted, the phase timer is the fuse and the spike resolves the round. */
	inline EPhaseTimerAction OnPhaseTimerExpired(ETacticalMatchPhase Phase, bool bSpikePlanted)
	{
		switch (Phase)
		{
		case ETacticalMatchPhase::PreMatch:     return EPhaseTimerAction::StartNewRound;
		case ETacticalMatchPhase::BuyPhase:     return EPhaseTimerAction::EnterBarrierPhase;
		case ETacticalMatchPhase::BarrierPhase: return EPhaseTimerAction::EnterActionPhase;
		case ETacticalMatchPhase::ActionPhase:  return bSpikePlanted ? EPhaseTimerAction::None : EPhaseTimerAction::DefendersWinOnTime;
		case ETacticalMatchPhase::PostRound:    return EPhaseTimerAction::FinishPostRound;
		default:                                return EPhaseTimerAction::None;
		}
	}
}
