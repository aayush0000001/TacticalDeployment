// Copyright TacticalDeployment. All Rights Reserved.

#include "Game/TacticalGameMode.h"
#include "Character/TacticalCharacter.h"
#include "Game/RoundRules.h"
#include "Game/SpikeBase.h"
#include "Game/TacticalGameState.h"
#include "Game/TacticalPlayerController.h"
#include "Game/TacticalPlayerState.h"
#include "UI/TacticalHUD.h"
#include "Weapons/WeaponStats.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "TimerManager.h"

namespace TacticalSpawnTags
{
	static const FName Attackers(TEXT("Attackers"));
	static const FName Defenders(TEXT("Defenders"));
}

ATacticalGameMode::ATacticalGameMode()
{
	GameStateClass = ATacticalGameState::StaticClass();
	PlayerStateClass = ATacticalPlayerState::StaticClass();
	PlayerControllerClass = ATacticalPlayerController::StaticClass();
	DefaultPawnClass = ATacticalCharacter::StaticClass(); // Override with the character Blueprint.
	HUDClass = ATacticalHUD::StaticClass();
	bStartPlayersAsSpectators = false;
}

ATacticalGameState* ATacticalGameMode::GetTacticalGameState() const
{
	return GetGameState<ATacticalGameState>();
}

void ATacticalGameMode::StartPlay()
{
	Super::StartPlay();
	EnterPhase(ETacticalMatchPhase::PreMatch, PreMatchDuration);
}

// ---------------------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------------------

void ATacticalGameMode::EnterPhase(ETacticalMatchPhase Phase, float Duration)
{
	GetTacticalGameState()->ServerSetPhase(Phase, Duration);

	GetWorldTimerManager().ClearTimer(PhaseTimer);
	if (Duration > 0.f)
	{
		GetWorldTimerManager().SetTimer(PhaseTimer, this, &ThisClass::OnPhaseTimerExpired, Duration, false);
	}
	UE_LOG(LogTactical, Log, TEXT("Round %d: entering phase %s (%.1f s)"), CurrentRound, *UEnum::GetValueAsString(Phase), Duration);
}

void ATacticalGameMode::OnPhaseTimerExpired()
{
	ATacticalGameState* GameState = GetTacticalGameState();
	switch (RoundRules::OnPhaseTimerExpired(GameState->GetPhase(), GameState->GetRoundState().bSpikePlanted))
	{
	case RoundRules::EPhaseTimerAction::StartNewRound:
		StartNewRound();
		break;

	case RoundRules::EPhaseTimerAction::EnterBarrierPhase:
		EnterPhase(ETacticalMatchPhase::BarrierPhase, BarrierPhaseDuration);
		break;

	case RoundRules::EPhaseTimerAction::EnterActionPhase:
		EnterPhase(ETacticalMatchPhase::ActionPhase, ActionPhaseDuration);
		break;

	case RoundRules::EPhaseTimerAction::DefendersWinOnTime:
		EndRound(GameState->GetDefendingTeam(), ETacticalRoundEndReason::TimeExpired);
		break;

	case RoundRules::EPhaseTimerAction::FinishPostRound:
		if (HasTeamWonMatch(ETacticalTeam::TeamA) || HasTeamWonMatch(ETacticalTeam::TeamB))
		{
			EnterPhase(ETacticalMatchPhase::MatchEnded, 0.f);
		}
		else
		{
			StartNewRound();
		}
		break;

	default:
		break;
	}
}

void ATacticalGameMode::StartNewRound()
{
	++CurrentRound;
	bRoundResolved = false;

	// Sides swap at halftime. (Overtime side alternation would slot in here.)
	const ETacticalTeam Attackers = RoundRules::GetAttackingTeam(CurrentRound, HalftimeAfterRound);
	GetTacticalGameState()->ServerBeginRound(CurrentRound, Attackers);

	// Phase first: movement is locked (BuyPhase) before anyone spawns.
	EnterPhase(ETacticalMatchPhase::BuyPhase, BuyPhaseDuration);
	ResetPlayersForRound();
	SpawnSpikeForAttackers();
}

void ATacticalGameMode::EndRound(ETacticalTeam Winner, ETacticalRoundEndReason Reason)
{
	ATacticalGameState* GameState = GetTacticalGameState();
	if (bRoundResolved || GameState->GetPhase() != ETacticalMatchPhase::ActionPhase)
	{
		return;
	}
	bRoundResolved = true;

	GameState->ServerRecordRoundResult(Winner, Reason);
	if (ASpikeBase* Spike = GameState->GetSpike())
	{
		Spike->ServerOnRoundEnded();
	}

	for (APlayerState* PS : GameState->PlayerArray)
	{
		if (ATacticalPlayerState* TacticalPS = Cast<ATacticalPlayerState>(PS))
		{
			TacticalPS->ServerAddCredits(TacticalPS->GetTeam() == Winner ? RoundWinCredits : RoundLossCredits);
		}
	}

	UE_LOG(LogTactical, Log, TEXT("Round %d won by %s (%s)"), CurrentRound, *UEnum::GetValueAsString(Winner), *UEnum::GetValueAsString(Reason));
	EnterPhase(ETacticalMatchPhase::PostRound, PostRoundDuration);
}

bool ATacticalGameMode::HasTeamWonMatch(ETacticalTeam Team) const
{
	const ATacticalGameState* GameState = GetTacticalGameState();
	return RoundRules::HasTeamWonMatch(GameState->GetScore(Team), GameState->GetScore(GetOpposingTeam(Team)), RoundsToWin);
}

// ---------------------------------------------------------------------------------------
// Gameplay events
// ---------------------------------------------------------------------------------------

void ATacticalGameMode::OnCharacterKilled(ATacticalCharacter* Victim, AController* Killer)
{
	if (ATacticalPlayerState* VictimPS = Victim ? Victim->GetPlayerState<ATacticalPlayerState>() : nullptr)
	{
		VictimPS->ServerAddDeath();
		SpectateTeammate(Cast<APlayerController>(Victim->GetController()), VictimPS->GetTeam());
	}

	ATacticalPlayerState* KillerPS = Killer ? Killer->GetPlayerState<ATacticalPlayerState>() : nullptr;
	if (KillerPS && Victim && KillerPS->GetTeam() != Victim->GetTeam())
	{
		KillerPS->ServerAddKill();
		KillerPS->ServerAddCredits(KillCredits);
	}

	if (Victim)
	{
		FKillFeedEntry Entry;
		Entry.Killer = KillerPS;
		Entry.Victim = Victim->GetPlayerState();
		Entry.Weapon = Victim->GetLastDamageWeapon();
		Entry.bHeadshot = Victim->GetLastDamageZone() == EHitZone::Head;
		Entry.bWallbang = Victim->WasLastDamageWallbang();
		GetTacticalGameState()->Multicast_KillFeed(Entry);
	}

	CheckEliminationWin();
}

void ATacticalGameMode::CheckEliminationWin()
{
	const ATacticalGameState* GameState = GetTacticalGameState();
	const ETacticalTeam Attackers = GameState->GetAttackingTeam();
	const ETacticalTeam Defenders = GameState->GetDefendingTeam();

	const ETacticalTeam Winner = RoundRules::EvaluateElimination(CountAlive(Attackers), CountAlive(Defenders),
		GameState->GetRoundState().bSpikePlanted, Attackers);
	if (Winner != ETacticalTeam::Spectator)
	{
		EndRound(Winner, ETacticalRoundEndReason::Elimination);
	}
}

void ATacticalGameMode::OnSpikePlanted(ATacticalCharacter* Planter)
{
	ATacticalGameState* GameState = GetTacticalGameState();
	GameState->ServerSetSpikePlanted(true);

	// The round timer becomes the fuse; the spike owns detonation, we only mirror the end time.
	GetWorldTimerManager().ClearTimer(PhaseTimer);
	GameState->ServerSetPhaseEndTime(GameState->GetServerWorldTimeSeconds() + SpikeDetonationTime);

	if (ATacticalPlayerState* PlanterPS = Planter ? Planter->GetPlayerState<ATacticalPlayerState>() : nullptr)
	{
		PlanterPS->ServerAddCredits(PlantCredits);
	}
}

void ATacticalGameMode::OnSpikeDefused(ATacticalCharacter* Defuser)
{
	EndRound(GetTacticalGameState()->GetDefendingTeam(), ETacticalRoundEndReason::SpikeDefused);
}

void ATacticalGameMode::OnSpikeDetonated()
{
	EndRound(GetTacticalGameState()->GetAttackingTeam(), ETacticalRoundEndReason::SpikeDetonated);
}

// ---------------------------------------------------------------------------------------
// Players, spawning, spike
// ---------------------------------------------------------------------------------------

void ATacticalGameMode::PostLogin(APlayerController* NewPlayer)
{
	if (ATacticalPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<ATacticalPlayerState>() : nullptr)
	{
		PS->ServerSetTeam(PickTeamForNewPlayer());
	}
	Super::PostLogin(NewPlayer);
}

ETacticalTeam ATacticalGameMode::PickTeamForNewPlayer() const
{
	int32 CountA = 0;
	int32 CountB = 0;
	for (const APlayerState* PS : GetTacticalGameState()->PlayerArray)
	{
		if (const ATacticalPlayerState* TacticalPS = Cast<ATacticalPlayerState>(PS))
		{
			CountA += TacticalPS->GetTeam() == ETacticalTeam::TeamA;
			CountB += TacticalPS->GetTeam() == ETacticalTeam::TeamB;
		}
	}
	return CountA <= CountB ? ETacticalTeam::TeamA : ETacticalTeam::TeamB;
}

bool ATacticalGameMode::PlayerCanRestart_Implementation(APlayerController* Player)
{
	// Mid-round joiners/respawns wait for the next round; spawning is owned by ResetPlayersForRound.
	const ETacticalMatchPhase Phase = GetTacticalGameState()->GetPhase();
	return (Phase == ETacticalMatchPhase::PreMatch || Phase == ETacticalMatchPhase::BuyPhase) && Super::PlayerCanRestart_Implementation(Player);
}

AActor* ATacticalGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	const ATacticalPlayerState* PS = Player ? Player->GetPlayerState<ATacticalPlayerState>() : nullptr;
	const bool bAttacker = PS && PS->GetTeam() == GetTacticalGameState()->GetAttackingTeam();
	const FName WantedTag = bAttacker ? TacticalSpawnTags::Attackers : TacticalSpawnTags::Defenders;

	TArray<APlayerStart*, TInlineAllocator<8>> Candidates;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		if (It->PlayerStartTag == WantedTag)
		{
			Candidates.Add(*It);
		}
	}

	// Shuffle (Fisher-Yates), then prefer starts nobody is standing on.
	for (int32 i = Candidates.Num() - 1; i > 0; --i)
	{
		Candidates.Swap(i, FMath::RandRange(0, i));
	}
	for (APlayerStart* Start : Candidates)
	{
		bool bOccupied = false;
		for (TActorIterator<ATacticalCharacter> CharIt(GetWorld()); CharIt; ++CharIt)
		{
			if (CharIt->IsAlive() && FVector::DistSquared(CharIt->GetActorLocation(), Start->GetActorLocation()) < FMath::Square(80.f))
			{
				bOccupied = true;
				break;
			}
		}
		if (!bOccupied)
		{
			return Start;
		}
	}
	return Candidates.Num() > 0 ? Candidates[0] : Super::ChoosePlayerStart_Implementation(Player);
}

void ATacticalGameMode::ResetPlayersForRound()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		const ATacticalPlayerState* PS = PC ? PC->GetPlayerState<ATacticalPlayerState>() : nullptr;
		if (!PS || PS->GetTeam() == ETacticalTeam::Spectator)
		{
			continue;
		}

		if (APawn* OldPawn = PC->GetPawn())
		{
			PC->UnPossess();
			OldPawn->Destroy();
		}
		RestartPlayer(PC);
		PC->SetViewTarget(PC->GetPawn());

		if (ATacticalCharacter* Character = Cast<ATacticalCharacter>(PC->GetPawn()))
		{
			Character->ServerGiveWeapon(DefaultSidearm);
		}
	}
}

void ATacticalGameMode::SpawnSpikeForAttackers()
{
	ATacticalGameState* GameState = GetTacticalGameState();
	if (ASpikeBase* OldSpike = GameState->GetSpike())
	{
		OldSpike->Destroy();
		GameState->ServerSetSpike(nullptr);
	}
	if (!SpikeClass)
	{
		return;
	}

	TArray<ATacticalCharacter*, TInlineAllocator<5>> Attackers;
	for (TActorIterator<ATacticalCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && It->GetTeam() == GameState->GetAttackingTeam())
		{
			Attackers.Add(*It);
		}
	}
	if (Attackers.Num() == 0)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASpikeBase* Spike = GetWorld()->SpawnActor<ASpikeBase>(SpikeClass, Attackers[0]->GetActorTransform(), Params);
	GameState->ServerSetSpike(Spike);
	Spike->ServerGiveTo(Attackers[FMath::RandRange(0, Attackers.Num() - 1)]);
}

void ATacticalGameMode::SpectateTeammate(APlayerController* PC, ETacticalTeam Team) const
{
	if (!PC)
	{
		return;
	}
	for (TActorIterator<ATacticalCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && It->GetTeam() == Team)
		{
			// The fog of war now evaluates this connection through the teammate's eyes.
			PC->SetViewTargetWithBlend(*It, 0.5f);
			return;
		}
	}
}

int32 ATacticalGameMode::CountAlive(ETacticalTeam Team) const
{
	int32 Count = 0;
	for (TActorIterator<ATacticalCharacter> It(GetWorld()); It; ++It)
	{
		Count += (It->IsAlive() && It->GetTeam() == Team) ? 1 : 0;
	}
	return Count;
}

// ---------------------------------------------------------------------------------------
// Shop (all validation server-side)
// ---------------------------------------------------------------------------------------

bool ATacticalGameMode::TryPurchaseWeapon(APlayerController* Buyer, const UWeaponStats* Weapon)
{
	ATacticalPlayerState* PS = Buyer ? Buyer->GetPlayerState<ATacticalPlayerState>() : nullptr;
	ATacticalCharacter* Character = Buyer ? Cast<ATacticalCharacter>(Buyer->GetPawn()) : nullptr;
	if (!PS || !Character || !Character->IsAlive() || !GetTacticalGameState()->IsShopOpen())
	{
		return false;
	}
	if (!Weapon || !ShopCatalog.Contains(Weapon))
	{
		UE_LOG(LogTactical, Warning, TEXT("%s requested non-catalog item %s."), *GetNameSafe(Buyer), *GetNameSafe(Weapon));
		return false;
	}
	if (!PS->ServerTrySpendCredits(Weapon->Cost))
	{
		return false;
	}
	Character->ServerGiveWeapon(Weapon);
	return true;
}

bool ATacticalGameMode::TryPurchaseArmor(APlayerController* Buyer, bool bHeavy)
{
	ATacticalPlayerState* PS = Buyer ? Buyer->GetPlayerState<ATacticalPlayerState>() : nullptr;
	ATacticalCharacter* Character = Buyer ? Cast<ATacticalCharacter>(Buyer->GetPawn()) : nullptr;
	if (!PS || !Character || !Character->IsAlive() || !GetTacticalGameState()->IsShopOpen())
	{
		return false;
	}
	const float Value = bHeavy ? HeavyArmorValue : LightArmorValue;
	if (Character->GetArmor() >= Value || !PS->ServerTrySpendCredits(bHeavy ? HeavyArmorCost : LightArmorCost))
	{
		return false;
	}
	Character->ServerSetArmor(Value);
	return true;
}
