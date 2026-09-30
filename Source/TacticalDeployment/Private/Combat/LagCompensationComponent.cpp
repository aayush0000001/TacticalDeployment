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

namespace LagCompensationMath
{
	/** Entry distance along normalized Rd, 0 if Ro is inside, -1 on miss. */
	static float IntersectRaySphere(const FVector3f& Ro, const FVector3f& Rd, const FVector3f& Center, float Radius)
	{
		const FVector3f Oc = Ro - Center;
		const float B = Oc | Rd;
		const float C = (Oc | Oc) - Radius * Radius;
		if (C > 0.f && B > 0.f)
		{
			return -1.f; // Outside and pointing away.
		}
		const float H = B * B - C;
		if (H < 0.f)
		{
			return -1.f;
		}
		return FMath::Max(0.f, -B - FMath::Sqrt(H));
	}

	/** Ray vs. capsule (segment Pa-Pb swept by Radius). Entry distance, 0 if inside, -1 on miss. */
	static float IntersectRayCapsule(const FVector3f& Ro, const FVector3f& Rd, const FVector3f& Pa, const FVector3f& Pb, float Radius)
	{
		const FVector3f Ba = Pb - Pa;
		const float Baba = Ba | Ba;
		if (Baba < KINDA_SMALL_NUMBER)
		{
			return IntersectRaySphere(Ro, Rd, Pa, Radius);
		}

		if (FMath::PointDistToSegmentSquared(FVector(Ro), FVector(Pa), FVector(Pb)) <= FMath::Square(Radius))
		{
			return 0.f; // Muzzle inside the hitbox.
		}

		const FVector3f Oa = Ro - Pa;
		const float Bard = Ba | Rd;
		const float Baoa = Ba | Oa;
		const float A = Baba - Bard * Bard;

		if (A > KINDA_SMALL_NUMBER)
		{
			const float B = Baba * (Rd | Oa) - Baoa * Bard;
			const float C = Baba * (Oa | Oa) - Baoa * Baoa - Radius * Radius * Baba;
			const float H = B * B - A * C;
			if (H < 0.f)
			{
				return -1.f; // Misses the infinite cylinder, therefore both caps too.
			}
			const float T = (-B - FMath::Sqrt(H)) / A;
			const float Y = Baoa + T * Bard;
			if (T >= 0.f && Y > 0.f && Y < Baba)
			{
				return T; // Cylindrical body.
			}
		}

		// Hemispherical caps: nearest valid sphere entry.
		const float Ta = IntersectRaySphere(Ro, Rd, Pa, Radius);
		const float Tb = IntersectRaySphere(Ro, Rd, Pb, Radius);
		if (Ta < 0.f) { return Tb; }
		if (Tb < 0.f) { return Ta; }
		return FMath::Min(Ta, Tb);
	}

	static void LerpPose(const FCharacterPoseRecord& A, const FCharacterPoseRecord& B, float Alpha, FCharacterPoseRecord& Out)
	{
		// Alive in only one of the bracketing frames (spawn/death boundary): snap to the valid one.
		if (!A.bValid || !B.bValid || A.NumHitboxes != B.NumHitboxes)
		{
			const FCharacterPoseRecord& Source = (B.bValid && (Alpha >= 0.5f || !A.bValid)) ? B : A;
			FMemory::Memcpy(&Out, &Source, sizeof(FCharacterPoseRecord));
			return;
		}

		Out.bValid = true;
		Out.NumHitboxes = A.NumHitboxes;
		Out.BoundsCenter = FMath::Lerp(A.BoundsCenter, B.BoundsCenter, Alpha);
		Out.BoundsRadius = FMath::Max(A.BoundsRadius, B.BoundsRadius);
		for (int32 i = 0; i < A.NumHitboxes; ++i)
		{
			Out.Hitboxes[i].Center = FMath::Lerp(A.Hitboxes[i].Center, B.Hitboxes[i].Center, Alpha);
			Out.Hitboxes[i].Rotation = FQuat4f::FastLerp(A.Hitboxes[i].Rotation, B.Hitboxes[i].Rotation, Alpha).GetNormalized();
		}
	}
}

// ---------------------------------------------------------------------------------------

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

	History.SetNumZeroed(LagCompensation::HistoryCapacity);
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
	if (!Mesh || Mesh->GetSkeletalMeshAsset() != Slot.ResolvedForMesh.Get())
	{
		ResolveBoneIndices(Slot);
	}
	if (!Mesh)
	{
		return;
	}

	// Requires the server mesh to refresh bones every frame (see ATacticalCharacter::PostInitializeComponents).
	const TArray<FTransform>& ComponentSpace = Mesh->GetComponentSpaceTransforms();
	const FTransform& ComponentToWorld = Mesh->GetComponentTransform();
	const TArray<FHitboxDefinition>& Definitions = Character->GetHitboxDefinitions();

	FVector3f BoundsMin(TNumericLimits<float>::Max());
	FVector3f BoundsMax(-TNumericLimits<float>::Max());
	float MaxExtent = 0.f;

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

		FHitboxSnapshot& Snap = Out.Hitboxes[Out.NumHitboxes++];
		Snap.Center = FVector3f(HitboxWorld.GetLocation());
		Snap.Rotation = FQuat4f(HitboxWorld.GetRotation());

		BoundsMin = BoundsMin.ComponentMin(Snap.Center);
		BoundsMax = BoundsMax.ComponentMax(Snap.Center);
		MaxExtent = FMath::Max(MaxExtent, Def.Radius + Def.HalfSegment);
	}

	if (Out.NumHitboxes > 0)
	{
		Out.BoundsCenter = (BoundsMin + BoundsMax) * 0.5f;
		Out.BoundsRadius = (BoundsMax - BoundsMin).Size() * 0.5f + MaxExtent;
		Out.bValid = true;
	}
}

void ULagCompensationComponent::RecordFrame(double ServerTime)
{
	SCOPE_CYCLE_COUNTER(STAT_LagCompRecord);

	NewestIndex = (NewestIndex + 1) % LagCompensation::HistoryCapacity;
	NumRecorded = FMath::Min(NumRecorded + 1, LagCompensation::HistoryCapacity);

	FFrameRecord& Frame = History[NewestIndex];
	Frame.ServerTime = ServerTime;
	for (int32 i = 0; i < LagCompensation::MaxTrackedCharacters; ++i)
	{
		RecordCharacter(Slots[i], Frame.Characters[i]);
	}
}

const FFrameRecord& ULagCompensationComponent::GetFrame(int32 LogicalIndex) const
{
	check(LogicalIndex >= 0 && LogicalIndex < NumRecorded);
	const int32 Oldest = NewestIndex - (NumRecorded - 1);
	return History[(Oldest + LogicalIndex + LagCompensation::HistoryCapacity) % LagCompensation::HistoryCapacity];
}

double ULagCompensationComponent::GetNewestRecordTime() const
{
	return NumRecorded > 0 ? History[NewestIndex].ServerTime : 0.0;
}

double ULagCompensationComponent::ResolveRewindTime(const APlayerController* Shooter, double ClientViewTime) const
{
	const double Now = GetWorld()->GetTimeSeconds();

	// APlayerState ping is a smoothed round-trip time.
	const APlayerState* PlayerState = Shooter ? Shooter->PlayerState.Get() : nullptr;
	const double RoundTrip = PlayerState ? PlayerState->GetPingInMilliseconds() * 0.001 : 0.0;
	const double OneWay = RoundTrip * 0.5;

	// Downstream leg (snapshot age when rendered) + upstream leg (RPC flight) + proxy interpolation.
	const double Estimated = Now - OneWay - OneWay - TacticalNet::ProxyInterpolationDelay;

	double RewindTo = Estimated;
	if (FMath::Abs(ClientViewTime - Estimated) <= ClientTimeTolerance + TacticalNet::ServerFrameTime)
	{
		RewindTo = ClientViewTime; // Precise, and consistent with what we measured.
	}
	else
	{
		UE_LOG(LogTacticalHitReg, Verbose, TEXT("Rejected client view time %.4f (estimate %.4f, rtt %.1f ms) for %s."),
			ClientViewTime, Estimated, RoundTrip * 1000.0, *GetNameSafe(Shooter));
	}

	return FMath::Clamp(RewindTo, Now - MaxRewindTime, Now);
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
	if (NumRecorded == 0)
	{
		return;
	}

	// Binary search for the newest frame with ServerTime <= Time.
	int32 Lo = 0;
	int32 Hi = NumRecorded - 1;
	if (Time >= GetFrame(Hi).ServerTime)
	{
		Lo = Hi;
	}
	else if (Time <= GetFrame(0).ServerTime)
	{
		Hi = 0;
	}
	else
	{
		while (Hi - Lo > 1)
		{
			const int32 Mid = (Lo + Hi) / 2;
			(GetFrame(Mid).ServerTime <= Time ? Lo : Hi) = Mid;
		}
	}

	const FFrameRecord& Older = GetFrame(FMath::Min(Lo, Hi));
	const FFrameRecord& Newer = GetFrame(FMath::Max(Lo, Hi));
	const double Span = Newer.ServerTime - Older.ServerTime;
	const float Alpha = Span > UE_DOUBLE_SMALL_NUMBER ? static_cast<float>(FMath::Clamp((Time - Older.ServerTime) / Span, 0.0, 1.0)) : 1.f;

	const ETacticalTeam ShooterTeam = Shooter ? Shooter->GetTeam() : ETacticalTeam::Spectator;
	for (int32 i = 0; i < LagCompensation::MaxTrackedCharacters; ++i)
	{
		const ATacticalCharacter* Target = Slots[i].Character.Get();
		if (!Target || Target == Shooter || Target->GetTeam() == ShooterTeam)
		{
			continue; // Friendly fire off: teammates are not part of the rewound world.
		}
		LagCompensationMath::LerpPose(Older.Characters[i], Newer.Characters[i], Alpha, RewoundPoses[i]);
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
		const FCharacterPoseRecord& Pose = Component.RewoundPoses[SlotIndex];
		const ATacticalCharacter* Candidate = Component.Slots[SlotIndex].Character.Get();
		if (!Pose.bValid || !Candidate)
		{
			continue;
		}

		// Work relative to the pose centre: keeps float math precise and cheap.
		const FVector3f Origin = FVector3f(Start - FVector(Pose.BoundsCenter));

		// Broad phase.
		const float BoundsT = LagCompensationMath::IntersectRaySphere(Origin, Dir, FVector3f::ZeroVector, Pose.BoundsRadius);
		if (BoundsT < 0.f || BoundsT > BestT)
		{
			continue;
		}

		const TArray<FHitboxDefinition>& Definitions = Candidate->GetHitboxDefinitions();
		const TArray<int32, TFixedAllocator<LagCompensation::MaxHitboxesPerCharacter>>& BoneIndices = Component.Slots[SlotIndex].BoneIndices;

		// Hitboxes were recorded in definition order, skipping unresolved bones.
		int32 SnapshotIndex = 0;
		for (int32 DefIndex = 0; DefIndex < BoneIndices.Num() && SnapshotIndex < Pose.NumHitboxes; ++DefIndex)
		{
			if (BoneIndices[DefIndex] == INDEX_NONE)
			{
				continue;
			}
			const FHitboxDefinition& Def = Definitions[DefIndex];
			const FHitboxSnapshot& Snap = Pose.Hitboxes[SnapshotIndex++];

			const FVector3f Center = Snap.Center - Pose.BoundsCenter;
			const FVector3f Axis = Snap.Rotation.GetAxisZ() * Def.HalfSegment;
			const float T = LagCompensationMath::IntersectRayCapsule(Origin, Dir, Center - Axis, Center + Axis, Def.Radius);
			if (T >= 0.f && T < BestT)
			{
				BestT = T;
				BestSlot = SlotIndex;
				BestHitbox = DefIndex;
			}
		}
	}

	if (BestSlot == INDEX_NONE)
	{
		return false;
	}

	ATacticalCharacter* HitCharacter = Component.Slots[BestSlot].Character.Get();
	OutHit.Character = HitCharacter;
	OutHit.Zone = HitCharacter->GetHitboxDefinitions()[BestHitbox].Zone;
	OutHit.Distance = BestT;
	OutHit.Location = Start + FVector(Dir) * BestT;
	return true;
}
