// Copyright TacticalDeployment. All Rights Reserved.

#include "Character/TacticalCharacter.h"
#include "Character/TacticalCharacterMovementComponent.h"
#include "Character/TacticalMovementTuning.h"
#include "Core/TacticalGameUserSettings.h"
#include "Weapons/TacticalWeapon.h"
#include "Weapons/WeaponStats.h"
#include "Game/TacticalGameMode.h"
#include "Game/TacticalGameState.h"
#include "Game/TacticalPlayerState.h"
#include "Game/SpikeBase.h"
#include "Net/FogOfWarSubsystem.h"
#include "Net/FogOfWarRules.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/DamageEvents.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

ATacticalCharacter::ATacticalCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UTacticalCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	Capsule->InitCapsuleSize(34.f, 88.f);
	// Bullets never touch the movement capsule: they hit lag-compensated hitboxes instead.
	Capsule->SetCollisionResponseToChannel(TacticalCollision::WeaponTrace, ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(TacticalCollision::FogOcclusion, ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(TacticalCollision::SpawnBarrier, ECR_Block);

	BaseEyeHeight = 64.f;
	CrouchedEyeHeight = 38.f;

	// Yaw follows the mouse exactly (no turn-in-place lag on hitboxes); pitch goes through the AimOffset.
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	// --- First-person camera: attached to the capsule, NOT to a head bone -------------------
	// Head-bone cameras inherit animation bob, which makes the view (and therefore aim)
	// non-deterministic relative to the server's eye position used for hitscan.
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(Capsule);
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, BaseEyeHeight));
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->SetFieldOfView(103.f);

	// --- Mesh1P: arms + viewmodel, owner only ------------------------------------------------
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh1P"));
	Mesh1P->SetupAttachment(FirstPersonCamera);
	Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	Mesh1P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh1P->SetCollisionResponseToAllChannels(ECR_Ignore);
	Mesh1P->SetGenerateOverlapEvents(false);
	// Only the owner ever renders it; nobody else (server included) should pay for its animation.
	Mesh1P->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	// Pulled toward the camera + scaled down: same screen footprint, but physically inside the
	// capsule so it can't poke through walls (Docs/ARCHITECTURE.md, Pillar 6).
	Mesh1P->SetRelativeLocation(FVector(-15.f, 0.f, -75.f));
	Mesh1P->SetRelativeScale3D(FVector(0.5f));
	// Engine-native first-person rendering: separate FOV/scale for the viewmodel.
	Mesh1P->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonCamera->bEnableFirstPersonFieldOfView = true;
	FirstPersonCamera->bEnableFirstPersonScale = true;
	FirstPersonCamera->FirstPersonFieldOfView = 74.f;
	FirstPersonCamera->FirstPersonScale = 0.6f;

	// --- Mesh3P: full body, everyone but the owner --------------------------------------------
	USkeletalMeshComponent* Mesh3P = GetMesh();
	Mesh3P->SetOwnerNoSee(true);
	Mesh3P->bCastHiddenShadow = true; // Owner still sees their own shadow.
	// No physics state while alive: bullets use analytic lag-compensated hitboxes, so the mesh's
	// physics bodies would only cost kinematic updates every frame. Ragdoll re-enables it on death.
	Mesh3P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh3P->SetCollisionResponseToChannel(TacticalCollision::WeaponTrace, ECR_Ignore);
	Mesh3P->SetCollisionResponseToChannel(TacticalCollision::FogOcclusion, ECR_Ignore);
	Mesh3P->SetGenerateOverlapEvents(false);
	Mesh3P->bUpdateOverlapsOnAnimationFinalize = false;
	Mesh3P->SetRelativeLocation(FVector(0.f, 0.f, -88.f));
	Mesh3P->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	Mesh3P->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	// --- Replication ----------------------------------------------------------------------------
	bReplicates = true;
	SetReplicatingMovement(true);
	// Tactical maps are ~150 m across: distance culling is meaningless, occlusion culling (fog) is not.
	SetNetCullDistanceSquared(FMath::Square(1000.f * 100.f));
	SetNetUpdateFrequency(TacticalNet::ServerTickRate);
	SetMinNetUpdateFrequency(TacticalNet::ServerTickRate * 0.5f);
	NetPriority = 3.f;

	// Default FRepMovement packs rotation into bytes (1.4 deg): far too coarse for yaw that
	// orients head hitboxes. 16-bit components cost 3 extra bytes per update.
	FRepMovement& RepMovement = GetReplicatedMovement_Mutable();
	RepMovement.RotationQuantizationLevel = ERotatorQuantization::ShortComponents;
	RepMovement.LocationQuantizationLevel = EVectorQuantization::RoundTwoDecimals;
	RepMovement.VelocityQuantizationLevel = EVectorQuantization::RoundOneDecimal;
}

UTacticalCharacterMovementComponent* ATacticalCharacter::GetTacticalMovement() const
{
	return CastChecked<UTacticalCharacterMovementComponent>(GetCharacterMovement());
}

void ATacticalCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;

	Params.Condition = COND_None;
	DOREPLIFETIME_WITH_PARAMS_FAST(ATacticalCharacter, CurrentWeapon, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(ATacticalCharacter, bIsDead, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(ATacticalCharacter, DeathInfo, Params);

	Params.Condition = COND_SkipOwner;
	DOREPLIFETIME_WITH_PARAMS_FAST(ATacticalCharacter, ReplicatedAimPitch, Params);

	Params.Condition = COND_OwnerOnly;
	DOREPLIFETIME_WITH_PARAMS_FAST(ATacticalCharacter, Inventory, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(ATacticalCharacter, ReplicatedTagging, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(ATacticalCharacter, Health, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(ATacticalCharacter, Armor, Params);
}

void ATacticalCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (GetNetMode() == NM_DedicatedServer)
	{
		// Hit registration reads Mesh3P bone transforms every frame. A dedicated server never
		// renders, so without this the pose would never be evaluated and hitboxes would freeze.
		USkeletalMeshComponent* Mesh3P = GetMesh();
		Mesh3P->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh3P->bEnableUpdateRateOptimizations = false; // URO would skip frames: hitboxes must be exact.
		// The server never ragdolls and never simulates the mesh: skip all physics bookkeeping.
		Mesh3P->bEnablePhysicsOnDedicatedServer = false;
		Mesh3P->KinematicBonesUpdateType = EKinematicBonesUpdateToPhysics::SkipAllBones;
		Mesh1P->SetComponentTickEnabled(false);
	}
}

void ATacticalCharacter::BeginPlay()
{
	Super::BeginPlay();

	Mesh1PBaseLocation = Mesh1P->GetRelativeLocation();
	Mesh1PBaseRotation = Mesh1P->GetRelativeRotation();

	if (HasAuthority())
	{
		Health = MaxHealth;
		MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalCharacter, Health, this);

		if (ATacticalGameState* GameState = GetWorld()->GetGameState<ATacticalGameState>())
		{
			GameState->GetLagCompensation()->RegisterCharacter(this);
		}
		if (UTacticalFogOfWarSubsystem* Fog = GetWorld()->GetSubsystem<UTacticalFogOfWarSubsystem>())
		{
			Fog->RegisterCharacter(this);
		}
	}
}

void ATacticalCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (ATacticalGameState* GameState = GetWorld()->GetGameState<ATacticalGameState>())
		{
			GameState->GetLagCompensation()->UnregisterCharacter(this);
		}
		if (UTacticalFogOfWarSubsystem* Fog = GetWorld()->GetSubsystem<UTacticalFogOfWarSubsystem>())
		{
			Fog->UnregisterCharacter(this);
		}
		for (ATacticalWeapon* Weapon : Inventory)
		{
			if (Weapon)
			{
				Weapon->Destroy();
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ATacticalCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority() && IsAlive())
	{
		// 16-bit aim pitch for simulated proxies; push model only sends it when it changes.
		if (Controller)
		{
			const uint16 Packed = TacticalQuantize::PackPitch(Controller->GetControlRotation().Pitch);
			if (Packed != ReplicatedAimPitch)
			{
				ReplicatedAimPitch = Packed;
				MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalCharacter, ReplicatedAimPitch, this);
			}
		}

		// Running footsteps are audible: the fog of war must reveal us to nearby enemies so their
		// client can spatialize the sound. Shift-walking is silent and stays hidden.
		const UTacticalCharacterMovementComponent* Movement = GetTacticalMovement();
		if (Movement->IsMovingOnGround() && Movement->Velocity.Size2D() > Movement->MaxShiftWalkSpeed + 10.f)
		{
			ReportNoise(GetDefault<UTacticalFogOfWarSettings>()->FootstepAudibleRange);
		}
	}

	if (IsLocallyControlled())
	{
		// Eye height follows crouch exactly: the camera must sit where the server's hitscan
		// origin (GetPawnViewLocation) is, or the crosshair and the bullet disagree.
		FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, BaseEyeHeight));

		// Visual kick chases the latest pattern offset, and relaxes back once the spray stops.
		ViewKickTarget = FMath::RInterpTo(ViewKickTarget, FRotator::ZeroRotator, DeltaSeconds, 6.f);
		ViewKick = FMath::RInterpTo(ViewKick, ViewKickTarget, DeltaSeconds, 30.f);

		UpdateViewmodel(DeltaSeconds);
	}

	if (!bTeamHighlightApplied && GetNetMode() != NM_DedicatedServer)
	{
		UpdateTeamHighlight();
	}
}

void ATacticalCharacter::UpdateViewmodel(float DeltaSeconds)
{
	if (DeltaSeconds <= 0.f || !IsAlive())
	{
		return;
	}

	// Look rate from the control rotation (never from the camera: the view kick must not feed back).
	const FRotator Control = GetControlRotation();
	FViewmodelMotionInput Input;
	Input.DeltaTime = DeltaSeconds;
	if (bHasLastControlRotation)
	{
		Input.LookYawRate = FRotator::NormalizeAxis(Control.Yaw - LastControlRotation.Yaw) / DeltaSeconds;
		Input.LookPitchRate = FRotator::NormalizeAxis(Control.Pitch - LastControlRotation.Pitch) / DeltaSeconds;
	}
	LastControlRotation = Control;
	bHasLastControlRotation = true;

	Input.SpeedRatio = GetVelocity().Size2D() / TacticalMovementTuning::RunSpeed;
	Input.bOnGround = GetCharacterMovement()->IsMovingOnGround();
	ViewmodelMotion.Update(Input, ViewmodelSettings);

	Mesh1P->SetRelativeLocationAndRotation(Mesh1PBaseLocation + ViewmodelMotion.GetLocationOffset(),
		Mesh1PBaseRotation + ViewmodelMotion.GetRotationOffset());
}

void ATacticalCharacter::UpdateTeamHighlight()
{
	if (IsLocallyControlled())
	{
		bTeamHighlightApplied = true; // Never outline yourself.
		return;
	}

	const APlayerController* LocalPC = GetWorld()->GetFirstPlayerController();
	const ATacticalPlayerState* LocalState = LocalPC ? LocalPC->GetPlayerState<ATacticalPlayerState>() : nullptr;
	if (!LocalState || !GetPlayerState())
	{
		return; // Retry next frame: PlayerStates replicate after pawns.
	}

	const bool bEnemy = GetTeam() != LocalState->GetTeam();
	USkeletalMeshComponent* Mesh3P = GetMesh();
	Mesh3P->SetRenderCustomDepth(true);
	Mesh3P->SetCustomDepthStencilValue(bEnemy ? EnemyStencilValue : AllyStencilValue);
	bTeamHighlightApplied = true;
}

void ATacticalCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	bTeamHighlightApplied = false; // Re-evaluate with the new team.
}

void ATacticalCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaTime, OutResult);
	// Visual-only recoil kick. ControlRotation (what we send to the server) is untouched, so
	// recoil control is purely the player's mouse compensation against the pattern.
	OutResult.Rotation += ViewKick;
}

FRotator ATacticalCharacter::GetBaseAimRotation() const
{
	if (Controller && (IsLocallyControlled() || HasAuthority()))
	{
		return Super::GetBaseAimRotation();
	}
	FRotator Aim = GetActorRotation(); // Yaw: 16-bit via FRepMovement::ShortComponents.
	Aim.Pitch = TacticalQuantize::UnpackPitch(ReplicatedAimPitch);
	return Aim;
}

ETacticalTeam ATacticalCharacter::GetTeam() const
{
	const ATacticalPlayerState* TacticalPlayerState = GetPlayerState<ATacticalPlayerState>();
	return TacticalPlayerState ? TacticalPlayerState->GetTeam() : ETacticalTeam::Spectator;
}

float ATacticalCharacter::GetEquippedMovementMultiplier() const
{
	const UWeaponStats* Stats = CurrentWeapon ? CurrentWeapon->GetStats() : nullptr;
	return Stats ? Stats->MovementSpeedMultiplier : 1.f;
}

// ---------------------------------------------------------------------------------------
// Weapons
// ---------------------------------------------------------------------------------------

ATacticalWeapon* ATacticalCharacter::ServerGiveWeapon(const UWeaponStats* Stats)
{
	if (!HasAuthority() || !Stats || !Stats->WeaponClass)
	{
		return nullptr;
	}

	// Deferred so Stats is set before the first replication and before BeginPlay.
	ATacticalWeapon* Weapon = GetWorld()->SpawnActorDeferred<ATacticalWeapon>(Stats->WeaponClass, GetActorTransform(), this, this,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Weapon->InitializeFromStats(Stats);
	Weapon->FinishSpawning(GetActorTransform());

	Inventory.Add(Weapon);
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalCharacter, Inventory, this);
	EquipWeapon(Weapon);
	return Weapon;
}

void ATacticalCharacter::EquipWeapon(ATacticalWeapon* Weapon)
{
	ATacticalWeapon* Previous = CurrentWeapon;
	if (Previous == Weapon)
	{
		return;
	}

	CurrentWeapon = Weapon;
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalCharacter, CurrentWeapon, this);
	OnRep_CurrentWeapon(Previous); // Server-side attach + equip timers.

	if (UTacticalFogOfWarSubsystem* Fog = GetWorld()->GetSubsystem<UTacticalFogOfWarSubsystem>())
	{
		Fog->OnLoadoutChanged(this); // Weapon joins our Iris exclusion group.
	}
}

void ATacticalCharacter::OnRep_CurrentWeapon(ATacticalWeapon* PreviousWeapon)
{
	if (PreviousWeapon)
	{
		PreviousWeapon->OnUnequipped();
	}
	if (CurrentWeapon)
	{
		CurrentWeapon->OnEquipped(this);
	}
}

void ATacticalCharacter::ServerSetArmor(float NewArmor)
{
	check(HasAuthority());
	Armor = NewArmor;
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalCharacter, Armor, this);
}

// ---------------------------------------------------------------------------------------
// Damage & tagging
// ---------------------------------------------------------------------------------------

float ATacticalCharacter::ApplyBulletDamage(float Damage, EHitZone Zone, const UWeaponStats* Weapon, AController* InstigatorController, AActor* DamageCauser,
	const FVector& ShotDirection, bool bWallbang)
{
	check(HasAuthority());
	if (!IsAlive() || Damage <= 0.f)
	{
		return 0.f;
	}

	LastDamageWeapon = Weapon;
	LastDamageZone = Zone;
	bLastDamageWallbang = bWallbang;
	const float HealthBefore = Health;

	// Armor soaks a fixed share until depleted.
	const float Absorbed = FMath::Min(Armor, Damage * ArmorAbsorption);
	if (Absorbed > 0.f)
	{
		Armor -= Absorbed;
		MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalCharacter, Armor, this);
	}

	Health = FMath::Max(0.f, Health - (Damage - Absorbed));
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalCharacter, Health, this);

	UE_LOG(LogTacticalHitReg, Verbose, TEXT("%s hit %s zone=%d dmg=%.1f hp=%.1f"),
		*GetNameSafe(InstigatorController), *GetName(), static_cast<int32>(Zone), Damage, Health);

	const float Dealt = (HealthBefore - Health) + Absorbed;
	if (Health <= 0.f)
	{
		DeathInfo.Direction = ShotDirection.GetSafeNormal();
		DeathInfo.Zone = Zone;
		MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalCharacter, DeathInfo, this);
		Die(InstigatorController);
		return Dealt;
	}

	if (Weapon)
	{
		FDamageTagParams TagParams;
		TagParams.SlowFraction = Weapon->TaggingSlow;
		TagParams.Duration = Weapon->TaggingDuration;
		// Through the interface so non-character taggables (e.g. future drones) share the path.
		if (ITakeDamageTagging* Taggable = Cast<ITakeDamageTagging>(this))
		{
			Taggable->ApplyDamageTag(TagParams);
		}
	}
	return Dealt;
}

float ATacticalCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// Non-bullet damage (spike detonation, falling). Bullets use ApplyBulletDamage.
	const float Actual = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (HasAuthority() && IsAlive() && Actual > 0.f)
	{
		LastDamageWeapon = nullptr;
		LastDamageZone = EHitZone::Body;
		bLastDamageWallbang = false;
		Health = FMath::Max(0.f, Health - Actual);
		MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalCharacter, Health, this);
		if (Health <= 0.f)
		{
			// Blown away from the source (spike detonation).
			DeathInfo.Direction = DamageCauser ? (GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal() : FVector::ZeroVector;
			DeathInfo.Zone = EHitZone::Body;
			MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalCharacter, DeathInfo, this);
			Die(EventInstigator);
		}
	}
	return Actual;
}

void ATacticalCharacter::ApplyDamageTag(const FDamageTagParams& Params)
{
	check(HasAuthority());
	ReplicatedTagging = GetTacticalMovement()->ServerApplyTag(Params);
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalCharacter, ReplicatedTagging, this);
}

float ATacticalCharacter::GetTaggingSpeedScalar() const
{
	return GetTacticalMovement()->GetTaggingSpeedScalar();
}

void ATacticalCharacter::OnRep_TaggingState()
{
	// Owning client adopts the tag; the next correction replays saved moves against it.
	GetTacticalMovement()->SetTaggingState(ReplicatedTagging);
}

void ATacticalCharacter::Die(AController* Killer)
{
	check(HasAuthority());
	if (bIsDead)
	{
		return;
	}

	bIsDead = true;
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalCharacter, bIsDead, this);

	if (ATacticalGameState* GameState = GetWorld()->GetGameState<ATacticalGameState>())
	{
		if (ASpikeBase* Spike = GameState->GetSpike())
		{
			Spike->ServerOnCharacterDied(this);
		}
	}

	OnRep_IsDead();
	OnDied.Broadcast(this, Killer);

	if (ATacticalGameMode* GameMode = GetWorld()->GetAuthGameMode<ATacticalGameMode>())
	{
		GameMode->OnCharacterKilled(this, Killer);
	}
}

void ATacticalCharacter::OnRep_IsDead()
{
	if (!bIsDead)
	{
		return;
	}

	const FVector InheritedVelocity = GetVelocity(); // Before movement is disabled.
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->DisableMovement();
	if (CurrentWeapon)
	{
		CurrentWeapon->StopFire();
	}

	if (GetNetMode() != NM_DedicatedServer)
	{
		USkeletalMeshComponent* Mesh3P = GetMesh();
		Mesh3P->SetOwnerNoSee(false); // Owner watches their own body from the death cam.
		Mesh3P->SetCollisionProfileName(TEXT("Ragdoll"));
		Mesh3P->SetSimulatePhysics(true);
		Mesh3P->SetAllPhysicsLinearVelocity(InheritedVelocity);
		const FVector Direction = FVector(DeathInfo.Direction);
		if (!Direction.IsNearlyZero())
		{
			// Thrown along the killing shot from the bone that took it.
			Mesh3P->AddImpulse(Direction * DeathImpulse, DeathInfo.Zone == EHitZone::Head ? HeadBoneName : BodyBoneName, /*bVelChange*/ true);
		}
		Mesh1P->SetHiddenInGame(true);
	}
}

// ---------------------------------------------------------------------------------------
// Fog of war
// ---------------------------------------------------------------------------------------

bool ATacticalCharacter::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
	// Legacy (non-Iris) replication path. Under Iris this is not called; the same decision is
	// pushed into exclusion groups by UTacticalFogOfWarSubsystem.
	if (IsOwnedBy(ViewTarget) || IsOwnedBy(RealViewer) || this == ViewTarget || ViewTarget == GetInstigator())
	{
		return true;
	}

	if (const UTacticalFogOfWarSubsystem* Fog = GetWorld()->GetSubsystem<UTacticalFogOfWarSubsystem>())
	{
		if (!Fog->IsRelevantTo(Cast<APlayerController>(RealViewer), this))
		{
			return false; // Occluded and inaudible: this client receives nothing about us.
		}
	}

	return Super::IsNetRelevantFor(RealViewer, ViewTarget, SrcLocation);
}

void ATacticalCharacter::ReportNoise(float Radius)
{
	if (!HasAuthority())
	{
		return;
	}
	FogOfWarRules::AccumulateNoise(LastNoiseTime, LastNoiseRadius, GetWorld()->GetTimeSeconds(), Radius,
		GetDefault<UTacticalFogOfWarSettings>()->NoiseMemoryTime);
}

void ATacticalCharacter::Landed(const FHitResult& Hit)
{
	if (IsLocallyControlled())
	{
		ViewmodelMotion.AddLanding(FMath::Abs(GetVelocity().Z), ViewmodelSettings);
	}
	Super::Landed(Hit);
	ReportNoise(LandingNoiseRadius);
}

// ---------------------------------------------------------------------------------------
// Spike interaction (plant/defuse). Movement lock is predicted through the move flags.
// ---------------------------------------------------------------------------------------

void ATacticalCharacter::Server_BeginInteract_Implementation()
{
	ATacticalGameState* GameState = GetWorld()->GetGameState<ATacticalGameState>();
	ASpikeBase* Spike = GameState ? GameState->GetSpike() : nullptr;
	if (!Spike || !Spike->ServerTryBeginInteract(this))
	{
		Client_InteractionEnded(); // Release the client's predicted movement lock.
	}
}

void ATacticalCharacter::Server_EndInteract_Implementation()
{
	ATacticalGameState* GameState = GetWorld()->GetGameState<ATacticalGameState>();
	if (ASpikeBase* Spike = GameState ? GameState->GetSpike() : nullptr)
	{
		Spike->ServerEndInteract(this);
	}
}

void ATacticalCharacter::Client_InteractionEnded_Implementation()
{
	GetTacticalMovement()->SetWantsInteractLock(false);
}

// ---------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------

void ATacticalCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();

	const APlayerController* PC = Cast<APlayerController>(GetController());
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
	{
		InputSubsystem->ClearAllMappings();
		if (DefaultMappingContext)
		{
			InputSubsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}
}

void ATacticalCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);
	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::Input_Move);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ThisClass::Input_Look);
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &ThisClass::Input_CrouchPressed);
	Input->BindAction(CrouchAction, ETriggerEvent::Completed, this, &ThisClass::Input_CrouchReleased);
	Input->BindAction(WalkAction, ETriggerEvent::Started, this, &ThisClass::Input_WalkPressed);
	Input->BindAction(WalkAction, ETriggerEvent::Completed, this, &ThisClass::Input_WalkReleased);
	Input->BindAction(FireAction, ETriggerEvent::Started, this, &ThisClass::Input_FirePressed);
	Input->BindAction(FireAction, ETriggerEvent::Completed, this, &ThisClass::Input_FireReleased);
	Input->BindAction(ReloadAction, ETriggerEvent::Started, this, &ThisClass::Input_Reload);
	Input->BindAction(InteractAction, ETriggerEvent::Started, this, &ThisClass::Input_InteractPressed);
	Input->BindAction(InteractAction, ETriggerEvent::Completed, this, &ThisClass::Input_InteractReleased);
}

void ATacticalCharacter::Input_Move(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	const FRotator YawRotation(0.f, GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Axis.X);
}

void ATacticalCharacter::Input_Look(const FInputActionValue& Value)
{
	const UTacticalGameUserSettings* Settings = UTacticalGameUserSettings::Get();
	const float Sensitivity = Settings ? Settings->MouseSensitivity : 1.f;
	const FVector2D Axis = Value.Get<FVector2D>() * Sensitivity;
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}

void ATacticalCharacter::Input_CrouchPressed() { Crouch(); }
void ATacticalCharacter::Input_CrouchReleased() { UnCrouch(); }
void ATacticalCharacter::Input_WalkPressed() { GetTacticalMovement()->SetWantsToShiftWalk(true); }
void ATacticalCharacter::Input_WalkReleased() { GetTacticalMovement()->SetWantsToShiftWalk(false); }

void ATacticalCharacter::Input_FirePressed()
{
	if (CurrentWeapon && IsAlive())
	{
		CurrentWeapon->StartFire();
	}
}

void ATacticalCharacter::Input_FireReleased()
{
	if (CurrentWeapon)
	{
		CurrentWeapon->StopFire();
	}
}

void ATacticalCharacter::Input_Reload()
{
	if (CurrentWeapon && IsAlive())
	{
		CurrentWeapon->StartReload();
	}
}

void ATacticalCharacter::Input_InteractPressed()
{
	const ATacticalGameState* GameState = GetWorld()->GetGameState<ATacticalGameState>();
	const ASpikeBase* Spike = GameState ? GameState->GetSpike() : nullptr;
	if (!IsAlive() || !Spike || !Spike->CanCharacterInteract(this))
	{
		return;
	}
	// Predict the freeze immediately; the flag rides in our moves so the server freezes the same moves.
	GetTacticalMovement()->SetWantsInteractLock(true);
	if (CurrentWeapon)
	{
		CurrentWeapon->StopFire();
	}
	Server_BeginInteract();
}

void ATacticalCharacter::Input_InteractReleased()
{
	if (GetTacticalMovement()->IsInteractLocked())
	{
		GetTacticalMovement()->SetWantsInteractLock(false);
		Server_EndInteract();
	}
}
