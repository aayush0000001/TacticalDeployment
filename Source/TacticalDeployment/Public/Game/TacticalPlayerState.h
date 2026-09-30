// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Core/TacticalTypes.h"
#include "TacticalPlayerState.generated.h"

/**
 * Persistent (per match) player data. PlayerStates are always relevant and contain no spatial
 * data, so nothing here can leak positions through the fog of war.
 */
UCLASS()
class TACTICALDEPLOYMENT_API ATacticalPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	ATacticalPlayerState();

	ETacticalTeam GetTeam() const { return Team; }
	int32 GetCredits() const { return Credits; }
	int32 GetKills() const { return Kills; }
	int32 GetDeaths() const { return Deaths; }

	// --- Server-only ----------------------------------------------------------------------

	void ServerSetTeam(ETacticalTeam NewTeam);
	void ServerAddCredits(int32 Amount);
	bool ServerTrySpendCredits(int32 Amount);
	void ServerAddKill();
	void ServerAddDeath();

	//~ AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditDefaultsOnly, Category = "Economy")
	int32 MaxCredits = 9000;

protected:
	UPROPERTY(Replicated)
	ETacticalTeam Team = ETacticalTeam::Spectator;

	/** Owner only: the enemy team must not see your economy. */
	UPROPERTY(Replicated)
	int32 Credits = 800;

	UPROPERTY(Replicated)
	int32 Kills = 0;

	UPROPERTY(Replicated)
	int32 Deaths = 0;
};
