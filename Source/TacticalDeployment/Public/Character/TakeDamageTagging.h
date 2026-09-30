// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TakeDamageTagging.generated.h"

/** What a hit asks of the victim's movement. Comes from UWeaponStats::TaggingSlow/TaggingDuration. */
USTRUCT(BlueprintType)
struct FDamageTagParams
{
	GENERATED_BODY()

	/** Fraction of max speed removed at the moment of impact (0.7 = 70% slow). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tagging", meta = (ClampMin = "0", ClampMax = "1"))
	float SlowFraction = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tagging", meta = (Units = "s"))
	float Duration = 0.5f;
};

/**
 * Active tag, expressed on the victim's *move clock* (the owning client's CMC timestamps).
 *
 * Why the move clock: the server stamps the tag with the client timestamp of the last move it
 * processed, and both sides evaluate the ease-out against each move's own timestamp. Replaying
 * saved moves after a correction therefore reproduces the server's speed exactly; the only
 * correction a tag ever causes is the single one for moves already in flight when it landed.
 */
USTRUCT()
struct FTaggingState
{
	GENERATED_BODY()

	UPROPERTY()
	float StartMoveTime = -1.f;

	UPROPERTY()
	float Duration = 0.f;

	/** Quantized on the server *before* use so server and client evaluate identical values. */
	UPROPERTY()
	uint8 SlowQuantized = 0;

	void SetSlow(float SlowFraction) { SlowQuantized = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SlowFraction, 0.f, 1.f) * 255.f)); }
	float GetSlow() const { return SlowQuantized / 255.f; }

	/** Speed scalar in [1 - Slow, 1]: holds near full slow on impact, eases back to 1 over Duration. */
	float Evaluate(float MoveTime) const
	{
		const float Elapsed = MoveTime - StartMoveTime;
		if (StartMoveTime < 0.f || Duration <= 0.f || Elapsed < 0.f || Elapsed >= Duration)
		{
			return 1.f; // Also covers CMC timestamp resets (Elapsed goes hugely negative).
		}
		const float Recovery = FMath::SmoothStep(0.f, Duration, Elapsed);
		return 1.f - GetSlow() * (1.f - Recovery);
	}

	/**
	 * A new hit at Now (move clock). Stacking never weakens an active tag: the new slow is the
	 * larger of the hit's slow and what the current tag still applies; the ease-out restarts.
	 */
	static FTaggingState Stack(const FTaggingState& Current, float Now, float SlowFraction, float Duration)
	{
		const float RemainingSlow = 1.f - Current.Evaluate(Now);
		FTaggingState Next;
		Next.StartMoveTime = Now;
		Next.Duration = Duration;
		Next.SetSlow(FMath::Max(SlowFraction, RemainingSlow));
		return Next;
	}
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UTakeDamageTagging : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything that can be "tagged" (slowed) by bullet damage. Kept as a native interface so the
 * damage path does a single Cast<> and a virtual call, no reflection.
 */
class TACTICALDEPLOYMENT_API ITakeDamageTagging
{
	GENERATED_BODY()

public:
	/** Server only. Stacks by keeping the strongest remaining slow. */
	virtual void ApplyDamageTag(const FDamageTagParams& Params) = 0;

	/** Current multiplier applied to max movement speed. */
	virtual float GetTaggingSpeedScalar() const = 0;
};
