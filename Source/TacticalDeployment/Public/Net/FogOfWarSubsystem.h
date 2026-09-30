// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldCollision.h"
#if UE_WITH_IRIS
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Iris/ReplicationSystem/NetObjectGroupHandle.h"
#endif
#include "FogOfWarSubsystem.generated.h"

class ATacticalCharacter;
class APlayerController;

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Network Fog of War"))
class TACTICALDEPLOYMENT_API UTacticalFogOfWarSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	UPROPERTY(config, EditAnywhere, Category = "Fog of War")
	bool bEnableNetworkFogOfWar = true;

	/** Each viewer/target pair is re-traced every N server frames (2 = 64 Hz at 128 tick). */
	UPROPERTY(config, EditAnywhere, Category = "Fog of War", meta = (ClampMin = "1", ClampMax = "8"))
	int32 EvaluationStride = 2;

	/** Keep an enemy relevant this long after the last successful LOS test (hysteresis vs. pop-in/churn). */
	UPROPERTY(config, EditAnywhere, Category = "Fog of War", meta = (Units = "s"))
	float VisibilityGraceTime = 0.25f;

	/**
	 * Upper bound on how far ahead (viewer latency + stride) positions are extrapolated when
	 * testing LOS. Reveals enemies slightly *before* they become visible, so the client already
	 * has them when the corner is cleared: no peeker pop-in.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Fog of War", meta = (Units = "s"))
	float MaxRevealLookahead = 0.15f;

	/** Target sample points are pushed out by capsule radius * this (optimistic silhouette). */
	UPROPERTY(config, EditAnywhere, Category = "Fog of War", meta = (ClampMin = "1"))
	float SilhouetteExpansion = 1.35f;

	UPROPERTY(config, EditAnywhere, Category = "Fog of War", meta = (Units = "cm"))
	float FootstepAudibleRange = 2800.f;

	UPROPERTY(config, EditAnywhere, Category = "Fog of War", meta = (Units = "cm"))
	float GunfireAudibleRange = 6000.f;

	/** How long a noise keeps its source relevant to in-range enemies. */
	UPROPERTY(config, EditAnywhere, Category = "Fog of War", meta = (Units = "s"))
	float NoiseMemoryTime = 0.4f;
};

/**
 * Server-side network fog of war (anti-ESP).
 *
 * Every EvaluationStride server frames, for every ordered (viewer, enemy) pair, fires a batch of
 * async line traces on the FogOcclusion channel (world geometry only) from the viewer's
 * latency-extrapolated eye to five points on the enemy's latency-extrapolated silhouette.
 * An enemy is relevant to a viewer when it is: a teammate, dead, recently visible (grace), or
 * audible (noise radius). Otherwise its replication to that client is culled entirely: the
 * client does not have the actor, so there is nothing for a wallhack to read.
 *
 * Output paths:
 *   Iris   - one exclusion group per character (character + weapons + carried spike);
 *            SetGroupFilterStatus per connection, pushed only on change.
 *   Legacy - ATacticalCharacter::IsNetRelevantFor queries IsRelevantTo().
 */
UCLASS()
class TACTICALDEPLOYMENT_API UTacticalFogOfWarSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterCharacter(ATacticalCharacter* Character);
	void UnregisterCharacter(ATacticalCharacter* Character);

	/** Weapons/spike changed hands: rebuild the character's Iris group membership. */
	void OnLoadoutChanged(ATacticalCharacter* Character);

	bool IsRelevantTo(const APlayerController* Viewer, const ATacticalCharacter* Target) const;

	//~ UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//~ FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	static constexpr int32 MaxSlots = 12;
	static constexpr int32 NumSamplePoints = 5;

	struct FTrackedCharacter
	{
		TWeakObjectPtr<ATacticalCharacter> Character;
#if UE_WITH_IRIS
		UE::Net::FNetObjectGroupHandle Group;
		TArray<UE::Net::FNetRefHandle, TInlineAllocator<4>> GroupMembers;
#endif
		bool bGroupDirty = true;
	};

	struct FPairState
	{
		double LastVisibleTime = -1.0e9;
		double DispatchTime = 0.0;
		uint16 Generation = 0;
		uint8 PendingTraces = 0;
	};

	struct FConnectionState
	{
		TWeakObjectPtr<APlayerController> PlayerController;
		uint32 ConnectionId = 0;
		TBitArray<> Applied;
		TBitArray<> Known;
	};

	int32 FindSlot(const ATacticalCharacter* Character) const;
	void ResetPairsForSlot(int32 Slot);

	/** The character whose eyes this connection sees through (own pawn, or spectated teammate). */
	const ATacticalCharacter* ResolveViewerCharacter(const APlayerController* Viewer) const;

	bool ComputeRelevancy(const ATacticalCharacter* ViewerCharacter, int32 ViewerSlot, const ATacticalCharacter* Target, int32 TargetSlot, double Now) const;

	float GetRevealLookahead(const ATacticalCharacter* Viewer) const;
	void DispatchTraces(double Now);
	void OnTraceCompleted(const FTraceHandle& Handle, FTraceDatum& Datum);

#if UE_WITH_IRIS
	void SyncIrisGroups();
	void PushIrisFilterStatus();
	void DestroyIrisGroup(FTrackedCharacter& Tracked);
#endif

	FTrackedCharacter Slots[MaxSlots];
	FPairState Pairs[MaxSlots * MaxSlots];
	TArray<FConnectionState> Connections;
	FTraceDelegate TraceDelegate;
	uint64 FrameCounter = 0;
};
