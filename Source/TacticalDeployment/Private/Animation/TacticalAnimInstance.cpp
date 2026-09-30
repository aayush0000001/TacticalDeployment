// Copyright TacticalDeployment. All Rights Reserved.

#include "Animation/TacticalAnimInstance.h"
#include "Character/TacticalCharacter.h"
#include "Character/TacticalCharacterMovementComponent.h"

void UTacticalAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	Character = Cast<ATacticalCharacter>(TryGetPawnOwner());
}

void UTacticalAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	// Game thread: only copy raw state. Everything derived is computed off-thread below.
	const ATacticalCharacter* Owner = Character.Get();
	if (!Owner)
	{
		return;
	}

	// Owner/server: full-precision controller rotation. Simulated proxy: 16-bit replicated pitch
	// + 16-bit actor yaw (see ATacticalCharacter::GetBaseAimRotation).
	AimRotation = Owner->GetBaseAimRotation();
	ActorRotation = Owner->GetActorRotation();
	Velocity = Owner->GetVelocity();

	const UTacticalCharacterMovementComponent* Movement = Owner->GetTacticalMovement();
	bIsCrouching = Movement->IsCrouching();
	bIsFalling = Movement->IsFalling();
	bIsShiftWalking = Movement->WantsToShiftWalk();
	bIsInteracting = Movement->IsInteractLocked();
	bIsDead = !Owner->IsAlive();
}

void UTacticalAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);

	const FRotator Delta = (AimRotation - ActorRotation).GetNormalized();
	AimPitch = FMath::Clamp(Delta.Pitch, -90.f, 90.f);
	AimYaw = Delta.Yaw;
	SpinePitchPerBone = AimPitch / static_cast<float>(FMath::Max(NumSpineBones, 1));

	GroundSpeed = Velocity.Size2D();
	Direction = GroundSpeed > 1.f ? FRotator::NormalizeAxis(Velocity.Rotation().Yaw - ActorRotation.Yaw) : 0.f;
}
