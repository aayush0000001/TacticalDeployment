// Copyright TacticalDeployment. All Rights Reserved.

#include "Net/FogOfWarSubsystem.h"
#include "Character/TacticalCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Core/TacticalTypes.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "Game/SpikeBase.h"
#include "Game/TacticalGameState.h"
#include "Game/TacticalPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Weapons/TacticalWeapon.h"

#if UE_WITH_IRIS
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Misc/EngineVersionComparison.h"
#include "Net/Iris/ReplicationSystem/ReplicationSystemUtil.h"
#endif

DECLARE_CYCLE_STAT(TEXT("FogOfWar Dispatch"), STAT_FogDispatch, STATGROUP_Tactical);
DECLARE_CYCLE_STAT(TEXT("FogOfWar Iris Push"), STAT_FogIrisPush, STATGROUP_Tactical);

namespace FogOfWar
{
	FORCEINLINE uint32 PackUserData(int32 PairIndex, uint16 Generation) { return (static_cast<uint32>(Generation) << 16) | static_cast<uint32>(PairIndex); }
	FORCEINLINE int32 UnpackPairIndex(uint32 UserData) { return static_cast<int32>(UserData & 0xFFFF); }
	FORCEINLINE uint16 UnpackGeneration(uint32 UserData) { return static_cast<uint16>(UserData >> 16); }

	FORCEINLINE ETacticalTeam GetControllerTeam(const APlayerController* PC)
	{
		const ATacticalPlayerState* PS = PC ? PC->GetPlayerState<ATacticalPlayerState>() : nullptr;
		return PS ? PS->GetTeam() : ETacticalTeam::Spectator;
	}
}

bool UTacticalFogOfWarSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTacticalFogOfWarSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TraceDelegate.BindUObject(this, &ThisClass::OnTraceCompleted);
}

void UTacticalFogOfWarSubsystem::Deinitialize()
{
#if UE_WITH_IRIS
	for (FTrackedCharacter& Tracked : Slots)
	{
		DestroyIrisGroup(Tracked);
	}
#endif
	TraceDelegate.Unbind();
	Super::Deinitialize();
}

TStatId UTacticalFogOfWarSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTacticalFogOfWarSubsystem, STATGROUP_Tactical);
}

// ---------------------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------------------

int32 UTacticalFogOfWarSubsystem::FindSlot(const ATacticalCharacter* Character) const
{
	for (int32 i = 0; i < MaxSlots; ++i)
	{
		if (Character && Slots[i].Character.Get() == Character)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void UTacticalFogOfWarSubsystem::RegisterCharacter(ATacticalCharacter* Character)
{
	if (!Character || FindSlot(Character) != INDEX_NONE)
	{
		return;
	}
	for (int32 i = 0; i < MaxSlots; ++i)
	{
		if (!Slots[i].Character.IsValid())
		{
			Slots[i] = FTrackedCharacter();
			Slots[i].Character = Character;
			ResetPairsForSlot(i);
			return;
		}
	}
	UE_LOG(LogTactical, Error, TEXT("FogOfWar: no free slot for %s."), *GetNameSafe(Character));
}

void UTacticalFogOfWarSubsystem::UnregisterCharacter(ATacticalCharacter* Character)
{
	const int32 Slot = FindSlot(Character);
	if (Slot == INDEX_NONE)
	{
		return;
	}
#if UE_WITH_IRIS
	DestroyIrisGroup(Slots[Slot]);
#endif
	Slots[Slot] = FTrackedCharacter();
	ResetPairsForSlot(Slot);
}

void UTacticalFogOfWarSubsystem::OnLoadoutChanged(ATacticalCharacter* Character)
{
	const int32 Slot = FindSlot(Character);
	if (Slot != INDEX_NONE)
	{
		Slots[Slot].bGroupDirty = true;
	}
}

void UTacticalFogOfWarSubsystem::ResetPairsForSlot(int32 Slot)
{
	// Bump generations so in-flight async results for the previous occupant are discarded.
	for (int32 Other = 0; Other < MaxSlots; ++Other)
	{
		for (FPairState* Pair : { &Pairs[Slot * MaxSlots + Other], &Pairs[Other * MaxSlots + Slot] })
		{
			const uint16 NextGeneration = static_cast<uint16>(Pair->Generation + 1);
			*Pair = FPairState();
			Pair->Generation = NextGeneration;
		}
	}
	for (FConnectionState& Connection : Connections)
	{
		Connection.Known[Slot] = false;
	}
}

// ---------------------------------------------------------------------------------------
// Relevancy decision
// ---------------------------------------------------------------------------------------

const ATacticalCharacter* UTacticalFogOfWarSubsystem::ResolveViewerCharacter(const APlayerController* Viewer) const
{
	if (!Viewer)
	{
		return nullptr;
	}
	const ETacticalTeam ViewerTeam = FogOfWar::GetControllerTeam(Viewer);

	// Dead players spectate a teammate and are entitled to exactly what that teammate sees.
	if (const ATacticalCharacter* ViewTarget = Cast<ATacticalCharacter>(Viewer->GetViewTarget()))
	{
		if (ViewTarget->IsAlive() && ViewTarget->GetTeam() == ViewerTeam)
		{
			return ViewTarget;
		}
	}
	const ATacticalCharacter* Pawn = Cast<ATacticalCharacter>(Viewer->GetPawn());
	return (Pawn && Pawn->IsAlive()) ? Pawn : nullptr;
}

bool UTacticalFogOfWarSubsystem::ComputeRelevancy(const ATacticalCharacter* ViewerCharacter, int32 ViewerSlot, const ATacticalCharacter* Target, int32 TargetSlot, double Now) const
{
	if (!Target->IsAlive())
	{
		return true; // Corpses carry no tactical information.
	}
	if (!ViewerCharacter)
	{
		return false; // Dead with nobody to spectate: no eyes, nothing to see.
	}
	if (ViewerCharacter->GetTeam() == Target->GetTeam())
	{
		return true;
	}

	const UTacticalFogOfWarSettings* Settings = GetDefault<UTacticalFogOfWarSettings>();

	// Seen recently (async LOS results, with hysteresis).
	if (ViewerSlot != INDEX_NONE && TargetSlot != INDEX_NONE)
	{
		const FPairState& Pair = Pairs[ViewerSlot * MaxSlots + TargetSlot];
		if (Now - Pair.LastVisibleTime <= Settings->VisibilityGraceTime)
		{
			return true;
		}
	}

	// Heard: the client needs the actor to spatialize footsteps/gunfire correctly.
	if (Now - Target->GetLastNoiseTime() <= Settings->NoiseMemoryTime)
	{
		const double DistSq = FVector::DistSquared(ViewerCharacter->GetActorLocation(), Target->GetActorLocation());
		if (DistSq <= FMath::Square(static_cast<double>(Target->GetLastNoiseRadius())))
		{
			return true;
		}
	}

	return false;
}

bool UTacticalFogOfWarSubsystem::IsRelevantTo(const APlayerController* Viewer, const ATacticalCharacter* Target) const
{
	if (!Target || !GetDefault<UTacticalFogOfWarSettings>()->bEnableNetworkFogOfWar)
	{
		return true;
	}

	const ETacticalTeam ViewerTeam = FogOfWar::GetControllerTeam(Viewer);
	if (ViewerTeam == ETacticalTeam::Spectator || ViewerTeam == Target->GetTeam())
	{
		return true; // Casters and teammates see everything / each other.
	}

	const ATacticalCharacter* ViewerCharacter = ResolveViewerCharacter(Viewer);
	return ComputeRelevancy(ViewerCharacter, FindSlot(ViewerCharacter), Target, FindSlot(Target), GetWorld()->GetTimeSeconds());
}

// ---------------------------------------------------------------------------------------
// Async LOS
// ---------------------------------------------------------------------------------------

float UTacticalFogOfWarSubsystem::GetRevealLookahead(const ATacticalCharacter* Viewer) const
{
	const UTacticalFogOfWarSettings* Settings = GetDefault<UTacticalFogOfWarSettings>();
	const APlayerState* PS = Viewer->GetPlayerState();
	const float OneWay = PS ? PS->GetPingInMilliseconds() * 0.0005f : 0.f;
	const float StrideTime = Settings->EvaluationStride * TacticalNet::ServerFrameTime;
	return FMath::Min(OneWay + StrideTime, Settings->MaxRevealLookahead);
}

void UTacticalFogOfWarSubsystem::DispatchTraces(double Now)
{
	SCOPE_CYCLE_COUNTER(STAT_FogDispatch);

	UWorld* World = GetWorld();
	const UTacticalFogOfWarSettings* Settings = GetDefault<UTacticalFogOfWarSettings>();
	const int32 Stride = FMath::Max(1, Settings->EvaluationStride);

	// Simple collision is plenty for occlusion, and far cheaper than complex.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FogOfWarLOS), /*bTraceComplex*/ false);

	for (int32 ViewerSlot = 0; ViewerSlot < MaxSlots; ++ViewerSlot)
	{
		const ATacticalCharacter* Viewer = Slots[ViewerSlot].Character.Get();
		if (!Viewer || !Viewer->IsAlive())
		{
			continue;
		}

		const float Lookahead = GetRevealLookahead(Viewer);
		const FVector Eye = Viewer->GetPawnViewLocation() + Viewer->GetVelocity() * Lookahead;

		for (int32 TargetSlot = 0; TargetSlot < MaxSlots; ++TargetSlot)
		{
			const ATacticalCharacter* Target = Slots[TargetSlot].Character.Get();
			if (!Target || !Target->IsAlive() || Target->GetTeam() == Viewer->GetTeam())
			{
				continue;
			}

			const int32 PairIndex = ViewerSlot * MaxSlots + TargetSlot;
			FPairState& Pair = Pairs[PairIndex];
			// Stagger pairs across frames; never stack batches on a pair still awaiting results.
			if (((PairIndex + FrameCounter) % Stride) != 0 || Pair.PendingTraces > 0)
			{
				continue;
			}

			// Optimistic silhouette at the target's extrapolated position.
			const UCapsuleComponent* Capsule = Target->GetCapsuleComponent();
			const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
			const float Radius = Capsule->GetScaledCapsuleRadius() * Settings->SilhouetteExpansion;
			const FVector Center = Target->GetActorLocation() + Target->GetVelocity() * Lookahead;
			const FVector ToTarget = (Center - Eye).GetSafeNormal2D();
			const FVector Side = FVector::CrossProduct(ToTarget, FVector::UpVector) * Radius;
			const FVector Chest = Center + FVector(0.f, 0.f, HalfHeight * 0.35f);

			const FVector Samples[NumSamplePoints] =
			{
				Center + FVector(0.f, 0.f, HalfHeight - 8.f), // Head
				Chest,
				Chest + Side,                                  // Shoulders: first thing to clear a corner
				Chest - Side,
				Center - FVector(0.f, 0.f, HalfHeight - 12.f), // Feet (visible under boxes/doors)
			};

			Pair.PendingTraces = NumSamplePoints;
			Pair.DispatchTime = Now;
			const uint32 UserData = FogOfWar::PackUserData(PairIndex, Pair.Generation);
			for (const FVector& Sample : Samples)
			{
				// Test traces return blocked/not-blocked only: the cheapest query type.
				World->AsyncLineTraceByChannel(EAsyncTraceType::Test, Eye, Sample, TacticalCollision::FogOcclusion,
					Params, FCollisionResponseParams::DefaultResponseParam, &TraceDelegate, UserData);
			}
		}
	}
}

void UTacticalFogOfWarSubsystem::OnTraceCompleted(const FTraceHandle& Handle, FTraceDatum& Datum)
{
	const int32 PairIndex = FogOfWar::UnpackPairIndex(Datum.UserData);
	if (PairIndex < 0 || PairIndex >= MaxSlots * MaxSlots)
	{
		return;
	}

	FPairState& Pair = Pairs[PairIndex];
	if (Pair.Generation != FogOfWar::UnpackGeneration(Datum.UserData))
	{
		return; // Result for a previous slot occupant.
	}
	if (Pair.PendingTraces > 0)
	{
		--Pair.PendingTraces;
	}

	const bool bBlocked = Datum.OutHits.ContainsByPredicate([](const FHitResult& Hit) { return Hit.bBlockingHit; });
	if (!bBlocked)
	{
		Pair.LastVisibleTime = FMath::Max(Pair.LastVisibleTime, Pair.DispatchTime);
	}
}

// ---------------------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------------------

void UTacticalFogOfWarSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || World->GetNetMode() == NM_Standalone)
	{
		return;
	}
	if (!GetDefault<UTacticalFogOfWarSettings>()->bEnableNetworkFogOfWar)
	{
		return;
	}

	++FrameCounter;
	DispatchTraces(World->GetTimeSeconds());

#if UE_WITH_IRIS
	SyncIrisGroups();
	PushIrisFilterStatus();
#endif
}

// ---------------------------------------------------------------------------------------
// Iris exclusion groups. All Iris API usage is isolated here: group-creation signatures have
// shifted between engine minor versions (5.4 CreateGroup() vs. named groups in later versions).
// ---------------------------------------------------------------------------------------

#if UE_WITH_IRIS

void UTacticalFogOfWarSubsystem::DestroyIrisGroup(FTrackedCharacter& Tracked)
{
	if (!Tracked.Group.IsValid())
	{
		return;
	}
	if (UReplicationSystem* ReplicationSystem = UE::Net::FReplicationSystemUtil::GetReplicationSystem(Tracked.Character.Get()))
	{
		ReplicationSystem->DestroyGroup(Tracked.Group);
	}
	Tracked.Group = UE::Net::FNetObjectGroupHandle();
	Tracked.GroupMembers.Reset();
}

void UTacticalFogOfWarSubsystem::SyncIrisGroups()
{
	using namespace UE::Net;

	const ATacticalGameState* GameState = GetWorld()->GetGameState<ATacticalGameState>();
	const ASpikeBase* Spike = GameState ? GameState->GetSpike() : nullptr;

	for (int32 SlotIndex = 0; SlotIndex < MaxSlots; ++SlotIndex)
	{
		FTrackedCharacter& Tracked = Slots[SlotIndex];
		ATacticalCharacter* Character = Tracked.Character.Get();
		if (!Character || !Tracked.bGroupDirty)
		{
			continue;
		}

		UReplicationSystem* ReplicationSystem = FReplicationSystemUtil::GetReplicationSystem(Character);
		const FNetRefHandle CharacterHandle = FReplicationSystemUtil::GetNetRefHandle(Character);
		if (!ReplicationSystem || !CharacterHandle.IsValid())
		{
			continue; // Not replicating yet (or Iris disabled at runtime): retry next frame.
		}

		if (!Tracked.Group.IsValid())
		{
#if UE_VERSION_OLDER_THAN(5, 5, 0)
			Tracked.Group = ReplicationSystem->CreateGroup();
#else
			Tracked.Group = ReplicationSystem->CreateGroup(*FString::Printf(TEXT("FogOfWar_%s"), *Character->GetName()));
#endif
			ReplicationSystem->AddExclusionFilterGroup(Tracked.Group);
		}

		// Everything that would leak this character's position travels with it.
		TArray<FNetRefHandle, TInlineAllocator<4>> Desired;
		Desired.Add(CharacterHandle);
		bool bAllResolved = true;
		auto AddMember = [&](const UObject* Object)
		{
			const FNetRefHandle Handle = FReplicationSystemUtil::GetNetRefHandle(Object);
			if (Handle.IsValid())
			{
				Desired.AddUnique(Handle);
			}
			else
			{
				bAllResolved = false; // Spawned this frame; pick it up next frame.
			}
		};
		for (const ATacticalWeapon* Weapon : Character->GetInventory())
		{
			if (Weapon)
			{
				AddMember(Weapon);
			}
		}
		if (Spike && Spike->GetCarrier() == Character)
		{
			AddMember(Spike);
		}

		for (const FNetRefHandle& Existing : Tracked.GroupMembers)
		{
			if (!Desired.Contains(Existing))
			{
				ReplicationSystem->RemoveFromGroup(Tracked.Group, Existing);
			}
		}
		for (const FNetRefHandle& Wanted : Desired)
		{
			if (!Tracked.GroupMembers.Contains(Wanted))
			{
				ReplicationSystem->AddToGroup(Tracked.Group, Wanted);
			}
		}
		Tracked.GroupMembers = Desired;
		Tracked.bGroupDirty = !bAllResolved;

		for (FConnectionState& Connection : Connections)
		{
			Connection.Known[SlotIndex] = false; // Re-push status for the (re)built group.
		}
	}
}

void UTacticalFogOfWarSubsystem::PushIrisFilterStatus()
{
	using namespace UE::Net;
	SCOPE_CYCLE_COUNTER(STAT_FogIrisPush);

	UWorld* World = GetWorld();

	// Refresh the connection list (joins, leaves, reconnects).
	Connections.RemoveAllSwap([](const FConnectionState& Connection) { return !Connection.PlayerController.IsValid(); });
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		const UNetConnection* NetConnection = PC ? PC->GetNetConnection() : nullptr;
		if (!NetConnection)
		{
			continue; // Local player on a listen server: nothing to filter.
		}
		if (!Connections.ContainsByPredicate([PC](const FConnectionState& C) { return C.PlayerController.Get() == PC; }))
		{
			FConnectionState& Connection = Connections.AddDefaulted_GetRef();
			Connection.PlayerController = PC;
			Connection.ConnectionId = NetConnection->GetConnectionId();
			Connection.Applied.Init(false, MaxSlots);
			Connection.Known.Init(false, MaxSlots);
		}
	}

	const double Now = World->GetTimeSeconds();
	for (FConnectionState& Connection : Connections)
	{
		const APlayerController* PC = Connection.PlayerController.Get();
		const ETacticalTeam ViewerTeam = FogOfWar::GetControllerTeam(PC);
		const ATacticalCharacter* ViewerCharacter = ResolveViewerCharacter(PC);
		const int32 ViewerSlot = FindSlot(ViewerCharacter);

		for (int32 TargetSlot = 0; TargetSlot < MaxSlots; ++TargetSlot)
		{
			const FTrackedCharacter& Tracked = Slots[TargetSlot];
			const ATacticalCharacter* Target = Tracked.Character.Get();
			if (!Target || !Tracked.Group.IsValid())
			{
				continue;
			}

			const bool bAllow = ViewerTeam == ETacticalTeam::Spectator
				|| ViewerTeam == Target->GetTeam()
				|| ComputeRelevancy(ViewerCharacter, ViewerSlot, Target, TargetSlot, Now);

			if (!Connection.Known[TargetSlot] || Connection.Applied[TargetSlot] != bAllow)
			{
				if (UReplicationSystem* ReplicationSystem = FReplicationSystemUtil::GetReplicationSystem(Target))
				{
					ReplicationSystem->SetGroupFilterStatus(Tracked.Group, Connection.ConnectionId, bAllow ? ENetFilterStatus::Allow : ENetFilterStatus::Disallow);
				}
				Connection.Applied[TargetSlot] = bAllow;
				Connection.Known[TargetSlot] = true;
			}
		}
	}
}

#endif // UE_WITH_IRIS
