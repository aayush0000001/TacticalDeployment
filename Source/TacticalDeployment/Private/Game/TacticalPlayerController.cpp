// Copyright TacticalDeployment. All Rights Reserved.

#include "Game/TacticalPlayerController.h"
#include "Game/TacticalGameMode.h"
#include "Core/TacticalTypes.h"
#include "Engine/World.h"
#include "TimerManager.h"

ATacticalPlayerController::ATacticalPlayerController()
{
}

void ATacticalPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController() && !HasAuthority())
	{
		SendTimeSyncRequest();
		GetWorldTimerManager().SetTimer(TimeSyncTimer, this, &ThisClass::SendTimeSyncRequest, TimeSyncInterval, true);
	}
}

void ATacticalPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(TimeSyncTimer);
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------------------
// Clock sync
// ---------------------------------------------------------------------------------------

void ATacticalPlayerController::SendTimeSyncRequest()
{
	Server_RequestTimeSync(GetWorld()->GetTimeSeconds());
}

void ATacticalPlayerController::Server_RequestTimeSync_Implementation(double ClientSendTime)
{
	Client_ReportTimeSync(ClientSendTime, GetWorld()->GetTimeSeconds());
}

void ATacticalPlayerController::Client_ReportTimeSync_Implementation(double ClientSendTime, double ServerTime)
{
	ClockSync.AddSample(ClientSendTime, ServerTime, GetWorld()->GetTimeSeconds());
}

double ATacticalPlayerController::GetEstimatedServerTime() const
{
	const double Now = GetWorld()->GetTimeSeconds();
	return HasAuthority() ? Now : ClockSync.EstimateServerTime(Now);
}

double ATacticalPlayerController::GetClientViewTime() const
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (HasAuthority())
	{
		return Now; // Listen host / standalone: no latency to undo.
	}
	return ClockSync.EstimateViewTime(Now, TacticalNet::ProxyInterpolationDelay);
}

// ---------------------------------------------------------------------------------------
// Shop
// ---------------------------------------------------------------------------------------

void ATacticalPlayerController::Server_PurchaseWeapon_Implementation(const UWeaponStats* Weapon)
{
	if (ATacticalGameMode* GameMode = GetWorld()->GetAuthGameMode<ATacticalGameMode>())
	{
		GameMode->TryPurchaseWeapon(this, Weapon);
	}
}

void ATacticalPlayerController::Server_PurchaseArmor_Implementation(bool bHeavy)
{
	if (ATacticalGameMode* GameMode = GetWorld()->GetAuthGameMode<ATacticalGameMode>())
	{
		GameMode->TryPurchaseArmor(this, bHeavy);
	}
}
