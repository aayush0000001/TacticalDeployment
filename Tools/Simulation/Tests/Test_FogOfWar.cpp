// Copyright TacticalDeployment. All Rights Reserved.
// Pillar 1: network fog of war. Rules from Net/FogOfWarRules.h, driven through a virtual corner.

#include "SimTest.h"
#include "Core/TacticalTypes.h"
#include "Net/FogOfWarRules.h"
#include "VirtualCharacter.h"
#include "VirtualWorld.h"

#include <functional>

using namespace FogOfWarRules;

SIM_TEST(FogOfWar, RelevancyTruthTable)
{
	FRelevancyInputs In;
	In.Now = 10.0;

	FRelevancyInputs Corpse = In; Corpse.bTargetAlive = false;
	SIM_EXPECT_TRUE(IsRelevant(Corpse));

	FRelevancyInputs NoEyes = In; NoEyes.bHasViewer = false;
	SIM_EXPECT_FALSE(IsRelevant(NoEyes));

	FRelevancyInputs Teammate = In; Teammate.bSameTeam = true;
	SIM_EXPECT_TRUE(IsRelevant(Teammate));

	SIM_EXPECT_FALSE(IsRelevant(In)); // Hidden, silent enemy.

	FRelevancyInputs Seen = In; Seen.LastVisibleTime = 10.0 - 0.25;
	SIM_EXPECT_TRUE(IsRelevant(Seen));
	Seen.LastVisibleTime = 10.0 - 0.26;
	SIM_EXPECT_FALSE(IsRelevant(Seen));

	FRelevancyInputs Heard = In; Heard.LastNoiseTime = 9.8; Heard.LastNoiseRadius = 2800.f; Heard.ViewerToTargetDistSq = 2700.0 * 2700.0;
	SIM_EXPECT_TRUE(IsRelevant(Heard));
	Heard.ViewerToTargetDistSq = 2900.0 * 2900.0;
	SIM_EXPECT_FALSE(IsRelevant(Heard)); // Out of earshot.
	Heard.ViewerToTargetDistSq = 100.0;
	Heard.LastNoiseTime = 9.5;
	SIM_EXPECT_FALSE(IsRelevant(Heard)); // Noise forgotten (0.5 s > 0.4 s memory).
}

SIM_TEST(FogOfWar, NoiseMemoryKeepsLoudestNoise)
{
	double LastTime = -1e9;
	float LastRadius = 0.f;
	AccumulateNoise(LastTime, LastRadius, 1.00, 6000.f, 0.4f); // Gunshot.
	AccumulateNoise(LastTime, LastRadius, 1.10, 2800.f, 0.4f); // Footsteps must not shrink it...
	SIM_EXPECT_NEAR(LastRadius, 6000.f, 0.0);
	SIM_EXPECT_NEAR(LastTime, 1.00, 1e-9);
	AccumulateNoise(LastTime, LastRadius, 1.45, 2800.f, 0.4f); // ...until the gunshot is forgotten.
	SIM_EXPECT_NEAR(LastRadius, 2800.f, 0.0);
	SIM_EXPECT_NEAR(LastTime, 1.45, 1e-9);
	AccumulateNoise(LastTime, LastRadius, 1.50, 6000.f, 0.4f); // A louder noise always wins.
	SIM_EXPECT_NEAR(LastRadius, 6000.f, 0.0);
}

SIM_TEST(FogOfWar, SamplePointsFormOptimisticSilhouette)
{
	FVector Points[NumSamplePoints];
	const FVector Eye(0, 0, 64);
	const FVector Target(1000, 0, 0);
	BuildSamplePoints(Eye, Target, 88.f, 34.f * 1.35f, Points);
	SIM_EXPECT_NEAR(Points[0].Z, 80.0, 1e-6);                 // Head
	SIM_EXPECT_NEAR(Points[4].Z, -76.0, 1e-6);                // Feet
	SIM_EXPECT_NEAR(std::abs(Points[2].Y), 34.0 * 1.35, 1e-4); // Shoulders perpendicular to the view ray
	SIM_EXPECT_NEAR(Points[2].Y, -Points[3].Y, 1e-6);
	SIM_EXPECT_NEAR(Points[2].X, 1000.0, 1e-6);
}

SIM_TEST(FogOfWar, PairSchedulingCoversEveryPairOncePerStride)
{
	for (int32 Stride = 1; Stride <= 4; ++Stride)
	{
		for (int32 Pair = 0; Pair < 144; ++Pair)
		{
			int32 Evaluations = 0;
			for (uint64 Frame = 0; Frame < uint64(Stride) * 10; ++Frame)
			{
				Evaluations += ShouldEvaluatePair(Pair, Frame, Stride) ? 1 : 0;
			}
			SIM_EXPECT_EQ(Evaluations, 10);
		}
	}
}

// ---------------------------------------------------------------------------------------
// Corner-peek simulation. Mirrors UTacticalFogOfWarSubsystem: 128 Hz frames, pairs evaluated
// every EvaluationStride frames, five async LOS traces per evaluation whose results arrive one
// frame later, then IsRelevant(). Ground truth is dense line of sight to the real capsule.
// ---------------------------------------------------------------------------------------

namespace
{
	constexpr float Frame = TacticalNet::ServerFrameTime;
	constexpr int32 Stride = 2;
	constexpr float Grace = 0.25f;
	constexpr float Expansion = 1.35f;

	FVirtualWorld MakeCornerWorld()
	{
		FVirtualWorld World;
		// A wall whose edge (Y = -100) is the corner being peeked.
		World.AddBox(FVector(400, -2000, -200), FVector(440, -100, 400), 2.f, "CornerWall");
		return World;
	}

	/** Dense ground truth: can the viewer's eye see any point of the target's capsule? */
	bool CanSeeCapsule(const FVirtualWorld& World, const FVector& Eye, const FVector& Center)
	{
		const double R = FVirtualCharacter::CapsuleRadius, H = FVirtualCharacter::CapsuleHalfHeight;
		for (int32 Ring = 0; Ring <= 8; ++Ring)
		{
			const double Z = -H + R + (2.0 * (H - R)) * Ring / 8.0;
			for (int32 Step = 0; Step < 24; ++Step)
			{
				const double A = 2.0 * UE_PI * Step / 24.0;
				if (!World.IsSegmentBlocked(Eye, Center + FVector(R * std::cos(A), R * std::sin(A), Z)))
				{
					return true;
				}
			}
		}
		return !World.IsSegmentBlocked(Eye, Center + FVector(0, 0, H)) || !World.IsSegmentBlocked(Eye, Center - FVector(0, 0, H));
	}

	struct FPeekOutcome
	{
		double FirstVisibleTime = -1.0;  // Ground truth, server clock.
		double FirstRelevantTime = -1.0; // Fog decision, server clock.
		double RequiredLead = 0.0;       // How early relevancy must start for data to arrive in time.
		double LeadMargin() const { return (FirstVisibleTime - FirstRelevantTime) - RequiredLead; }
	};

	using FLookaheadFn = std::function<FRevealLookahead(float Rtt)>;

	/**
	 * bViewerPeeks: the viewer strafes out past the corner toward a static enemy (the viewer's own
	 * client is ahead of the server, so data must be sent a full RTT early). Otherwise the enemy
	 * strafes out into the static viewer's view (data must be sent by the first visible frame).
	 */
	FPeekOutcome SimulatePeek(bool bViewerPeeks, float Rtt, const FLookaheadFn& Lookahead)
	{
		const FVirtualWorld World = MakeCornerWorld();
		FVirtualCharacter Viewer, Target;
		if (bViewerPeeks)
		{
			Viewer.Location = FVector(0, -200, 0);
			Viewer.Velocity = FVector(0, 675, 0);
			Target.Location = FVector(800, -600, 0);
		}
		else
		{
			Viewer.Location = FVector(0, -300, 0);
			Target.Location = FVector(800, -900, 0);
			Target.Velocity = FVector(0, 675, 0);
		}

		FPeekOutcome Out;
		Out.RequiredLead = bViewerPeeks ? Rtt : 0.0;
		const int32 PairIndex = 1;
		double LastVisibleTime = -1e9;
		double PendingDispatchTime = -1.0;
		bool bPendingVisible = false;

		for (int32 FrameIndex = 0; FrameIndex < 400; ++FrameIndex)
		{
			const double Now = FrameIndex * double(Frame);
			Viewer.Location = Viewer.Location + Viewer.Velocity * double(Frame);
			Target.Location = Target.Location + Target.Velocity * double(Frame);

			// Async results from last frame's dispatch land now.
			if (PendingDispatchTime >= 0.0)
			{
				if (bPendingVisible) { LastVisibleTime = std::max(LastVisibleTime, PendingDispatchTime); }
				PendingDispatchTime = -1.0;
			}

			if (ShouldEvaluatePair(PairIndex, uint64(FrameIndex), Stride))
			{
				const FRevealLookahead L = Lookahead(Rtt);
				const FVector Eye = Viewer.EyeLocation() + Viewer.Velocity * double(L.Viewer);
				const FVector Center = Target.Location + Target.Velocity * double(L.Target);
				FVector Samples[NumSamplePoints];
				BuildSamplePoints(Eye, Center, FVirtualCharacter::CapsuleHalfHeight, FVirtualCharacter::CapsuleRadius * Expansion, Samples);
				bPendingVisible = false;
				for (const FVector& Sample : Samples)
				{
					bPendingVisible |= !World.IsSegmentBlocked(Eye, Sample);
				}
				PendingDispatchTime = Now;
			}

			FRelevancyInputs In;
			In.Now = Now;
			In.LastVisibleTime = LastVisibleTime;
			In.VisibilityGraceTime = Grace;
			if (Out.FirstRelevantTime < 0.0 && IsRelevant(In)) { Out.FirstRelevantTime = Now; }
			if (Out.FirstVisibleTime < 0.0 && CanSeeCapsule(World, Viewer.EyeLocation(), Target.Location)) { Out.FirstVisibleTime = Now; }
			if (Out.FirstVisibleTime >= 0.0 && Out.FirstRelevantTime >= 0.0) { break; }
		}
		return Out;
	}

	FRevealLookahead CurrentFormula(float Rtt) { return ComputeRevealLookahead(Rtt, Stride, Frame, 0.2f); }

	/** The first design (RTT/2 + stride for both). Kept only as a comparison baseline. */
	FRevealLookahead LegacyFormula(float Rtt)
	{
		const float L = FMath::Min(Rtt * 0.5f + Stride * Frame, 0.15f);
		return { L, L };
	}
}

SIM_TEST(FogOfWar, EnemyPeekIsRevealedBeforeItIsVisible)
{
	for (const float Rtt : { 0.02f, 0.06f, 0.1f, 0.15f })
	{
		const FPeekOutcome Out = SimulatePeek(/*bViewerPeeks*/ false, Rtt, CurrentFormula);
		SIM_EXPECT_GE(Out.FirstVisibleTime, 0.0);
		SIM_EXPECT_GE(Out.LeadMargin(), 0.0);   // Never late.
		SIM_EXPECT_LE(Out.LeadMargin(), 0.12);  // And not absurdly early (information leak).
		SimTest::Report("enemy peeks, RTT %3.0f ms: visible at %.1f ms, relevant %.1f ms earlier (need >= 0)",
			Rtt * 1000.f, Out.FirstVisibleTime * 1000.0, (Out.FirstVisibleTime - Out.FirstRelevantTime) * 1000.0);
	}
}

SIM_TEST(FogOfWar, ViewerPeekIsRevealedAFullRttEarly)
{
	for (const float Rtt : { 0.02f, 0.06f, 0.1f, 0.15f })
	{
		const FPeekOutcome Current = SimulatePeek(/*bViewerPeeks*/ true, Rtt, CurrentFormula);
		const FPeekOutcome Legacy = SimulatePeek(/*bViewerPeeks*/ true, Rtt, LegacyFormula);
		SIM_EXPECT_GE(Current.LeadMargin(), 0.0);
		SimTest::Report("viewer peeks, RTT %3.0f ms: lead %.1f ms (need %.0f) | legacy RTT/2 formula: lead %.1f ms -> %s",
			Rtt * 1000.f, (Current.FirstVisibleTime - Current.FirstRelevantTime) * 1000.0, Rtt * 1000.f,
			(Legacy.FirstVisibleTime - Legacy.FirstRelevantTime) * 1000.0, Legacy.LeadMargin() >= 0.0 ? "in time" : "LATE (pop-in)");
	}
	// Beyond the cap (200 ms), reveals on your own peeks are late by design: report it.
	const FPeekOutcome HighPing = SimulatePeek(true, 0.25f, CurrentFormula);
	SimTest::Report("viewer peeks, RTT 250 ms (over the 200 ms cap): margin %.1f ms (negative = late, accepted trade-off)", HighPing.LeadMargin() * 1000.0);
}

SIM_TEST(FogOfWar, HiddenSilentEnemyIsNeverReplicated)
{
	// Shift-walking enemy patrols behind the wall for 5 s: never visible, never audible.
	const FVirtualWorld World = MakeCornerWorld();
	FVirtualCharacter Viewer, Target;
	Viewer.Location = FVector(0, -300, 0);
	Target.Location = FVector(800, -1200, 0);
	int32 RelevantFrames = 0;
	double LastVisibleTime = -1e9;
	for (int32 FrameIndex = 0; FrameIndex < 640; ++FrameIndex)
	{
		const double Now = FrameIndex * double(Frame);
		Target.Velocity = FVector(0, (FrameIndex / 128) % 2 == 0 ? 405.0 : -405.0, 0);
		Target.Location = Target.Location + Target.Velocity * double(Frame);
		const FRevealLookahead L = CurrentFormula(0.08f);
		FVector Samples[NumSamplePoints];
		const FVector Eye = Viewer.EyeLocation();
		BuildSamplePoints(Eye, Target.Location + Target.Velocity * double(L.Target), 88.f, 34.f * Expansion, Samples);
		for (const FVector& Sample : Samples)
		{
			if (!World.IsSegmentBlocked(Eye, Sample)) { LastVisibleTime = Now; }
		}
		FRelevancyInputs In;
		In.Now = Now;
		In.LastVisibleTime = LastVisibleTime;
		In.ViewerToTargetDistSq = FVector::DistSquared(Viewer.Location, Target.Location); // No noise: shift-walking.
		RelevantFrames += IsRelevant(In) ? 1 : 0;
	}
	SIM_EXPECT_EQ(RelevantFrames, 0);
	SimTest::Report("shift-walking enemy behind a wall for 5 s: replicated on %d of 640 frames", RelevantFrames);
}

SIM_TEST(FogOfWar, RunningFootstepsRevealOnlyWithinEarshot)
{
	for (const double Distance : { 1500.0, 2700.0, 2900.0, 4000.0 })
	{
		FRelevancyInputs In;
		In.Now = 5.0;
		In.LastNoiseTime = 5.0 - Frame; // Running: noise reported every frame.
		In.LastNoiseRadius = 2800.f;
		In.ViewerToTargetDistSq = Distance * Distance;
		const bool bRelevant = IsRelevant(In);
		SIM_EXPECT_EQ(bRelevant, Distance <= 2800.0);
		SimTest::Report("running enemy behind wall at %.0f m: %s", Distance / 100.0, bRelevant ? "replicated (audible)" : "culled");
	}
}

SIM_TEST(FogOfWar, GraceWindowPreventsRelevancyChurn)
{
	// Enemy jiggle-peeks: visible 3 frames, hidden 10 frames, repeatedly. Relevancy should hold
	// continuously (no destroy/re-create churn) and drop 250 ms after the last sighting.
	double LastVisibleTime = -1e9;
	int32 Flips = 0;
	bool bPrev = false;
	double DroppedAt = -1.0;
	for (int32 FrameIndex = 0; FrameIndex < 256; ++FrameIndex)
	{
		const double Now = FrameIndex * double(Frame);
		const bool bJiggling = FrameIndex < 128;
		if (bJiggling && (FrameIndex % 13) < 3) { LastVisibleTime = Now; }
		FRelevancyInputs In;
		In.Now = Now;
		In.LastVisibleTime = LastVisibleTime;
		const bool bRelevant = IsRelevant(In);
		if (bRelevant != bPrev) { ++Flips; if (!bRelevant) { DroppedAt = Now; } } // Initial state is "culled".
		bPrev = bRelevant;
	}
	SIM_EXPECT_EQ(Flips, 2); // On once, off once.
	SIM_EXPECT_NEAR(DroppedAt - LastVisibleTime, 0.25, Frame + 1e-9);
	SimTest::Report("jiggle-peek for 1 s: %d relevancy transitions (1 reveal + 1 cull), culled %.1f ms after last sighting",
		Flips, (DroppedAt - LastVisibleTime) * 1000.0);
}
