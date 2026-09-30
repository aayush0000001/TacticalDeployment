// Copyright TacticalDeployment. All Rights Reserved.

#include "Character/TacticalCharacterMovementComponent.h"
#include "Character/TacticalCharacter.h"
#include "Game/TacticalGameState.h"
#include "Core/TacticalTypes.h"
#include "Engine/World.h"

UTacticalCharacterMovementComponent::UTacticalCharacterMovementComponent()
	: bWantsToShiftWalk(false)
	, bWantsInteractLock(false)
	, bInMoveAutonomous(false)
{
	// --- Speeds (cm/s) ----------------------------------------------------------------
	MaxWalkSpeed = 675.f;           // Run.
	MaxShiftWalkSpeed = 405.f;      // Silent walk (60%).
	MaxWalkSpeedCrouched = 230.f;

	// --- Snappy ground model ----------------------------------------------------------
	// With input opposing velocity, CalcVelocity bends velocity toward the input direction at
	// rate GroundFriction, then adds MaxAcceleration. At 128 Hz this removes ~19% of velocity per
	// tick from friction alone, so a counter-strafe drops below the weapon's accuracy threshold
	// (30% of run speed) in ~45 ms. Releasing the key only applies braking: ~200 ms. That gap is
	// the skill expression.
	MaxAcceleration = 5200.f;
	GroundFriction = 12.f;
	BrakingDecelerationWalking = 3400.f;
	bUseSeparateBrakingFriction = true;
	BrakingFriction = 4.f;
	BrakingFrictionFactor = 1.f;
	BrakingSubStepTime = 1.f / 128.f;

	// --- Air ----------------------------------------------------------------------------
	JumpZVelocity = 440.f;
	GravityScale = 1.25f;
	AirControl = 0.25f;
	BrakingDecelerationFalling = 0.f;
	FallingLateralFriction = 0.f;

	// --- Crouch -------------------------------------------------------------------------
	GetNavAgentPropertiesRef().bCanCrouch = true;
	bCanWalkOffLedgesWhenCrouching = true;
	SetCrouchedHalfHeight(60.f);

	// --- Determinism --------------------------------------------------------------------
	// Sub-step at exactly one server frame so every client framerate integrates identically.
	MaxSimulationTimeStep = TacticalNet::ServerFrameTime;
	MaxSimulationIterations = 16;
	MaxJumpApexAttemptsPerSimulation = 2;
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = false;

	// --- Networking ---------------------------------------------------------------------
	// Linear smoothing of simulated proxies has a *known*, fixed render delay, which lag
	// compensation needs (TacticalNet::ProxyInterpolationDelay). Exponential smoothing does not.
	NetworkSmoothingMode = ENetworkSmoothingMode::Linear;
	NetworkSimulatedSmoothLocationTime = TacticalNet::ProxyInterpolationDelay;
	NetworkSimulatedSmoothRotationTime = TacticalNet::ProxyInterpolationDelay;
	ListenServerNetworkSimulatedSmoothLocationTime = TacticalNet::ProxyInterpolationDelay;
	ListenServerNetworkSimulatedSmoothRotationTime = TacticalNet::ProxyInterpolationDelay;
	bNetworkAlwaysReplicateTransformUpdateTimestamp = true; // Required for Linear smoothing.
	NetworkMaxSmoothUpdateDistance = 92.f;
	NetworkNoSmoothUpdateDistance = 140.f;
	bNetworkSkipProxyPredictionOnNetUpdate = true; // Never extrapolate enemies past the server state.
}

float UTacticalCharacterMovementComponent::GetMaxSpeed() const
{
	if (IsMovementPhaseLocked())
	{
		return 0.f;
	}

	const bool bOnGround = IsMovingOnGround();
	if (bWantsInteractLock && bOnGround)
	{
		return 0.f;
	}

	float Speed = Super::GetMaxSpeed(); // Handles crouch / swim / fly.
	if (bOnGround && bWantsToShiftWalk && !IsCrouching())
	{
		Speed = FMath::Min(Speed, MaxShiftWalkSpeed);
	}

	if (const ATacticalCharacter* TacticalOwner = Cast<ATacticalCharacter>(CharacterOwner))
	{
		Speed *= TacticalOwner->GetEquippedMovementMultiplier();
	}

	return Speed * GetTaggingSpeedScalar();
}

bool UTacticalCharacterMovementComponent::CanAttemptJump() const
{
	return !bWantsInteractLock && !IsMovementPhaseLocked() && Super::CanAttemptJump();
}

bool UTacticalCharacterMovementComponent::IsMovementPhaseLocked() const
{
	// Derived from replicated round state on both sides: no extra replication. The client learns
	// of a phase change RTT/2 late, which costs at most one correction per phase transition.
	const UWorld* World = GetWorld();
	const ATacticalGameState* GameState = World ? World->GetGameState<ATacticalGameState>() : nullptr;
	return GameState && GameState->IsMovementLocked();
}

void UTacticalCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToShiftWalk = (Flags & FSavedMove_Tactical::FLAG_ShiftWalk) != 0;
	bWantsInteractLock = (Flags & FSavedMove_Tactical::FLAG_InteractLock) != 0;
}

void UTacticalCharacterMovementComponent::MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel)
{
	// Server processing a client move, or the client replaying a saved move after a correction:
	// every speed query inside this move evaluates tagging at the move's own timestamp.
	TGuardValue<float> TimeGuard(CurrentMoveTimeStamp, ClientTimeStamp);
	bInMoveAutonomous = true;
	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
	bInMoveAutonomous = false;
}

float UTacticalCharacterMovementComponent::GetMoveClockTime() const
{
	if (bInMoveAutonomous)
	{
		return CurrentMoveTimeStamp;
	}
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy)
	{
		if (const FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character())
		{
			return ClientData->CurrentTimeStamp; // New move being simulated right now.
		}
	}
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_Authority && !CharacterOwner->IsLocallyControlled())
	{
		return GetServerMoveClockNow();
	}
	const UWorld* World = GetWorld();
	return World ? static_cast<float>(World->GetTimeSeconds()) : 0.f;
}

float UTacticalCharacterMovementComponent::GetServerMoveClockNow() const
{
	if (CharacterOwner && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy)
	{
		if (const FNetworkPredictionData_Server_Character* ServerData = GetPredictionData_Server_Character())
		{
			return ServerData->CurrentClientTimeStamp;
		}
	}
	const UWorld* World = GetWorld();
	return World ? static_cast<float>(World->GetTimeSeconds()) : 0.f;
}

const FTaggingState& UTacticalCharacterMovementComponent::ServerApplyTag(const FDamageTagParams& Params)
{
	check(CharacterOwner && CharacterOwner->HasAuthority());

	const float Now = CharacterOwner->IsLocallyControlled()
		? static_cast<float>(GetWorld()->GetTimeSeconds())
		: GetServerMoveClockNow();

	// Stacking: never weaken an active tag; a fresh hit restarts the ease-out.
	const float RemainingSlow = 1.f - TaggingState.Evaluate(Now);

	FTaggingState NewState;
	NewState.StartMoveTime = Now;
	NewState.Duration = Params.Duration;
	NewState.SetSlow(FMath::Max(Params.SlowFraction, RemainingSlow));
	TaggingState = NewState;
	return TaggingState;
}

FNetworkPredictionData_Client* UTacticalCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UTacticalCharacterMovementComponent* MutableThis = const_cast<UTacticalCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Tactical(*this);
	}
	return ClientPredictionData;
}

// ---------------------------------------------------------------------------------------
// Saved move
// ---------------------------------------------------------------------------------------

void FSavedMove_Tactical::Clear()
{
	Super::Clear();
	bSavedWantsToShiftWalk = false;
	bSavedWantsInteractLock = false;
}

uint8 FSavedMove_Tactical::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();
	if (bSavedWantsToShiftWalk)
	{
		Result |= FLAG_ShiftWalk;
	}
	if (bSavedWantsInteractLock)
	{
		Result |= FLAG_InteractLock;
	}
	return Result;
}

bool FSavedMove_Tactical::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	const FSavedMove_Tactical* Other = static_cast<const FSavedMove_Tactical*>(NewMove.Get());
	if (bSavedWantsToShiftWalk != Other->bSavedWantsToShiftWalk || bSavedWantsInteractLock != Other->bSavedWantsInteractLock)
	{
		return false;
	}
	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void FSavedMove_Tactical::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);
	if (const UTacticalCharacterMovementComponent* Movement = Cast<UTacticalCharacterMovementComponent>(C->GetCharacterMovement()))
	{
		bSavedWantsToShiftWalk = Movement->bWantsToShiftWalk;
		bSavedWantsInteractLock = Movement->bWantsInteractLock;
	}
}

void FSavedMove_Tactical::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);
	if (UTacticalCharacterMovementComponent* Movement = Cast<UTacticalCharacterMovementComponent>(C->GetCharacterMovement()))
	{
		Movement->bWantsToShiftWalk = bSavedWantsToShiftWalk;
		Movement->bWantsInteractLock = bSavedWantsInteractLock;
	}
}

FNetworkPredictionData_Client_Tactical::FNetworkPredictionData_Client_Tactical(const UCharacterMovementComponent& ClientMovement)
	: Super(ClientMovement)
{
}

FSavedMovePtr FNetworkPredictionData_Client_Tactical::AllocateNewMove()
{
	return FSavedMovePtr(new FSavedMove_Tactical());
}
