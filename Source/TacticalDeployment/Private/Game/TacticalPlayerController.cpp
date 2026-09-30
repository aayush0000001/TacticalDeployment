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
	const double Now = GetWorld()->GetTimeSeconds();
	const double RoundTrip = Now - ClientSendTime;
	if (RoundTrip < 0.0 || RoundTrip > 2.0)
	{
		return; // Stale or bogus probe.
	}

	// The server stamped ServerTime ~RTT/2 before we received it.
	FClockSample& Sample = ClockSamples[NextSampleIndex];
	Sample.RoundTrip = RoundTrip;
	Sample.Offset = (ServerTime + RoundTrip * 0.5) - Now;
	NextSampleIndex = (NextSampleIndex + 1) % NumClockSamples;
	NumValidSamples = FMath::Min(NumValidSamples + 1, NumClockSamples);

	int32 Best = 0;
	for (int32 i = 1; i < NumValidSamples; ++i)
	{
		if (ClockSamples[i].RoundTrip < ClockSamples[Best].RoundTrip)
		{
			Best = i;
		}
	}

	// Slew instead of stepping so the stamped view time never jumps backwards mid-spray.
	const double TargetOffset = ClockSamples[Best].Offset;
	ServerTimeOffset = bHasTimeSync ? FMath::Lerp(ServerTimeOffset, TargetOffset, 0.25) : TargetOffset;
	SmoothedRoundTrip = bHasTimeSync ? FMath::Lerp(SmoothedRoundTrip, static_cast<float>(RoundTrip), 0.2f) : static_cast<float>(RoundTrip);
	bHasTimeSync = true;
}

double ATacticalPlayerController::GetEstimatedServerTime() const
{
	const double Now = GetWorld()->GetTimeSeconds();
	return HasAuthority() ? Now : Now + ServerTimeOffset;
}

double ATacticalPlayerController::GetClientViewTime() const
{
	if (HasAuthority())
	{
		return GetWorld()->GetTimeSeconds(); // Listen host / standalone: no latency to undo.
	}
	return GetEstimatedServerTime() - SmoothedRoundTrip * 0.5 - TacticalNet::ProxyInterpolationDelay;
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
