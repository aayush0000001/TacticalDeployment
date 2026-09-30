// Copyright TacticalDeployment. All Rights Reserved.

#include "Game/TacticalPlayerState.h"
#include "Misc/EngineVersionComparison.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

ATacticalPlayerState::ATacticalPlayerState()
{
	// Scoreboard data: no need to spend 128 Hz on it.
#if UE_VERSION_OLDER_THAN(5, 5, 0)
	NetUpdateFrequency = 10.f;
#else
	SetNetUpdateFrequency(10.f);
#endif
}

void ATacticalPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(ATacticalPlayerState, Team, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(ATacticalPlayerState, Kills, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(ATacticalPlayerState, Deaths, Params);

	Params.Condition = COND_OwnerOnly;
	DOREPLIFETIME_WITH_PARAMS_FAST(ATacticalPlayerState, Credits, Params);
}

void ATacticalPlayerState::ServerSetTeam(ETacticalTeam NewTeam)
{
	check(HasAuthority());
	Team = NewTeam;
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalPlayerState, Team, this);
}

void ATacticalPlayerState::ServerAddCredits(int32 Amount)
{
	check(HasAuthority());
	Credits = FMath::Clamp(Credits + Amount, 0, MaxCredits);
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalPlayerState, Credits, this);
}

bool ATacticalPlayerState::ServerTrySpendCredits(int32 Amount)
{
	check(HasAuthority());
	if (Amount < 0 || Credits < Amount)
	{
		return false;
	}
	Credits -= Amount;
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalPlayerState, Credits, this);
	return true;
}

void ATacticalPlayerState::ServerAddKill()
{
	check(HasAuthority());
	++Kills;
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalPlayerState, Kills, this);
}

void ATacticalPlayerState::ServerAddDeath()
{
	check(HasAuthority());
	++Deaths;
	MARK_PROPERTY_DIRTY_FROM_NAME(ATacticalPlayerState, Deaths, this);
}
