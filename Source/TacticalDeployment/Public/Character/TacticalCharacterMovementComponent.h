// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Character/TakeDamageTagging.h"
#include "TacticalCharacterMovementComponent.generated.h"

/**
 * Competitive ground movement on top of the CMC's proven client prediction / server
 * reconciliation (see Docs/ARCHITECTURE.md for the Mover 2.0 migration outline).
 *
 *  - Snappy: high acceleration + high ground friction = counter-strafing kills velocity in a
 *    handful of 128 Hz ticks, while simply releasing a key brakes noticeably slower.
 *  - Deterministic: sub-steps capped at 7.8125 ms keep 60 FPS and 360 FPS clients within a few
 *    centimetres of each other, and the server replays each client's exact steps.
 *  - Predicted intents: shift-walk and the plant/defuse lock ride in the compressed move flags,
 *    so the server applies them on exactly the same moves the client did (no corrections).
 *  - Tagging: bullet hits scale max speed through FTaggingState, evaluated on the move clock.
 */
UCLASS()
class TACTICALDEPLOYMENT_API UTacticalCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	friend class FSavedMove_Tactical;

public:
	UTacticalCharacterMovementComponent();

	// --- Predicted intents --------------------------------------------------------------

	void SetWantsToShiftWalk(bool bWants) { bWantsToShiftWalk = bWants; }
	bool WantsToShiftWalk() const { return bWantsToShiftWalk; }

	/** Plant/defuse: freezes the player for the whole interaction. */
	void SetWantsInteractLock(bool bWants) { bWantsInteractLock = bWants; }
	bool IsInteractLocked() const { return bWantsInteractLock; }

	// --- Tagging ------------------------------------------------------------------------

	/** Authority only. Returns the new state so the owning character can replicate it. */
	const FTaggingState& ServerApplyTag(const FDamageTagParams& Params);

	/** Owning client: adopt the server's tag (from the character's OnRep). */
	void SetTaggingState(const FTaggingState& State) { TaggingState = State; }

	const FTaggingState& GetTaggingState() const { return TaggingState; }

	/** Scalar for the move currently being simulated (or "now" outside of a move). */
	float GetTaggingSpeedScalar() const { return TaggingState.Evaluate(GetMoveClockTime()); }

	// --- Tuning -------------------------------------------------------------------------

	/** Run speed lives in MaxWalkSpeed; this is the silent shift-walk speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactical Movement", meta = (Units = "cm/s"))
	float MaxShiftWalkSpeed = 405.f;

	float GetMaxRunSpeed() const { return MaxWalkSpeed; }

	//~ UCharacterMovementComponent
	virtual float GetMaxSpeed() const override;
	virtual bool CanAttemptJump() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;

protected:
	/**
	 * The clock tags are expressed in:
	 *  - inside MoveAutonomous (server processing / client replaying): that move's client timestamp;
	 *  - owning client creating a new move: the client's current move timestamp;
	 *  - otherwise (listen host, AI): world time.
	 */
	float GetMoveClockTime() const;

	/** Server: the remote client's clock position = timestamp of the last move we processed. */
	float GetServerMoveClockNow() const;

	bool IsMovementPhaseLocked() const;

	uint8 bWantsToShiftWalk : 1;
	uint8 bWantsInteractLock : 1;
	uint8 bInMoveAutonomous : 1;

	float CurrentMoveTimeStamp = 0.f;

	FTaggingState TaggingState;
};

/** Saved move carrying our intents so replays and server moves agree bit-for-bit. */
class FSavedMove_Tactical : public FSavedMove_Character
{
public:
	using Super = FSavedMove_Character;

	// FLAG_Custom_0 / FLAG_Custom_1 of FSavedMove_Character::CompressedFlags.
	static constexpr uint8 FLAG_ShiftWalk = FLAG_Custom_0;
	static constexpr uint8 FLAG_InteractLock = FLAG_Custom_1;

	uint8 bSavedWantsToShiftWalk : 1;
	uint8 bSavedWantsInteractLock : 1;

	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* C) override;
};

class FNetworkPredictionData_Client_Tactical : public FNetworkPredictionData_Client_Character
{
public:
	using Super = FNetworkPredictionData_Client_Character;

	explicit FNetworkPredictionData_Client_Tactical(const UCharacterMovementComponent& ClientMovement);

	virtual FSavedMovePtr AllocateNewMove() override;
};
