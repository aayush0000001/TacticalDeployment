// Copyright TacticalDeployment. All Rights Reserved.

#include "Weapons/TacticalWeapon.h"
#include "Weapons/TacticalPhysicalMaterial.h"
#include "Weapons/ShotRules.h"
#include "Character/TacticalCharacter.h"
#include "Character/TacticalCharacterMovementComponent.h"
#include "Combat/LagCompensationComponent.h"
#include "Game/TacticalGameState.h"
#include "Game/TacticalPlayerController.h"
#include "Net/FogOfWarSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

DECLARE_CYCLE_STAT(TEXT("Weapon ResolveShot"), STAT_WeaponResolveShot, STATGROUP_Tactical);

ATacticalWeapon::ATacticalWeapon()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	WeaponMesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh1P"));
	WeaponMesh1P->SetupAttachment(RootComponent);
	WeaponMesh1P->SetOnlyOwnerSee(true);
	WeaponMesh1P->CastShadow = false;
	WeaponMesh1P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh1P->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

	WeaponMesh3P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh3P"));
	WeaponMesh3P->SetupAttachment(RootComponent);
	WeaponMesh3P->SetOwnerNoSee(true);
	WeaponMesh3P->bCastHiddenShadow = true;
	WeaponMesh3P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh3P->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

	bReplicates = true;
	// Legacy path: inherit the owner's fog-of-war relevancy. (Iris: same exclusion group as the owner.)
	bNetUseOwnerRelevancy = true;
}

void ATacticalWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;

	Params.Condition = COND_InitialOnly;
	DOREPLIFETIME_WITH_PARAMS_FAST(ATacticalWeapon, Stats, Params);

	Params.Condition = COND_OwnerOnly;
	DOREPLIFETIME_WITH_PARAMS_FAST(ATacticalWeapon, AmmoInMagazine, Params);

	Params.Condition = COND_SkipOwner;
	DOREPLIFETIME_WITH_PARAMS_FAST(ATacticalWeapon, ShotNotify, Params);
}

void ATacticalWeapon::InitializeFromStats(const UWeaponStats* InStats)
{
	check(HasAuthority());
	Stats = InStats;
	AmmoInMagazine = InStats->MagazineSize;
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalWeapon, Stats, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalWeapon, AmmoInMagazine, this);

	// Server-secret seed: clients cannot pre-compute spread and aim-compensate it ("no-spread").
	ServerSpreadStream.Initialize(static_cast<int32>(FPlatformTime::Cycles() ^ GetUniqueID()));
}

void ATacticalWeapon::OnEquipped(ATacticalCharacter* NewOwner)
{
	OwnerCharacter = NewOwner;
	if (!OwnerCharacter)
	{
		return;
	}

	const FAttachmentTransformRules Rules(EAttachmentRule::SnapToTarget, true);
	WeaponMesh1P->AttachToComponent(OwnerCharacter->GetMesh1P(), Rules, Grip1PSocket);
	WeaponMesh3P->AttachToComponent(OwnerCharacter->GetMesh3P(), Rules, Grip3PSocket);
	SetActorHiddenInGame(false);

	LocalPredictedAmmo = AmmoInMagazine;
	LocalCosmeticStream.GenerateNewSeed();
	LocalSpray.Reset();

	if (HasAuthority() && Stats)
	{
		ServerEquipCompleteTime = GetWorld()->GetTimeSeconds() + Stats->EquipTime;
		ServerSpray.Reset();
	}
}

void ATacticalWeapon::OnUnequipped()
{
	StopFire();
	SetActorHiddenInGame(true);
	if (HasAuthority())
	{
		bServerReloading = false;
		GetWorldTimerManager().ClearTimer(ServerReloadTimer);
	}
}

// ---------------------------------------------------------------------------------------
// Owning client: input + prediction
// ---------------------------------------------------------------------------------------

void ATacticalWeapon::StartFire()
{
	if (!Stats || bLocalTriggerHeld)
	{
		return;
	}
	bLocalTriggerHeld = true;

	const float Interval = Stats->GetFireInterval();
	const double Now = GetWorld()->GetTimeSeconds();
	const float FirstDelay = FMath::Max(0.f, static_cast<float>(LocalLastFireTime + Interval - Now));

	if (Stats->bAutomatic)
	{
		// Looping timer keeps cadence exact across frame boundaries (expire time += rate).
		GetWorldTimerManager().SetTimer(LocalFireTimer, this, &ThisClass::LocalFireShot, Interval, true, FirstDelay > 0.f ? FirstDelay : -1.f);
		if (FirstDelay <= 0.f)
		{
			LocalFireShot();
		}
	}
	else if (FirstDelay <= 0.f)
	{
		LocalFireShot();
	}
	else
	{
		GetWorldTimerManager().SetTimer(LocalFireTimer, this, &ThisClass::LocalFireShot, FirstDelay, false);
	}
}

void ATacticalWeapon::StopFire()
{
	bLocalTriggerHeld = false;
	if (Stats && Stats->bAutomatic)
	{
		GetWorldTimerManager().ClearTimer(LocalFireTimer);
	}
}

void ATacticalWeapon::StartReload()
{
	if (!Stats || LocalPredictedAmmo >= Stats->MagazineSize)
	{
		return;
	}
	StopFire();
	Server_Reload();
}

float ATacticalWeapon::GetPredictedSpreadDegrees() const
{
	if (!Stats)
	{
		return 0.f;
	}
	// Preview the spray state at "now" without committing a shot.
	FWeaponSprayState Preview = LocalSpray;
	const ATacticalPlayerController* PC = OwnerCharacter ? OwnerCharacter->GetController<ATacticalPlayerController>() : nullptr;
	Preview.Recover(PC ? PC->GetClientViewTime() : GetWorld()->GetTimeSeconds(), *Stats);
	return Stats->ComputeSpread(BuildSpreadInputs(Preview.FiringError));
}

void ATacticalWeapon::LocalFireShot()
{
	const ATacticalGameState* GameState = GetWorld()->GetGameState<ATacticalGameState>();
	if (!OwnerCharacter || !OwnerCharacter->IsAlive() || LocalPredictedAmmo <= 0 || !GameState || !GameState->IsCombatAllowed())
	{
		StopFire();
		return;
	}

	const ATacticalPlayerController* PC = OwnerCharacter->GetController<ATacticalPlayerController>();
	const double ViewTime = PC ? PC->GetClientViewTime() : GetWorld()->GetTimeSeconds();
	LocalLastFireTime = GetWorld()->GetTimeSeconds();

	const FVector Start = OwnerCharacter->GetPawnViewLocation();
	const FRotator AimRotation = OwnerCharacter->GetControlRotation();
	const FVector End = Start + AimRotation.Vector() * Stats->MaxRange;

	// Predict the same spray the server will compute (same timestamps -> same bullet index).
	LocalSpray.Recover(ViewTime, *Stats);
	const int32 ShotIndex = LocalSpray.GetShotIndex();
	const FVector2D Recoil = Stats->EvaluateRecoil(ShotIndex);
	const float Spread = Stats->ComputeSpread(BuildSpreadInputs(LocalSpray.FiringError));
	const FVector CosmeticDirection = UWeaponStats::ApplyRecoilAndSpread(AimRotation, Recoil, Spread, LocalCosmeticStream);
	LocalSpray.CommitShot(ViewTime, *Stats);
	--LocalPredictedAmmo;

	OwnerCharacter->SetViewKickTarget(FRotator(Recoil.Y, Recoil.X, 0.f) * Stats->CameraKickFraction);
	OwnerCharacter->AddViewmodelKick(Stats->ViewmodelKick);

	FCollisionQueryParams CosmeticParams(SCENE_QUERY_STAT(WeaponCosmeticTrace), false, OwnerCharacter);
	FHitResult CosmeticHit;
	const FVector CosmeticEnd = Start + CosmeticDirection * Stats->MaxRange;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(CosmeticHit, Start, CosmeticEnd, TacticalCollision::WeaponTrace, CosmeticParams);
	PlayFireEffects(bHit ? CosmeticHit.ImpactPoint : CosmeticEnd);

	// Send pending moves first, so the server evaluates movement inaccuracy against the exact
	// velocity we had when we pulled the trigger (critical for counter-strafe fairness).
	OwnerCharacter->GetCharacterMovement()->FlushServerMoves();

	Server_FireWeapon(ViewTime, Start, End, static_cast<uint8>(FMath::Min(ShotIndex, 255)));
}

void ATacticalWeapon::OnRep_AmmoInMagazine()
{
	// Adopt increases (reload completed); keep our lower predicted count while shots are in flight.
	if (AmmoInMagazine > LocalPredictedAmmo)
	{
		LocalPredictedAmmo = AmmoInMagazine;
	}
}

void ATacticalWeapon::OnRep_ShotNotify()
{
	PlayFireEffects(ShotNotify.ImpactPoint);
}

FWeaponSpreadInputs ATacticalWeapon::BuildSpreadInputs(float FiringError) const
{
	FWeaponSpreadInputs Inputs;
	Inputs.FiringError = FiringError;
	if (const UTacticalCharacterMovementComponent* Movement = OwnerCharacter ? OwnerCharacter->GetTacticalMovement() : nullptr)
	{
		const float WeaponSpeedScale = Stats ? Stats->MovementSpeedMultiplier : 1.f;
		Inputs.HorizontalSpeed = Movement->Velocity.Size2D();
		Inputs.MaxRunSpeed = Movement->GetMaxRunSpeed() * WeaponSpeedScale;
		Inputs.ShiftWalkSpeed = Movement->MaxShiftWalkSpeed * WeaponSpeedScale;
		Inputs.bIsAirborne = Movement->IsFalling();
		Inputs.bIsCrouched = Movement->IsCrouching();
	}
	return Inputs;
}

// ---------------------------------------------------------------------------------------
// Server: validation + authoritative resolution
// ---------------------------------------------------------------------------------------

void ATacticalWeapon::Server_FireWeapon_Implementation(double ViewTime, FVector_NetQuantize100 StartTrace, FVector_NetQuantize EndTrace, uint8 ShotIndex)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (!ServerValidateFire(Now, ViewTime, StartTrace))
	{
		return;
	}

	const FVector AimDirection = (FVector(EndTrace) - FVector(StartTrace)).GetSafeNormal();
	if (AimDirection.IsNearlyZero())
	{
		return;
	}

	// Fire rate on client stamps: immune to network jitter bunching RPCs together.
	const APlayerState* ShooterState = OwnerCharacter->GetPlayerState();
	const double RoundTrip = ShooterState ? ShooterState->GetPingInMilliseconds() * 0.001 : 0.0;
	double CadenceTime = ViewTime;
	if (!ServerCadence.TryAcceptShot(ViewTime, Now, RoundTrip + TacticalNet::ProxyInterpolationDelay, Stats->GetFireInterval(), &CadenceTime))
	{
		UE_LOG(LogTacticalHitReg, Warning, TEXT("%s: fire cadence violation (stamp age %.4f s)."), *GetNameSafe(OwnerCharacter), Now - ViewTime);
		return;
	}

	ServerResolveShot(ViewTime, CadenceTime, StartTrace, AimDirection);

	UE_CLOG(ShotIndex != FMath::Min(ServerSpray.GetShotIndex() - 1, 255), LogTacticalHitReg, VeryVerbose,
		TEXT("Spray index desync on %s: client %d server %d"), *GetName(), ShotIndex, ServerSpray.GetShotIndex() - 1);
}

bool ATacticalWeapon::ServerValidateFire(double ServerNow, double ViewTime, const FVector& Start) const
{
	if (!Stats || !OwnerCharacter || !OwnerCharacter->IsAlive() || OwnerCharacter->GetCurrentWeapon() != this)
	{
		return false;
	}
	if (OwnerCharacter->GetTacticalMovement()->IsInteractLocked())
	{
		return false;
	}

	const ATacticalGameState* GameState = GetWorld()->GetGameState<ATacticalGameState>();
	if (!GameState || !GameState->IsCombatAllowed())
	{
		return false;
	}
	if (bServerReloading || AmmoInMagazine <= 0 || ServerNow < ServerEquipCompleteTime)
	{
		return false;
	}

	// The shot must originate at our authoritative eye (moves were flushed before the RPC).
	if (FVector::DistSquared(Start, OwnerCharacter->GetPawnViewLocation()) > FMath::Square(MaxEyeLocationError))
	{
		UE_LOG(LogTacticalHitReg, Warning, TEXT("%s: eye location mismatch."), *GetNameSafe(OwnerCharacter));
		return false;
	}
	return true;
}

void ATacticalWeapon::ServerResolveShot(double ViewTime, double CadenceTime, const FVector& Start, const FVector& AimDirection)
{
	SCOPE_CYCLE_COUNTER(STAT_WeaponResolveShot);

	// 1) Authoritative spray: deterministic recoil by bullet index, spread from server state.
	//    Timed on the gate's cadence time, so forged stamp gaps cannot buy recoil recovery.
	ServerSpray.Recover(CadenceTime, *Stats);
	const FVector2D Recoil = Stats->EvaluateRecoil(ServerSpray.GetShotIndex());
	const float Spread = Stats->ComputeSpread(BuildSpreadInputs(ServerSpray.FiringError));
	const FVector Direction = UWeaponStats::ApplyRecoilAndSpread(AimDirection.Rotation(), Recoil, Spread, ServerSpreadStream);
	ServerSpray.CommitShot(CadenceTime, *Stats);

	--AmmoInMagazine;
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalWeapon, AmmoInMagazine, this);

	OwnerCharacter->ReportNoise(GetDefault<UTacticalFogOfWarSettings>()->GunfireAudibleRange);

	// 2) Rewind every enemy to what this client saw, trace, restore.
	const ATacticalGameState* GameState = GetWorld()->GetGameState<ATacticalGameState>();
	const ULagCompensationComponent* LagCompensation = GameState->GetLagCompensation();
	const double RewindTime = LagCompensation->ResolveRewindTime(OwnerCharacter->GetController<APlayerController>(), ViewTime);

	FVector ImpactPoint;
	{
		FScopedLagCompensation Rewind(*LagCompensation, RewindTime, OwnerCharacter);
		ImpactPoint = TraceWithPenetration(Rewind, Start, Direction);
	} // Present restored.

	// 3) Cosmetics for everyone else (fog-filtered like any property).
	ShotNotify.ImpactPoint = ImpactPoint;
	++ShotNotify.ShotCounter;
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalWeapon, ShotNotify, this);
}

FVector ATacticalWeapon::TraceWithPenetration(const FScopedLagCompensation& Rewind, const FVector& Start, const FVector& Direction)
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WeaponTrace), /*bTraceComplex*/ true);
	Params.AddIgnoredActor(this);
	Params.AddIgnoredActor(OwnerCharacter);
	Params.bReturnPhysicalMaterial = true;

	UWorld* World = GetWorld();
	FHitResult LastWorldHit; // The exit probe needs the component of the surface just entered.
	FRewoundHit LastBodyHit;

	// World geometry only: characters ignore WeaponTrace and are hit via rewound hitboxes.
	auto TraceWorld = [&](const FVector& From, const FVector& To, FBulletSurfaceHit& Out)
	{
		if (!World->LineTraceSingleByChannel(LastWorldHit, From, To, TacticalCollision::WeaponTrace, Params))
		{
			return false;
		}
		Out.ImpactPoint = LastWorldHit.ImpactPoint;
		Out.Distance = LastWorldHit.Distance;
		Out.Density = UTacticalPhysicalMaterial::GetDensity(LastWorldHit.PhysMaterial.Get());
		return true;
	};
	auto ProbeExit = [&](const FBulletSurfaceHit&, const FVector& Dir, float MaxThickness, FVector& OutExit)
	{
		return FindExitPoint(LastWorldHit, Dir, MaxThickness, OutExit);
	};
	auto TraceBodies = [&](const FVector& From, const FVector& To, FBulletBodyHit& Out)
	{
		if (!Rewind.LineTrace(From, To, LastBodyHit))
		{
			return false;
		}
		Out.Location = LastBodyHit.Location;
		Out.Distance = LastBodyHit.Distance;
		return true;
	};

	const FBulletResult Result = ShotRules::SolveBulletPath(Start, Direction, Stats->MaxRange, Stats->GetPenetrationParams(), TraceWorld, ProbeExit, TraceBodies);
	if (Result.bHitBody)
	{
		ApplyHitDamage(LastBodyHit.Character, LastBodyHit.Zone, Result.TravelledDistance, Result.DamageScale, Direction);
	}
	return Result.ImpactPoint;
}

bool ATacticalWeapon::FindExitPoint(const FHitResult& EntryHit, const FVector& Direction, float MaxThickness, FVector& OutExit) const
{
	UPrimitiveComponent* Component = EntryHit.GetComponent();
	if (!Component)
	{
		return false;
	}

	// Trace *backwards* from beyond the far side against this one component: the first face
	// hit is the exit surface. Walls thicker than MaxThickness are not penetrable by this tier.
	const FVector Probe = FVector(EntryHit.ImpactPoint) + Direction * MaxThickness;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WeaponExitTrace), /*bTraceComplex*/ true);
	FHitResult ExitHit;
	const bool bProbeHit = Component->LineTraceComponent(ExitHit, Probe, EntryHit.ImpactPoint, Params);
	if (!ShotRules::IsValidExitProbe(bProbeHit, ExitHit.bStartPenetrating, ExitHit.ImpactPoint, EntryHit.ImpactPoint))
	{
		return false; // Thicker than this tier can probe (see IsValidExitProbe).
	}

	OutExit = ExitHit.ImpactPoint;
	return true;
}

void ATacticalWeapon::ApplyHitDamage(ATacticalCharacter* Victim, EHitZone Zone, float TravelledDistance, float PenetrationScale, const FVector& ShotDirection)
{
	if (!Victim)
	{
		return;
	}
	const float Damage = Stats->BaseDamage
		* Stats->GetZoneMultiplier(Zone)
		* Stats->GetRangeMultiplier(TravelledDistance)
		* PenetrationScale;

	const bool bWallbang = PenetrationScale < 1.f;
	const float Dealt = Victim->ApplyBulletDamage(FMath::RoundToFloat(Damage), Zone, Stats, OwnerCharacter->GetController(), this, ShotDirection, bWallbang);

	// Hit confirmation goes to the shooter only (never to anyone else).
	if (ATacticalPlayerController* ShooterPC = OwnerCharacter->GetController<ATacticalPlayerController>())
	{
		ShooterPC->Client_HitConfirmed(Zone, static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Dealt), 0, 255)), !Victim->IsAlive(), bWallbang);
	}
}

// ---------------------------------------------------------------------------------------
// Reload
// ---------------------------------------------------------------------------------------

void ATacticalWeapon::Server_Reload_Implementation()
{
	if (!Stats || bServerReloading || AmmoInMagazine >= Stats->MagazineSize || !OwnerCharacter || !OwnerCharacter->IsAlive())
	{
		return;
	}
	bServerReloading = true;
	GetWorldTimerManager().SetTimer(ServerReloadTimer, this, &ThisClass::FinishReload, Stats->ReloadTime, false);
}

void ATacticalWeapon::FinishReload()
{
	bServerReloading = false;
	AmmoInMagazine = Stats->MagazineSize;
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalWeapon, AmmoInMagazine, this);
	OnRep_AmmoInMagazine(); // Listen-server host.
}
