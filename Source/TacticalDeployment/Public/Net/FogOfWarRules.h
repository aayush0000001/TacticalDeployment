// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

// Engine-light fog-of-war decisions: who may know about whom, where to test line of sight,
// and how noise is remembered. UTacticalFogOfWarSubsystem gathers inputs and runs the traces.

#include "CoreMinimal.h"

namespace FogOfWarRules
{
	inline constexpr int32 NumSamplePoints = 5;

	struct FRelevancyInputs
	{
		bool bTargetAlive = true;
		bool bHasViewer = true;       // False: dead with nobody to spectate.
		bool bSameTeam = false;
		double Now = 0.0;
		double LastVisibleTime = -1.0e9;
		double LastNoiseTime = -1.0e9;
		float LastNoiseRadius = 0.f;
		double ViewerToTargetDistSq = 0.0;
		float VisibilityGraceTime = 0.25f;
		float NoiseMemoryTime = 0.4f;
	};

	/**
	 *   teammate / corpse                          -> relevant
	 *   no eyes (dead, nobody to spectate)         -> culled
	 *   seen within the grace window               -> relevant (hysteresis: no churn at corners)
	 *   heard: recent noise and within its radius  -> relevant (client must spatialize it)
	 *   otherwise                                  -> culled
	 */
	inline bool IsRelevant(const FRelevancyInputs& In)
	{
		if (!In.bTargetAlive)
		{
			return true; // Corpses carry no tactical information.
		}
		if (!In.bHasViewer)
		{
			return false;
		}
		if (In.bSameTeam)
		{
			return true;
		}
		if (In.Now - In.LastVisibleTime <= In.VisibilityGraceTime)
		{
			return true;
		}
		return In.Now - In.LastNoiseTime <= In.NoiseMemoryTime
			&& In.ViewerToTargetDistSq <= FMath::Square(static_cast<double>(In.LastNoiseRadius));
	}

	struct FRevealLookahead
	{
		float Viewer = 0.f; // Seconds to extrapolate the viewer's eye.
		float Target = 0.f; // Seconds to extrapolate the target.
	};

	/**
	 * How far ahead to extrapolate positions so an enemy is relevant *before* it can be seen.
	 *
	 * Evaluation latency: a pair waits up to Stride frames for its turn, and async results land
	 * one frame later.
	 * Viewer: a peeking client predicts its own movement, so it is ahead of the server by the
	 * upstream leg, and the enemy data it needs still has to travel the downstream leg. That is a
	 * full RTT on top of the evaluation latency.
	 * Target: the viewer renders the target from server snapshots, so only the evaluation latency matters.
	 */
	inline FRevealLookahead ComputeRevealLookahead(float ViewerRoundTripSeconds, int32 EvaluationStride, float ServerFrameTime, float MaxLookahead)
	{
		const float EvaluationLatency = static_cast<float>(EvaluationStride + 1) * ServerFrameTime;
		FRevealLookahead Lookahead;
		Lookahead.Viewer = FMath::Min(ViewerRoundTripSeconds + EvaluationLatency, MaxLookahead);
		Lookahead.Target = FMath::Min(EvaluationLatency, MaxLookahead);
		return Lookahead;
	}

	/** Pairs are staggered across frames so each frame traces 1/Stride of them. */
	inline bool ShouldEvaluatePair(int32 PairIndex, uint64 FrameCounter, int32 EvaluationStride)
	{
		return ((static_cast<uint64>(PairIndex) + FrameCounter) % static_cast<uint64>(FMath::Max(1, EvaluationStride))) == 0;
	}

	/**
	 * Optimistic silhouette of a standing target as seen from Eye: head, chest, both shoulders
	 * (pushed out by Radius, the first thing to clear a corner) and feet.
	 */
	inline void BuildSamplePoints(const FVector& Eye, const FVector& TargetCenter, float HalfHeight, float Radius, FVector (&OutPoints)[NumSamplePoints])
	{
		const FVector ToTarget = (TargetCenter - Eye).GetSafeNormal2D();
		const FVector Side = FVector::CrossProduct(ToTarget, FVector::UpVector) * Radius;
		const FVector Chest = TargetCenter + FVector(0.f, 0.f, HalfHeight * 0.35f);

		OutPoints[0] = TargetCenter + FVector(0.f, 0.f, HalfHeight - 8.f);
		OutPoints[1] = Chest;
		OutPoints[2] = Chest + Side;
		OutPoints[3] = Chest - Side;
		OutPoints[4] = TargetCenter - FVector(0.f, 0.f, HalfHeight - 12.f);
	}

	/**
	 * Records a noise. A quieter noise (footsteps) never shrinks a louder one (gunshot) that is
	 * still inside the memory window.
	 */
	inline void AccumulateNoise(double& InOutLastTime, float& InOutLastRadius, double Now, float Radius, float NoiseMemoryTime)
	{
		const bool bLouderNoiseExpired = (Now - InOutLastTime) > NoiseMemoryTime;
		if (bLouderNoiseExpired || Radius >= InOutLastRadius)
		{
			InOutLastRadius = Radius;
			InOutLastTime = Now;
		}
	}
}
