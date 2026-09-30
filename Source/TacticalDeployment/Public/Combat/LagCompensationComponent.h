// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/TacticalTypes.h"
#include "LagCompensationComponent.generated.h"

class ATacticalCharacter;
class APlayerController;

/**
 * Authored per character class: one lightweight capsule (or sphere) glued to a bone.
 * Hit registration never touches the physics asset; these shapes are the hitboxes.
 */
USTRUCT(BlueprintType)
struct FHitboxDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Hitbox")
	FName BoneName;

	UPROPERTY(EditDefaultsOnly, Category = "Hitbox")
	EHitZone Zone = EHitZone::Body;

	UPROPERTY(EditDefaultsOnly, Category = "Hitbox", meta = (Units = "cm", ClampMin = "0"))
	float Radius = 10.f;

	/** Half the distance between the two sphere centres along the hitbox's local Z. 0 = sphere. */
	UPROPERTY(EditDefaultsOnly, Category = "Hitbox", meta = (Units = "cm", ClampMin = "0"))
	float HalfSegment = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Hitbox")
	FVector LocalOffset = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, Category = "Hitbox")
	FRotator LocalRotation = FRotator::ZeroRotator;
};

namespace LagCompensation
{
	inline constexpr int32 MaxTrackedCharacters = 12;    // 10 players + reconnect slack.
	inline constexpr int32 MaxHitboxesPerCharacter = 16; // Head, neck, 3 spine, 2x(upper/lower arm), pelvis, 2x(thigh/calf/foot).
	inline constexpr float HistorySeconds = 1.f;
	/** 1000 ms at 128 Hz = 128 frames, plus slack so a hitch never evicts the frame we need. */
	inline constexpr int32 HistoryCapacity = 136;
}

/** One hitbox in world space at one server frame (28 bytes; float precision is ample inside a tactical map). */
struct FHitboxSnapshot
{
	FVector3f Center;
	FQuat4f Rotation; // Capsule axis is local Z.
};

/** One character's hitbox set at one server frame. */
struct FCharacterPoseRecord
{
	FVector3f BoundsCenter;       // Broad-phase sphere.
	float BoundsRadius;
	uint8 NumHitboxes;
	bool bValid;                  // Slot occupied, alive and recorded this frame.
	FHitboxSnapshot Hitboxes[LagCompensation::MaxHitboxesPerCharacter];
};

/**
 * The rewind unit: every tracked character's hitboxes at one server frame, stamped with
 * server time. POD, fixed-size, lives in a pre-allocated ring buffer: zero allocations per tick.
 */
struct FFrameRecord
{
	double ServerTime;
	FCharacterPoseRecord Characters[LagCompensation::MaxTrackedCharacters];
};

/** Result of a hitscan against rewound hitboxes. */
struct FRewoundHit
{
	ATacticalCharacter* Character = nullptr;
	EHitZone Zone = EHitZone::None;
	FVector Location = FVector::ZeroVector;
	float Distance = 0.f; // Along the traced segment.
};

/**
 * Server-side rewind (lives on the GameState; server only, never replicated).
 *
 * Records every tracked character's hitboxes once per server frame in TG_PostUpdateWork
 * (after animation has produced final bone transforms) into a 1000 ms ring buffer.
 * Weapons evaluate shots through FScopedLagCompensation.
 */
UCLASS(ClassGroup = (Tactical), meta = (BlueprintSpawnableComponent))
class TACTICALDEPLOYMENT_API ULagCompensationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULagCompensationComponent();

	void RegisterCharacter(ATacticalCharacter* Character);
	void UnregisterCharacter(ATacticalCharacter* Character);

	/**
	 * Server time at which a shot must be evaluated.
	 *
	 * Total rewind = downstream leg (RTT/2: the snapshot the client rendered was already this old)
	 *              + upstream leg   (RTT/2: the fire RPC's trip back)
	 *              + proxy interpolation delay.
	 * The client stamps the precise view time it rendered; we accept it only if it agrees with our
	 * own RTT-based estimate, otherwise fall back to the estimate. Result is clamped to MaxRewindTime.
	 */
	double ResolveRewindTime(const APlayerController* Shooter, double ClientViewTime) const;

	double GetNewestRecordTime() const;

	//~ UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Fairness cap. The buffer holds 1000 ms, but rewinding that far means a 400 ms player can
	 * kill you after you reached cover. 350 ms covers ~99% of a regional ranked population.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Lag Compensation", meta = (Units = "s", ClampMax = "1.0"))
	float MaxRewindTime = 0.35f;

	/** Allowed disagreement between the client's stamped view time and the server's estimate. */
	UPROPERTY(EditDefaultsOnly, Category = "Lag Compensation", meta = (Units = "s"))
	float ClientTimeTolerance = 0.05f;

private:
	friend class FScopedLagCompensation;

	struct FTrackedSlot
	{
		TWeakObjectPtr<ATacticalCharacter> Character;
		TArray<int32, TFixedAllocator<LagCompensation::MaxHitboxesPerCharacter>> BoneIndices;
		TWeakObjectPtr<const UObject> ResolvedForMesh; // Re-resolve bone indices if the mesh asset changes.
	};

	void RecordFrame(double ServerTime);
	void RecordCharacter(FTrackedSlot& Slot, FCharacterPoseRecord& Out) const;
	void ResolveBoneIndices(FTrackedSlot& Slot) const;

	/** Logical index 0 = oldest recorded frame. */
	const FFrameRecord& GetFrame(int32 LogicalIndex) const;

	/** Builds RewoundPoses for every enemy of Shooter at Time. */
	void BeginRewind(double Time, const ATacticalCharacter* Shooter) const;
	void EndRewind() const;

	FTrackedSlot Slots[LagCompensation::MaxTrackedCharacters];

	TArray<FFrameRecord> History;
	int32 NewestIndex = INDEX_NONE;
	int32 NumRecorded = 0;

	/** Scratch "rewound world" used by FScopedLagCompensation. Mutable: rewinding is logically const. */
	mutable FCharacterPoseRecord RewoundPoses[LagCompensation::MaxTrackedCharacters];
	mutable bool bRewindActive = false;
};

/**
 * RAII rewind: constructor rewinds every enemy's hitboxes to RewindTime, LineTrace() performs
 * the hitscan against that historic state, destructor restores the present.
 *
 * "Restore" is free because we never mutate live components: the rewound set is a scratch copy
 * of analytic shapes. No physics scene writes, no bone refresh, no render-state dirtying, and the
 * live skeletal meshes are never observed in a rewound state by anything else in the frame.
 */
class TACTICALDEPLOYMENT_API FScopedLagCompensation : public FNoncopyable
{
public:
	FScopedLagCompensation(const ULagCompensationComponent& InComponent, double InRewindTime, const ATacticalCharacter* Shooter);
	~FScopedLagCompensation();

	/** Nearest rewound hitbox intersected by [Start, End]. */
	bool LineTrace(const FVector& Start, const FVector& End, FRewoundHit& OutHit) const;

	double GetRewindTime() const { return RewindTime; }

private:
	const ULagCompensationComponent& Component;
	double RewindTime;
};
