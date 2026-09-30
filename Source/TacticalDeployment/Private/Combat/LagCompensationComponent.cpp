// Copyright TacticalDeployment. All Rights Reserved.

#include "Combat/LagCompensationComponent.h"
#include "Character/TacticalCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

DECLARE_CYCLE_STAT(TEXT("LagComp Record"), STAT_LagCompRecord, STATGROUP_Tactical);
DECLARE_CYCLE_STAT(TEXT("LagComp Rewind"), STAT_LagCompRewind, STATGROUP_Tactical);
DECLARE_CYCLE_STAT(TEXT("LagComp Trace"), STAT_LagCompTrace, STATGROUP_Tactical);

ULagCompensationComponent::ULagCompensationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // Enabled on authority in BeginPlay.
	// After animation and physics: bone transforms for this frame are final.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetIsReplicatedByDefault(false);
}

void ULagCompensationComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!GetOwner()->HasAuthority())
	{
		return; // Clients never rewind.
	}

	History.Allocate();
	FMemory::Memzero(RewoundPoses, sizeof(RewoundPoses));
	SetComponentTickEnabled(true);
}

void ULagCompensationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RecordFrame(GetWorld()->GetTimeSeconds());
}

void ULagCompensationComponent::RegisterCharacter(ATacticalCharacter* Character)
{
	if (!Character)
	{
		return;
	}

	int32 FreeSlot = INDEX_NONE;
	for (int32 i = 0; i < LagCompensation::MaxTrackedCharacters; ++i)
	{
		if (Slots[i].Character == Character)
		{
			return;
		}
		if (FreeSlot == INDEX_NONE && !Slots[i].Character.IsValid())
		{
			FreeSlot = i;
		}
	}

	if (FreeSlot == INDEX_NONE)
	{
		UE_LOG(LogTacticalHitReg, Error, TEXT("LagComp: no free slot for %s (max %d)."), *GetNameSafe(Character), LagCompensation::MaxTrackedCharacters);
		return;
	}

	Slots[FreeSlot] = FTrackedSlot();
	Slots[FreeSlot].Character = Character;
}

void ULagCompensationComponent::UnregisterCharacter(ATacticalCharacter* Character)
{
	for (FTrackedSlot& Slot : Slots)
	{
		if (Slot.Character == Character)
		{
			Slot = FTrackedSlot();
		}
	}
}

void ULagCompensationComponent::ResolveBoneIndices(FTrackedSlot& Slot) const
{
	ATacticalCharacter* Character = Slot.Character.Get();
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	Slot.BoneIndices.Reset();
	Slot.ResolvedForMesh = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Mesh)
	{
		return;
	}

	const TArray<FHitboxDefinition>& Definitions = Character->GetHitboxDefinitions();
	const int32 Count = FMath::Min(Definitions.Num(), LagCompensation::MaxHitboxesPerCharacter);
	for (int32 i = 0; i < Count; ++i)
	{
		const int32 BoneIndex = Mesh->GetBoneIndex(Definitions[i].BoneName);
		UE_CLOG(BoneIndex == INDEX_NONE, LogTacticalHitReg, Error, TEXT("LagComp: bone '%s' not found on %s."),
			*Definitions[i].BoneName.ToString(), *GetNameSafe(Mesh->GetSkeletalMeshAsset()));
		Slot.BoneIndices.Add(BoneIndex);
	}
}

void ULagCompensationComponent::RecordCharacter(FTrackedSlot& Slot, FCharacterPoseRecord& Out) const
{
	Out.bValid = false;
	Out.NumHitboxes = 0;

	ATacticalCharacter* Character = Slot.Character.Get();
	if (!Character || !Character->IsAlive())
	{
		return;
	}

	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (!Mesh)
	{
		return;
	}
	if (Mesh->GetSkeletalMeshAsset() != Slot.ResolvedForMesh.Get())
	{
		ResolveBoneIndices(Slot);
	}

	// Requires the server mesh to refresh bones every frame (see ATacticalCharacter::PostInitializeComponents).
	const TArray<FTransform>& ComponentSpace = Mesh->GetComponentSpaceTransforms();
	const FTransform& ComponentToWorld = Mesh->GetComponentTransform();
	const TArray<FHitboxDefinition>& Definitions = Character->GetHitboxDefinitions();

	for (int32 i = 0; i < Slot.BoneIndices.Num(); ++i)
	{
		const int32 BoneIndex = Slot.BoneIndices[i];
		if (!ComponentSpace.IsValidIndex(BoneIndex))
		{
			continue;
		}

		const FHitboxDefinition& Def = Definitions[i];
		const FTransform BoneWorld = ComponentSpace[BoneIndex] * ComponentToWorld;
		const FTransform HitboxWorld = FTransform(Def.LocalRotation, Def.LocalOffset) * BoneWorld;

		// Snapshots carry their own shape and zone: a rewound trace never reads the live character.
		FHitboxSnapshot& Snap = Out.Hitboxes[Out.NumHitboxes++];
		Snap.Center = FVector3f(HitboxWorld.GetLocation());
		Snap.Rotation = FQuat4f(HitboxWorld.GetRotation());
		Snap.Radius = Def.Radius;
		Snap.HalfSegment = Def.HalfSegment;
		Snap.Zone = Def.Zone;
	}

	TacticalHitboxMath::FinalizePose(Out);
}

void ULagCompensationComponent::RecordFrame(double ServerTime)
{
	SCOPE_CYCLE_COUNTER(STAT_LagCompRecord);

	FFrameRecord& Frame = History.AddFrame(ServerTime);
	for (int32 i = 0; i < LagCompensation::MaxTrackedCharacters; ++i)
	{
		RecordCharacter(Slots[i], Frame.Characters[i]);
	}
}

double ULagCompensationComponent::GetNewestRecordTime() const
{
	return History.GetNewestTime();
}

double ULagCompensationComponent::ResolveRewindTime(const APlayerController* Shooter, double ClientViewTime) const
{
	// APlayerState ping is a smoothed round-trip time.
	const APlayerState* PlayerState = Shooter ? Shooter->PlayerState.Get() : nullptr;
	const double RoundTrip = PlayerState ? PlayerState->GetPingInMilliseconds() * 0.001 : 0.0;

	bool bAcceptedClientTime = false;
	const double RewindTo = TacticalRewind::ResolveRewindTime(GetWorld()->GetTimeSeconds(), RoundTrip, ClientViewTime,
		TacticalNet::ProxyInterpolationDelay, ClientTimeTolerance + TacticalNet::ServerFrameTime, MaxRewindTime, &bAcceptedClientTime);

	UE_CLOG(!bAcceptedClientTime, LogTacticalHitReg, Verbose, TEXT("Rejected client view time %.4f (rtt %.1f ms) for %s."),
		ClientViewTime, RoundTrip * 1000.0, *GetNameSafe(Shooter));
	return RewindTo;
}

void ULagCompensationComponent::BeginRewind(double Time, const ATacticalCharacter* Shooter) const
{
	SCOPE_CYCLE_COUNTER(STAT_LagCompRewind);
	checkf(!bRewindActive, TEXT("Nested lag compensation scopes are not supported."));
	bRewindActive = true;

	for (FCharacterPoseRecord& Pose : RewoundPoses)
	{
		Pose.bValid = false;
	}

	const FFrameRecord* Older = nullptr;
	const FFrameRecord* Newer = nullptr;
	float Alpha = 0.f;
	if (!History.FindBracket(Time, Older, Newer, Alpha))
	{
		return;
	}

	const ETacticalTeam ShooterTeam = Shooter ? Shooter->GetTeam() : ETacticalTeam::Spectator;
	for (int32 i = 0; i < LagCompensation::MaxTrackedCharacters; ++i)
	{
		const ATacticalCharacter* Target = Slots[i].Character.Get();
		if (!Target || Target == Shooter || Target->GetTeam() == ShooterTeam)
		{
			continue; // Friendly fire off: teammates are not part of the rewound world.
		}
		TacticalHitboxMath::LerpPose(Older->Characters[i], Newer->Characters[i], Alpha, RewoundPoses[i]);
	}
}

void ULagCompensationComponent::EndRewind() const
{
	bRewindActive = false;
}

// ---------------------------------------------------------------------------------------

FScopedLagCompensation::FScopedLagCompensation(const ULagCompensationComponent& InComponent, double InRewindTime, const ATacticalCharacter* Shooter)
	: Component(InComponent)
	, RewindTime(InRewindTime)
{
	Component.BeginRewind(RewindTime, Shooter);
}

FScopedLagCompensation::~FScopedLagCompensation()
{
	Component.EndRewind();
}

bool FScopedLagCompensation::LineTrace(const FVector& Start, const FVector& End, FRewoundHit& OutHit) const
{
	SCOPE_CYCLE_COUNTER(STAT_LagCompTrace);

	const FVector Delta = End - Start;
	const float Length = static_cast<float>(Delta.Size());
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const FVector3f Dir = FVector3f(Delta / Length);

	float BestT = Length;
	int32 BestSlot = INDEX_NONE;
	int32 BestHitbox = INDEX_NONE;

	for (int32 SlotIndex = 0; SlotIndex < LagCompensation::MaxTrackedCharacters; ++SlotIndex)
	{
		if (!Component.Slots[SlotIndex].Character.IsValid())
		{
			continue;
		}
		float T = 0.f;
		const int32 Hitbox = TacticalHitboxMath::TracePose(Component.RewoundPoses[SlotIndex], Start, Dir, BestT, T);
		if (Hitbox != INDEX_NONE && T <= BestT)
		{
			BestT = T;
			BestSlot = SlotIndex;
			BestHitbox = Hitbox;
		}
	}

	if (BestSlot == INDEX_NONE)
	{
		return false;
	}

	OutHit.Character = Component.Slots[BestSlot].Character.Get();
	OutHit.Zone = Component.RewoundPoses[BestSlot].Hitboxes[BestHitbox].Zone;
	OutHit.Distance = BestT;
	OutHit.Location = Start + FVector(Dir) * BestT;
	return true;
}
