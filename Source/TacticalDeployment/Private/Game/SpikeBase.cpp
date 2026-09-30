// Copyright TacticalDeployment. All Rights Reserved.

#include "Game/SpikeBase.h"
#include "Character/TacticalCharacter.h"
#include "Character/TacticalCharacterMovementComponent.h"
#include "Game/RoundRules.h"
#include "Game/TacticalGameMode.h"
#include "Game/TacticalGameState.h"
#include "Net/FogOfWarSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace SpikeHelpers
{
	static double GetServerNow(const UWorld* World)
	{
		const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
		return GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0);
	}
}

ASpikeBase::ASpikeBase()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // Server enables it while something is timing.

	PickupSphere = CreateDefaultSubobject<USphereComponent>(TEXT("PickupSphere"));
	PickupSphere->InitSphereRadius(60.f);
	PickupSphere->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	PickupSphere->SetGenerateOverlapEvents(true);
	RootComponent = PickupSphere;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(PickupSphere);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	bReplicates = true;
	SetReplicatingMovement(true); // Carries attachment replication while carried.
	// Always relevant when dropped/planted; while carried, IsNetRelevantFor (legacy) and the
	// carrier's Iris exclusion group hide it exactly like its carrier.
	bAlwaysRelevant = true;
}

void ASpikeBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASpikeBase, State);
	DOREPLIFETIME(ASpikeBase, Carrier);
	DOREPLIFETIME(ASpikeBase, Interaction);
	DOREPLIFETIME(ASpikeBase, DefuseCheckpoint);
	DOREPLIFETIME(ASpikeBase, DetonationServerTime);
}

void ASpikeBase::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		PickupSphere->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnPickupOverlap);
	}
}

bool ASpikeBase::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
	if (State == ESpikeState::Carried && Carrier)
	{
		return Carrier->IsNetRelevantFor(RealViewer, ViewTarget, SrcLocation);
	}
	return true;
}

// ---------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------

bool ASpikeBase::CanCharacterInteract(const ATacticalCharacter* Character) const
{
	const ATacticalGameState* GameState = GetWorld()->GetGameState<ATacticalGameState>();
	if (!Character || !Character->IsAlive() || !GameState || GameState->GetPhase() != ETacticalMatchPhase::ActionPhase)
	{
		return false;
	}
	if (!Character->GetCharacterMovement()->IsMovingOnGround())
	{
		return false;
	}

	switch (State)
	{
	case ESpikeState::Carried:
		return Carrier == Character && IsInsidePlantSite(Character->GetActorLocation());

	case ESpikeState::Planted:
		return Character->GetTeam() == GameState->GetDefendingTeam()
			&& FVector::DistSquared(Character->GetActorLocation(), GetActorLocation()) <= FMath::Square(DefuseRange + Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())
			&& (Interaction.Type == ESpikeInteraction::None || Interaction.Interactor == Character);

	default:
		return false;
	}
}

float ASpikeBase::GetInteractionProgress() const
{
	return Interaction.GetProgress(SpikeHelpers::GetServerNow(GetWorld()));
}

bool ASpikeBase::IsInsidePlantSite(const FVector& Location) const
{
	for (TActorIterator<ASpikeSiteVolume> It(GetWorld()); It; ++It)
	{
		if (It->ContainsPoint(Location))
		{
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------
// Server: carrying
// ---------------------------------------------------------------------------------------

void ASpikeBase::ServerGiveTo(ATacticalCharacter* NewCarrier)
{
	check(HasAuthority());
	if (!NewCarrier)
	{
		return;
	}
	Carrier = NewCarrier;
	SetOwner(NewCarrier);
	PickupSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetState(ESpikeState::Carried);
	AttachToCarrier();
	NotifyFogLoadoutChanged(NewCarrier);
}

void ASpikeBase::AttachToCarrier()
{
	if (Carrier)
	{
		AttachToComponent(Carrier->GetMesh3P(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, CarrySocket);
	}
}

void ASpikeBase::ServerDrop()
{
	ATacticalCharacter* FormerCarrier = Carrier;
	FVector DropLocation = FormerCarrier ? FormerCarrier->GetActorLocation() : GetActorLocation();

	// Settle on the floor under the carrier.
	FHitResult Floor;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SpikeDrop), false, FormerCarrier);
	if (GetWorld()->LineTraceSingleByChannel(Floor, DropLocation, DropLocation - FVector(0.f, 0.f, 500.f), ECC_WorldStatic, Params))
	{
		DropLocation = Floor.ImpactPoint;
	}

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorLocationAndRotation(DropLocation, FRotator::ZeroRotator);
	Carrier = nullptr;
	SetOwner(nullptr);
	SetState(ESpikeState::Dropped);
	PickupSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	NotifyFogLoadoutChanged(FormerCarrier);
}

void ASpikeBase::OnPickupOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ATacticalCharacter* Character = Cast<ATacticalCharacter>(OtherActor);
	const ATacticalGameState* GameState = GetWorld()->GetGameState<ATacticalGameState>();
	if (State == ESpikeState::Dropped && Character && Character->IsAlive() && GameState && Character->GetTeam() == GameState->GetAttackingTeam())
	{
		ServerGiveTo(Character);
	}
}

void ASpikeBase::ServerOnCharacterDied(ATacticalCharacter* Character)
{
	if (!HasAuthority())
	{
		return;
	}
	if (Interaction.Interactor == Character)
	{
		ServerStopInteraction(/*bBankCheckpoint*/ true); // A killed defuser still banks 50%.
	}
	if (State == ESpikeState::Carried && Carrier == Character)
	{
		ServerDrop();
	}
}

// ---------------------------------------------------------------------------------------
// Server: plant / defuse
// ---------------------------------------------------------------------------------------

bool ASpikeBase::ServerTryBeginInteract(ATacticalCharacter* Character)
{
	check(HasAuthority());
	if (Interaction.Type != ESpikeInteraction::None || !CanCharacterInteract(Character))
	{
		return false;
	}

	if (State == ESpikeState::Carried)
	{
		ServerBeginPlant(Character);
	}
	else
	{
		ServerBeginDefuse(Character);
	}
	return true;
}

void ASpikeBase::ServerBeginPlant(ATacticalCharacter* Planter)
{
	Interaction = SpikeRules::MakePlant(SpikeHelpers::GetServerNow(GetWorld()), PlantDuration);
	Interaction.Interactor = Planter;
	InteractionAnchor = Planter->GetActorLocation();
	SetActorTickEnabled(true);
}

void ASpikeBase::ServerBeginDefuse(ATacticalCharacter* Defuser)
{
	// Resume from the checkpoint: with 50% banked, 7 s becomes 3.5 s.
	Interaction = SpikeRules::MakeDefuse(SpikeHelpers::GetServerNow(GetWorld()), DefuseDuration, DefuseCheckpoint);
	Interaction.Interactor = Defuser;
	InteractionAnchor = Defuser->GetActorLocation();
	SetActorTickEnabled(true);
}

void ASpikeBase::ServerEndInteract(ATacticalCharacter* Character)
{
	if (Interaction.Interactor == Character)
	{
		ServerStopInteraction(/*bBankCheckpoint*/ true);
	}
}

void ASpikeBase::ServerStopInteraction(bool bBankCheckpoint)
{
	if (bBankCheckpoint)
	{
		// The 50% checkpoint: reaching 3.5 s of a 7 s defuse banks half, permanently for this plant.
		DefuseCheckpoint = SpikeRules::BankDefuseCheckpoint(Interaction, SpikeHelpers::GetServerNow(GetWorld()), DefuseCheckpoint, DefuseCheckpointFraction);
	}

	ATacticalCharacter* FormerInteractor = Interaction.Interactor;
	Interaction = FSpikeInteractionState();

	if (FormerInteractor)
	{
		FormerInteractor->Client_InteractionEnded(); // Release the client's predicted movement lock.
	}
	if (State != ESpikeState::Planted)
	{
		SetActorTickEnabled(false);
	}
}

bool ASpikeBase::IsInterruptionRequired(double Now) const
{
	const ATacticalCharacter* Interactor = Interaction.Interactor;
	const ATacticalGameState* GameState = GetWorld()->GetGameState<ATacticalGameState>();
	if (!Interactor || !Interactor->IsAlive() || !GameState || GameState->GetPhase() != ETacticalMatchPhase::ActionPhase)
	{
		return true;
	}
	// Server-side movement check: a client that suppresses its own movement lock gains nothing.
	if (FVector::DistSquared2D(Interactor->GetActorLocation(), InteractionAnchor) > FMath::Square(MaxInteractionDrift))
	{
		return true;
	}
	return Interaction.Type == ESpikeInteraction::Planting && Carrier != Interactor;
}

void ASpikeBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}

	const double Now = SpikeHelpers::GetServerNow(GetWorld());

	if (Interaction.Type != ESpikeInteraction::None && IsInterruptionRequired(Now))
	{
		ServerStopInteraction(/*bBankCheckpoint*/ true);
	}

	switch (SpikeRules::Evaluate(Interaction, State == ESpikeState::Planted, DetonationServerTime, Now))
	{
	case SpikeRules::ETickOutcome::PlantComplete:  ServerCompletePlant();  break;
	case SpikeRules::ETickOutcome::DefuseComplete: ServerCompleteDefuse(); break;
	case SpikeRules::ETickOutcome::Detonate:       ServerDetonate();       break;
	default: break;
	}
}

void ASpikeBase::ServerCompletePlant()
{
	ATacticalCharacter* Planter = Interaction.Interactor;
	ServerStopInteraction(/*bBankCheckpoint*/ false);

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	const float HalfHeight = Planter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	SetActorLocationAndRotation(Planter->GetActorLocation() - FVector(0.f, 0.f, HalfHeight), FRotator(0.f, Planter->GetActorRotation().Yaw, 0.f));
	Carrier = nullptr;
	SetOwner(nullptr);

	ATacticalGameMode* GameMode = GetWorld()->GetAuthGameMode<ATacticalGameMode>();
	DefuseCheckpoint = 0.f;
	DetonationServerTime = SpikeHelpers::GetServerNow(GetWorld()) + (GameMode ? GameMode->GetSpikeDetonationTime() : 45.f);
	SetState(ESpikeState::Planted);
	SetActorTickEnabled(true);
	NotifyFogLoadoutChanged(Planter);

	if (GameMode)
	{
		GameMode->OnSpikePlanted(Planter);
	}
}

void ASpikeBase::ServerCompleteDefuse()
{
	ATacticalCharacter* Defuser = Interaction.Interactor;
	ServerStopInteraction(/*bBankCheckpoint*/ false);
	SetState(ESpikeState::Defused);
	SetActorTickEnabled(false);

	if (ATacticalGameMode* GameMode = GetWorld()->GetAuthGameMode<ATacticalGameMode>())
	{
		GameMode->OnSpikeDefused(Defuser);
	}
}

void ASpikeBase::ServerDetonate()
{
	if (Interaction.Type != ESpikeInteraction::None)
	{
		ServerStopInteraction(/*bBankCheckpoint*/ false);
	}
	SetState(ESpikeState::Detonated);
	SetActorTickEnabled(false);

	// Resolve the round first so the blast's kills cannot flip the result.
	if (ATacticalGameMode* GameMode = GetWorld()->GetAuthGameMode<ATacticalGameMode>())
	{
		GameMode->OnSpikeDetonated();
	}

	// Walls do not protect you: no occlusion test, just radius.
	for (TActorIterator<ATacticalCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) <= FMath::Square(DetonationRadius))
		{
			It->TakeDamage(DetonationDamage, FDamageEvent(), nullptr, this);
		}
	}
}

void ASpikeBase::ServerOnRoundEnded()
{
	if (Interaction.Type != ESpikeInteraction::None)
	{
		ServerStopInteraction(/*bBankCheckpoint*/ false);
	}
	SetActorTickEnabled(false);
}

void ASpikeBase::SetState(ESpikeState NewState)
{
	State = NewState;
	OnRep_State(); // Server-side cosmetics / listen host.
}

void ASpikeBase::NotifyFogLoadoutChanged(ATacticalCharacter* Character) const
{
	if (UTacticalFogOfWarSubsystem* Fog = GetWorld()->GetSubsystem<UTacticalFogOfWarSubsystem>())
	{
		Fog->OnLoadoutChanged(Character); // Joins/leaves the carrier's Iris exclusion group.
	}
}

void ASpikeBase::OnRep_State()
{
	OnSpikeStateChanged(State);
}

void ASpikeBase::OnRep_Carrier()
{
	// Attachment itself replicates via movement replication; hide the world model from its carrier.
	Mesh->SetOwnerNoSee(Carrier != nullptr);
}

// ---------------------------------------------------------------------------------------
// ASpikeSiteVolume
// ---------------------------------------------------------------------------------------

ASpikeSiteVolume::ASpikeSiteVolume()
{
	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	Bounds->InitBoxExtent(FVector(400.f, 400.f, 200.f));
	Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Bounds;
	bReplicates = false; // Level-placed on both sides; nothing to sync.
}

bool ASpikeSiteVolume::ContainsPoint(const FVector& WorldLocation) const
{
	const FVector Local = Bounds->GetComponentTransform().InverseTransformPosition(WorldLocation);
	const FVector Extent = Bounds->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

// ---------------------------------------------------------------------------------------
// ATacticalSpawnBarrier
// ---------------------------------------------------------------------------------------

ATacticalSpawnBarrier::ATacticalSpawnBarrier()
{
	Barrier = CreateDefaultSubobject<UBoxComponent>(TEXT("Barrier"));
	Barrier->InitBoxExtent(FVector(20.f, 300.f, 300.f));
	Barrier->SetCollisionProfileName(TEXT("SpawnBarrier"));
	RootComponent = Barrier;
	bReplicates = false;
}

void ATacticalSpawnBarrier::BeginPlay()
{
	Super::BeginPlay();
	if (AGameStateBase* GameState = GetWorld()->GetGameState())
	{
		BindToGameState(GameState);
	}
	else
	{
		// Clients: the GameState arrives by replication after level actors begin play.
		GameStateSetHandle = GetWorld()->GameStateSetEvent.AddUObject(this, &ThisClass::BindToGameState);
	}
}

void ATacticalSpawnBarrier::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GameStateSetEvent.Remove(GameStateSetHandle);
	if (ATacticalGameState* GameState = GetWorld()->GetGameState<ATacticalGameState>())
	{
		GameState->OnPhaseChanged.Remove(PhaseChangedHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void ATacticalSpawnBarrier::BindToGameState(AGameStateBase* GameStateBase)
{
	ATacticalGameState* GameState = Cast<ATacticalGameState>(GameStateBase);
	if (!GameState || PhaseChangedHandle.IsValid())
	{
		return;
	}
	PhaseChangedHandle = GameState->OnPhaseChanged.AddWeakLambda(this, [this](ETacticalMatchPhase, ETacticalMatchPhase NewPhase)
	{
		ApplyPhase(NewPhase);
	});
	ApplyPhase(GameState->GetPhase());
}

void ATacticalSpawnBarrier::ApplyPhase(ETacticalMatchPhase Phase)
{
	Barrier->SetCollisionEnabled(RoundRules::AreBarriersUp(Phase) ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}
