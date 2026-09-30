// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Core/TacticalTypes.h"
#include "TacticalGameMode.generated.h"

class ATacticalCharacter;
class ATacticalGameState;
class ASpikeBase;
class UWeaponStats;

/**
 * Server-only round state machine for 5v5 attack/defense.
 *
 *   PreMatch -> BuyPhase -> BarrierPhase -> ActionPhase -> PostRound -> BuyPhase ... -> MatchEnded
 *
 * One timer drives every transition (OnPhaseTimerExpired). Round-ending events (elimination,
 * detonation, defuse) short-circuit into EndRound(). Planting replaces the ActionPhase timer with
 * the detonation timer, so "time expired" can only ever mean the defenders held.
 */
UCLASS(config = Game)
class TACTICALDEPLOYMENT_API ATacticalGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATacticalGameMode();

	// --- Events from gameplay -------------------------------------------------------------

	void OnCharacterKilled(ATacticalCharacter* Victim, AController* Killer);
	void OnSpikePlanted(ATacticalCharacter* Planter);
	void OnSpikeDefused(ATacticalCharacter* Defuser);
	void OnSpikeDetonated();

	// --- Shop -----------------------------------------------------------------------------

	bool TryPurchaseWeapon(APlayerController* Buyer, const UWeaponStats* Weapon);
	bool TryPurchaseArmor(APlayerController* Buyer, bool bHeavy);

	float GetSpikeDetonationTime() const { return SpikeDetonationTime; }

	//~ AGameModeBase
	virtual void StartPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;

protected:
	void EnterPhase(ETacticalMatchPhase Phase, float Duration);
	void OnPhaseTimerExpired();

	void StartNewRound();
	void EndRound(ETacticalTeam Winner, ETacticalRoundEndReason Reason);
	void CheckEliminationWin();
	bool HasTeamWonMatch(ETacticalTeam Team) const;

	void ResetPlayersForRound();
	void SpawnSpikeForAttackers();
	void SpectateTeammate(APlayerController* PC, ETacticalTeam Team) const;

	int32 CountAlive(ETacticalTeam Team) const;
	ETacticalTeam PickTeamForNewPlayer() const;
	ATacticalGameState* GetTacticalGameState() const;

	// --- Rules (DefaultGame.ini) ----------------------------------------------------------

	UPROPERTY(config, EditDefaultsOnly, Category = "Rules")
	int32 RoundsToWin = 13;

	UPROPERTY(config, EditDefaultsOnly, Category = "Rules")
	int32 HalftimeAfterRound = 12;

	UPROPERTY(config, EditDefaultsOnly, Category = "Rules", meta = (Units = "s"))
	float PreMatchDuration = 10.f;

	UPROPERTY(config, EditDefaultsOnly, Category = "Rules", meta = (Units = "s"))
	float BuyPhaseDuration = 15.f;

	UPROPERTY(config, EditDefaultsOnly, Category = "Rules", meta = (Units = "s"))
	float BarrierPhaseDuration = 15.f;

	UPROPERTY(config, EditDefaultsOnly, Category = "Rules", meta = (Units = "s"))
	float ActionPhaseDuration = 100.f;

	UPROPERTY(config, EditDefaultsOnly, Category = "Rules", meta = (Units = "s"))
	float PostRoundDuration = 7.f;

	UPROPERTY(config, EditDefaultsOnly, Category = "Rules", meta = (Units = "s"))
	float SpikeDetonationTime = 45.f;

	// --- Content ----------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, Category = "Content")
	TSubclassOf<ASpikeBase> SpikeClass;

	UPROPERTY(EditDefaultsOnly, Category = "Content")
	TObjectPtr<const UWeaponStats> DefaultSidearm;

	/** The only weapons a client may request. Anything else is rejected. */
	UPROPERTY(EditDefaultsOnly, Category = "Content")
	TArray<TObjectPtr<const UWeaponStats>> ShopCatalog;

	// --- Economy ----------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, Category = "Economy")
	int32 RoundWinCredits = 3000;

	UPROPERTY(EditDefaultsOnly, Category = "Economy")
	int32 RoundLossCredits = 1900;

	UPROPERTY(EditDefaultsOnly, Category = "Economy")
	int32 KillCredits = 200;

	UPROPERTY(EditDefaultsOnly, Category = "Economy")
	int32 PlantCredits = 300;

	UPROPERTY(EditDefaultsOnly, Category = "Economy")
	int32 LightArmorCost = 400;

	UPROPERTY(EditDefaultsOnly, Category = "Economy")
	int32 HeavyArmorCost = 1000;

	UPROPERTY(EditDefaultsOnly, Category = "Economy")
	float LightArmorValue = 25.f;

	UPROPERTY(EditDefaultsOnly, Category = "Economy")
	float HeavyArmorValue = 50.f;

private:
	FTimerHandle PhaseTimer;
	uint8 CurrentRound = 0;
	bool bRoundResolved = false;
};
