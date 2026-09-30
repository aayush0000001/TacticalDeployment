// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

// Engine-light spike timing rules (plant, defuse, 50% checkpoint, defuse-vs-detonation race).
// ASpikeBase owns validation and side effects; everything time-based lives here.

#include "CoreMinimal.h"
#include "SpikeRules.generated.h"

class ATacticalCharacter;

UENUM(BlueprintType)
enum class ESpikeInteraction : uint8
{
	None,
	Planting,
	Defusing,
};

/**
 * A hold-to-complete interaction expressed purely as timestamps: clients compute the progress
 * bar locally, the server replicates only on start/stop (zero bytes while the bar fills).
 */
USTRUCT(BlueprintType)
struct FSpikeInteractionState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Spike")
	ESpikeInteraction Type = ESpikeInteraction::None;

	UPROPERTY(BlueprintReadOnly, Category = "Spike")
	TObjectPtr<ATacticalCharacter> Interactor;

	UPROPERTY(BlueprintReadOnly, Category = "Spike")
	double StartServerTime = 0.0;

	/** Seconds from StartServerTime to completion (already reduced by a defuse checkpoint). */
	UPROPERTY(BlueprintReadOnly, Category = "Spike")
	float Duration = 0.f;

	/** Normalized progress at StartServerTime (0, or the 0.5 defuse checkpoint). */
	UPROPERTY(BlueprintReadOnly, Category = "Spike")
	float StartProgress = 0.f;

	double GetCompletionTime() const { return StartServerTime + Duration; }

	float GetProgress(double ServerNow) const
	{
		if (Type == ESpikeInteraction::None || Duration <= 0.f)
		{
			return StartProgress;
		}
		const float Alpha = FMath::Clamp(static_cast<float>((ServerNow - StartServerTime) / Duration), 0.f, 1.f);
		return StartProgress + (1.f - StartProgress) * Alpha;
	}
};

namespace SpikeRules
{
	enum class ETickOutcome : uint8
	{
		None,
		PlantComplete,
		DefuseComplete,
		Detonate,
	};

	/** Plant progress is never banked: every attempt starts from zero. */
	inline FSpikeInteractionState MakePlant(double ServerNow, float PlantDuration)
	{
		FSpikeInteractionState State;
		State.Type = ESpikeInteraction::Planting;
		State.StartServerTime = ServerNow;
		State.Duration = PlantDuration;
		State.StartProgress = 0.f;
		return State;
	}

	/** Resumes from the banked checkpoint: with 50% banked, a 7 s defuse needs 3.5 s. */
	inline FSpikeInteractionState MakeDefuse(double ServerNow, float DefuseDuration, float BankedCheckpoint)
	{
		FSpikeInteractionState State;
		State.Type = ESpikeInteraction::Defusing;
		State.StartServerTime = ServerNow;
		State.StartProgress = BankedCheckpoint;
		State.Duration = DefuseDuration * (1.f - BankedCheckpoint);
		return State;
	}

	/**
	 * Checkpoint after a defuse attempt ends at ServerNow (released, killed, or interrupted).
	 * Reaching CheckpointFraction banks it for the rest of this plant; it never goes backwards.
	 */
	inline float BankDefuseCheckpoint(const FSpikeInteractionState& Interaction, double ServerNow, float CurrentCheckpoint, float CheckpointFraction)
	{
		if (Interaction.Type != ESpikeInteraction::Defusing)
		{
			return CurrentCheckpoint;
		}
		return Interaction.GetProgress(ServerNow) >= CheckpointFraction - KINDA_SMALL_NUMBER
			? FMath::Max(CurrentCheckpoint, CheckpointFraction)
			: CurrentCheckpoint;
	}

	/**
	 * What must happen at ServerNow. Defuse vs. detonation is decided by comparing exact
	 * completion timestamps, never by which check runs first in a frame.
	 */
	inline ETickOutcome Evaluate(const FSpikeInteractionState& Interaction, bool bPlanted, double DetonationServerTime, double ServerNow)
	{
		if (Interaction.Type == ESpikeInteraction::Planting && ServerNow >= Interaction.GetCompletionTime())
		{
			return ETickOutcome::PlantComplete;
		}
		if (bPlanted && Interaction.Type == ESpikeInteraction::Defusing
			&& ServerNow >= Interaction.GetCompletionTime() && Interaction.GetCompletionTime() <= DetonationServerTime)
		{
			return ETickOutcome::DefuseComplete;
		}
		if (bPlanted && ServerNow >= DetonationServerTime)
		{
			return ETickOutcome::Detonate;
		}
		return ETickOutcome::None;
	}
}
