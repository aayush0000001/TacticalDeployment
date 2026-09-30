// Copyright TacticalDeployment. All Rights Reserved.
// Pillar 5: round state machine and spike. Real code: Game/RoundRules.h, Game/SpikeRules.h.

#include "SimTest.h"
#include "Game/RoundRules.h"
#include "Game/SpikeRules.h"

using namespace SpikeRules;

namespace
{
	constexpr float PlantTime = 4.f;
	constexpr float DefuseTime = 7.f;
	constexpr float Checkpoint = 0.5f;
}

// ---------------------------------------------------------------------------------------
// Spike
// ---------------------------------------------------------------------------------------

SIM_TEST(Spike, PlantTakesFourSecondsAndIsNeverBanked)
{
	const FSpikeInteractionState Plant = MakePlant(100.0, PlantTime);
	SIM_EXPECT_TRUE(Evaluate(Plant, false, 0.0, 103.999) == ETickOutcome::None);
	SIM_EXPECT_TRUE(Evaluate(Plant, false, 0.0, 104.0) == ETickOutcome::PlantComplete);
	SIM_EXPECT_NEAR(Plant.GetProgress(102.0), 0.5, 1e-6);
	// Releasing at 3.9 s banks nothing: the next plant needs the full 4 s again.
	SIM_EXPECT_NEAR(BankDefuseCheckpoint(Plant, 103.9, 0.f, Checkpoint), 0.0, 0.0);
	const FSpikeInteractionState Retry = MakePlant(110.0, PlantTime);
	SIM_EXPECT_NEAR(Retry.GetCompletionTime(), 114.0, 1e-9);
}

SIM_TEST(Spike, DefuseCheckpointBanksHalfAtThreeAndAHalfSeconds)
{
	struct FCase { double HeldFor; float Expected; };
	for (const FCase C : { FCase{ 1.0, 0.f }, FCase{ 3.4, 0.f }, FCase{ 3.499, 0.f }, FCase{ 3.5, 0.5f }, FCase{ 5.0, 0.5f }, FCase{ 6.9, 0.5f } })
	{
		const FSpikeInteractionState Defuse = MakeDefuse(200.0, DefuseTime, 0.f);
		const float Banked = BankDefuseCheckpoint(Defuse, 200.0 + C.HeldFor, 0.f, Checkpoint);
		SIM_EXPECT_NEAR(Banked, C.Expected, 0.0);
		SimTest::Report("released after %.3f s -> checkpoint %.0f%%", C.HeldFor, Banked * 100.f);
	}
}

SIM_TEST(Spike, ResumedDefuseNeedsOnlyTheRemainingHalf)
{
	// First attempt: held 4 s, killed. Second attempt (another defender) resumes from 50%.
	FSpikeInteractionState First = MakeDefuse(300.0, DefuseTime, 0.f);
	float Banked = BankDefuseCheckpoint(First, 304.0, 0.f, Checkpoint);
	SIM_EXPECT_NEAR(Banked, 0.5, 0.0);

	const FSpikeInteractionState Second = MakeDefuse(310.0, DefuseTime, Banked);
	SIM_EXPECT_NEAR(Second.Duration, 3.5, 1e-6);
	SIM_EXPECT_NEAR(Second.GetProgress(310.0), 0.5, 1e-6); // Progress bar resumes at half.
	SIM_EXPECT_TRUE(Evaluate(Second, true, 400.0, 313.499) == ETickOutcome::None);
	SIM_EXPECT_TRUE(Evaluate(Second, true, 400.0, 313.5) == ETickOutcome::DefuseComplete);

	// Letting go again at 64% keeps the checkpoint at 50%, never more.
	Banked = BankDefuseCheckpoint(Second, 311.0, Banked, Checkpoint);
	SIM_EXPECT_NEAR(Banked, 0.5, 0.0);

	// Full uninterrupted defuse: 7 s.
	const FSpikeInteractionState Clean = MakeDefuse(500.0, DefuseTime, 0.f);
	SIM_EXPECT_TRUE(Evaluate(Clean, true, 600.0, 506.999) == ETickOutcome::None);
	SIM_EXPECT_TRUE(Evaluate(Clean, true, 600.0, 507.0) == ETickOutcome::DefuseComplete);
}

SIM_TEST(Spike, DefuseVersusDetonationIsDecidedByTimestamps)
{
	const double Detonation = 1000.0;

	// Defuse completes 1 ms before detonation, but the server only ticks 5 ms after both: defused.
	const FSpikeInteractionState JustInTime = MakeDefuse(Detonation - 7.001, DefuseTime, 0.f);
	SIM_EXPECT_TRUE(Evaluate(JustInTime, true, Detonation, Detonation + 0.005) == ETickOutcome::DefuseComplete);

	// Exact tie goes to the defuser.
	const FSpikeInteractionState Tie = MakeDefuse(Detonation - 7.0, DefuseTime, 0.f);
	SIM_EXPECT_TRUE(Evaluate(Tie, true, Detonation, Detonation) == ETickOutcome::DefuseComplete);

	// 1 ms too late: detonation, even if evaluated after the defuse would have ended.
	const FSpikeInteractionState TooLate = MakeDefuse(Detonation - 6.999, DefuseTime, 0.f);
	SIM_EXPECT_TRUE(Evaluate(TooLate, true, Detonation, Detonation + 0.005) == ETickOutcome::Detonate);

	// No defuser: detonates exactly on time, not before.
	SIM_EXPECT_TRUE(Evaluate(FSpikeInteractionState(), true, Detonation, Detonation - 0.001) == ETickOutcome::None);
	SIM_EXPECT_TRUE(Evaluate(FSpikeInteractionState(), true, Detonation, Detonation) == ETickOutcome::Detonate);

	// Not planted: nothing to detonate.
	SIM_EXPECT_TRUE(Evaluate(FSpikeInteractionState(), false, Detonation, Detonation + 10.0) == ETickOutcome::None);
}

// ---------------------------------------------------------------------------------------
// Round rules
// ---------------------------------------------------------------------------------------

SIM_TEST(Rounds, SidesAndMatchPoint)
{
	using namespace RoundRules;
	SIM_EXPECT_TRUE(GetAttackingTeam(1, 12) == ETacticalTeam::TeamA);
	SIM_EXPECT_TRUE(GetAttackingTeam(12, 12) == ETacticalTeam::TeamA);
	SIM_EXPECT_TRUE(GetAttackingTeam(13, 12) == ETacticalTeam::TeamB);

	SIM_EXPECT_TRUE(HasTeamWonMatch(13, 0, 13));
	SIM_EXPECT_TRUE(HasTeamWonMatch(13, 11, 13));
	SIM_EXPECT_FALSE(HasTeamWonMatch(13, 12, 13)); // Overtime.
	SIM_EXPECT_FALSE(HasTeamWonMatch(12, 12, 13));
	SIM_EXPECT_TRUE(HasTeamWonMatch(14, 12, 13));
	SIM_EXPECT_FALSE(HasTeamWonMatch(14, 13, 13));
	SIM_EXPECT_TRUE(HasTeamWonMatch(17, 15, 13));
}

SIM_TEST(Rounds, EliminationRules)
{
	using namespace RoundRules;
	const ETacticalTeam A = ETacticalTeam::TeamA, B = ETacticalTeam::TeamB, None = ETacticalTeam::Spectator;
	SIM_EXPECT_TRUE(EvaluateElimination(3, 2, false, A) == None);
	SIM_EXPECT_TRUE(EvaluateElimination(3, 0, false, A) == A);
	SIM_EXPECT_TRUE(EvaluateElimination(0, 2, false, A) == B);
	SIM_EXPECT_TRUE(EvaluateElimination(0, 2, true, A) == None); // Planted: defenders must still defuse.
	SIM_EXPECT_TRUE(EvaluateElimination(1, 0, true, A) == A);
	SIM_EXPECT_TRUE(EvaluateElimination(0, 0, false, A) == B);   // Trade, no plant: defenders.
	SIM_EXPECT_TRUE(EvaluateElimination(0, 0, true, A) == A);    // Trade after plant: attackers.
	SIM_EXPECT_TRUE(EvaluateElimination(0, 3, false, B) == A);   // Second half: B attacks.
}

SIM_TEST(Rounds, PhaseGatesAndTimerTransitions)
{
	using namespace RoundRules;
	using P = ETacticalMatchPhase;
	struct FRow { P Phase; bool bMoveLocked, bCombat, bShop, bBarriers; };
	for (const FRow R : { FRow{ P::PreMatch, true, false, false, true }, FRow{ P::BuyPhase, true, false, true, true },
		FRow{ P::BarrierPhase, false, false, true, true }, FRow{ P::ActionPhase, false, true, false, false },
		FRow{ P::PostRound, false, false, false, false }, FRow{ P::MatchEnded, true, false, false, false } })
	{
		SIM_EXPECT_EQ(IsMovementLocked(R.Phase), R.bMoveLocked);
		SIM_EXPECT_EQ(IsCombatAllowed(R.Phase), R.bCombat);
		SIM_EXPECT_EQ(IsShopOpen(R.Phase), R.bShop);
		SIM_EXPECT_EQ(AreBarriersUp(R.Phase), R.bBarriers);
	}
	SIM_EXPECT_TRUE(OnPhaseTimerExpired(P::PreMatch, false) == EPhaseTimerAction::StartNewRound);
	SIM_EXPECT_TRUE(OnPhaseTimerExpired(P::BuyPhase, false) == EPhaseTimerAction::EnterBarrierPhase);
	SIM_EXPECT_TRUE(OnPhaseTimerExpired(P::BarrierPhase, false) == EPhaseTimerAction::EnterActionPhase);
	SIM_EXPECT_TRUE(OnPhaseTimerExpired(P::ActionPhase, false) == EPhaseTimerAction::DefendersWinOnTime);
	SIM_EXPECT_TRUE(OnPhaseTimerExpired(P::ActionPhase, true) == EPhaseTimerAction::None); // Fuse owns the round.
	SIM_EXPECT_TRUE(OnPhaseTimerExpired(P::PostRound, false) == EPhaseTimerAction::FinishPostRound);
	SIM_EXPECT_TRUE(OnPhaseTimerExpired(P::MatchEnded, false) == EPhaseTimerAction::None);
}

// ---------------------------------------------------------------------------------------
// Virtual matches: a driver that plays ATacticalGameMode's orchestration (same switch, same rule
// calls) with random but legal round events: kills, plants, partial and full defuses.
// ---------------------------------------------------------------------------------------

namespace
{
	struct FMatchStats
	{
		int32 Rounds = 0;
		int32 Overtimes = 0;
		int32 Detonations = 0;
		int32 Defuses = 0;
		int32 CheckpointSaves = 0; // Defuses that finished thanks to a banked 50%.
		int32 TimeoutWins = 0;
		int32 EliminationWins = 0;
		int32 Violations = 0;
	};

	void PlayMatch(FRandomStream& Rng, FMatchStats& Stats, int32& OutScoreA, int32& OutScoreB)
	{
		using namespace RoundRules;
		using P = ETacticalMatchPhase;
		const int32 RoundsToWin = 13, Halftime = 12;
		int32 Score[2] = { 0, 0 };
		int32 Round = 0;
		P Phase = P::PreMatch;
		double Now = 0.0;
		ETacticalTeam PreviousAttackers = ETacticalTeam::TeamA;

		auto EndRound = [&](ETacticalTeam Winner)
		{
			if (Phase != P::ActionPhase) { ++Stats.Violations; } // Rounds only resolve in ActionPhase.
			++Score[TeamToIndex(Winner)];
			Phase = P::PostRound;
		};

		for (int32 Guard = 0; Guard < 10000 && Phase != P::MatchEnded; ++Guard)
		{
			switch (OnPhaseTimerExpired(Phase, false))
			{
			case EPhaseTimerAction::StartNewRound:
			{
				++Round;
				++Stats.Rounds;
				const ETacticalTeam Attackers = GetAttackingTeam(Round, Halftime);
				if (Round == Halftime + 1 && Attackers == PreviousAttackers) { ++Stats.Violations; }
				if (Round != Halftime + 1 && Round > 1 && Attackers != PreviousAttackers) { ++Stats.Violations; }
				PreviousAttackers = Attackers;
				Phase = P::BuyPhase;
				if (Score[0] >= RoundsToWin - 1 && Score[1] >= RoundsToWin - 1) { ++Stats.Overtimes; }
				break;
			}
			case EPhaseTimerAction::EnterBarrierPhase: Phase = P::BarrierPhase; break;
			case EPhaseTimerAction::EnterActionPhase:
			{
				Phase = P::ActionPhase;
				const ETacticalTeam Attackers = GetAttackingTeam(Round, Halftime);
				const ETacticalTeam Defenders = GetOpposingTeam(Attackers);
				int32 AliveAtk = 5, AliveDef = 5;
				bool bPlanted = false;
				double Detonation = 0.0;
				float Banked = 0.f;
				Now = 0.0;
				ETacticalTeam Winner = ETacticalTeam::Spectator;

				// Fight until something resolves the round.
				while (Winner == ETacticalTeam::Spectator)
				{
					Now += 2.0 + Rng.FRand() * 14.0; // Engagements are spread over the 100 s round timer.
					const float Roll = Rng.FRand();
					if (!bPlanted && Now >= 100.0) { Winner = Defenders; ++Stats.TimeoutWins; break; } // Round timer.
					if (!bPlanted && AliveAtk > 0 && Roll < 0.12f)
					{
						const FSpikeInteractionState Plant = MakePlant(Now, PlantTime);
						if (Evaluate(Plant, false, 0.0, Now + PlantTime) == ETickOutcome::PlantComplete)
						{
							bPlanted = true;
							Now += PlantTime;
							Detonation = Now + 45.0;
						}
					}
					else if (bPlanted && AliveDef > 0 && Roll < 0.3f)
					{
						// A defender tries: sometimes interrupted (banking the checkpoint), sometimes finishes.
						const FSpikeInteractionState Defuse = MakeDefuse(Now, DefuseTime, Banked);
						const double HeldUntil = Now + (Rng.FRand() < 0.55f ? Rng.FRand() * 6.0 : 10.0);
						const ETickOutcome Outcome = Evaluate(Defuse, true, Detonation, std::min(HeldUntil, Defuse.GetCompletionTime()));
						if (Outcome == ETickOutcome::DefuseComplete)
						{
							Winner = Defenders; ++Stats.Defuses; Stats.CheckpointSaves += Banked > 0.f ? 1 : 0;
						}
						else if (Evaluate(Defuse, true, Detonation, HeldUntil) == ETickOutcome::Detonate)
						{
							Winner = Attackers; ++Stats.Detonations;
						}
						else
						{
							Banked = BankDefuseCheckpoint(Defuse, HeldUntil, Banked, Checkpoint);
							Now = HeldUntil;
							--AliveDef; // Interrupted by a kill.
						}
					}
					else
					{
						(Rng.FRand() < 0.5f ? AliveAtk : AliveDef) -= 1;
						AliveAtk = std::max(AliveAtk, 0);
						AliveDef = std::max(AliveDef, 0);
					}
					if (Winner == ETacticalTeam::Spectator && bPlanted && Now >= Detonation) { Winner = Attackers; ++Stats.Detonations; }
					if (Winner == ETacticalTeam::Spectator)
					{
						const ETacticalTeam ByElimination = EvaluateElimination(AliveAtk, AliveDef, bPlanted, Attackers);
						if (ByElimination != ETacticalTeam::Spectator) { Winner = ByElimination; ++Stats.EliminationWins; }
					}
				}
				EndRound(Winner);
				break;
			}
			case EPhaseTimerAction::FinishPostRound:
				Phase = (HasTeamWonMatch(Score[0], Score[1], RoundsToWin) || HasTeamWonMatch(Score[1], Score[0], RoundsToWin))
					? P::MatchEnded : P::PreMatch; // PreMatch -> StartNewRound on the next tick (same as a direct restart).
				break;
			default:
				++Stats.Violations;
				Phase = P::MatchEnded;
				break;
			}
		}
		OutScoreA = Score[0];
		OutScoreB = Score[1];
	}
}

SIM_TEST(Rounds, ThousandVirtualMatchesEndLegally)
{
	FRandomStream Rng(2024);
	FMatchStats Stats;
	int32 Matches = 0;
	int32 OvertimeMatches = 0;
	for (; Matches < 1000; ++Matches)
	{
		int32 A = 0, B = 0;
		PlayMatch(Rng, Stats, A, B);
		const int32 Winner = std::max(A, B), Loser = std::min(A, B);
		const bool bRegulation = Winner == 13 && Loser <= 11;
		const bool bOvertime = Winner >= 13 && Loser >= 12 && Winner - Loser == 2;
		SIM_EXPECT(bRegulation || bOvertime, "final score %d-%d", A, B);
		OvertimeMatches += bOvertime ? 1 : 0;
	}
	SIM_EXPECT_EQ(Stats.Violations, 0);
	// Every way a round can end must actually have been exercised.
	SIM_EXPECT_GT(Stats.EliminationWins, 0);
	SIM_EXPECT_GT(Stats.TimeoutWins, 0);
	SIM_EXPECT_GT(Stats.Detonations, 0);
	SIM_EXPECT_GT(Stats.Defuses, 0);
	SIM_EXPECT_GT(Stats.CheckpointSaves, 0);
	SIM_EXPECT_GT(OvertimeMatches, 0);
	SimTest::Report("%d matches, %d rounds, %d went to overtime; rule violations: %d",
		Matches, Stats.Rounds, OvertimeMatches, Stats.Violations);
	SimTest::Report("round endings: %d elimination, %d time-out, %d detonation, %d defuse (%d finished from a banked 50%%)",
		Stats.EliminationWins, Stats.TimeoutWins, Stats.Detonations, Stats.Defuses, Stats.CheckpointSaves);
}
