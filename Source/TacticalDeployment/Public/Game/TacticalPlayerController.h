// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/ClockSync.h"
#include "TacticalPlayerController.generated.h"

class UWeaponStats;

/**
 * Owns the client's clock synchronisation (NTP-style) used to stamp fire requests, and the
 * shop RPCs. All purchase validation happens in ATacticalGameMode.
 */
UCLASS()
class TACTICALDEPLOYMENT_API ATacticalPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATacticalPlayerController();

	/** Client estimate of the *current* server time. */
	double GetEstimatedServerTime() const;

	/**
	 * Server time of the world this client is currently rendering:
	 * now - downstream leg (RTT/2) - proxy interpolation delay. Sent with every shot.
	 */
	double GetClientViewTime() const;

	float GetSmoothedRoundTripTime() const { return static_cast<float>(ClockSync.GetSmoothedRoundTrip()); }

	UFUNCTION(Server, Reliable)
	void Server_PurchaseWeapon(const UWeaponStats* Weapon);

	UFUNCTION(Server, Reliable)
	void Server_PurchaseArmor(bool bHeavy);

	//~ AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	void SendTimeSyncRequest();

	UFUNCTION(Server, Unreliable)
	void Server_RequestTimeSync(double ClientSendTime);

	UFUNCTION(Client, Unreliable)
	void Client_ReportTimeSync(double ClientSendTime, double ServerTime);

	/** Seconds between clock sync probes. */
	UPROPERTY(EditDefaultsOnly, Category = "Networking")
	float TimeSyncInterval = 0.5f;

	FTacticalClockSync ClockSync;

	FTimerHandle TimeSyncTimer;
};
