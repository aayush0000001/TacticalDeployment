// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "TacticalAnimInstance.generated.h"

class ATacticalCharacter;

/**
 * Native base for both the 3P body AnimBP and the 1P arms AnimBP.
 *
 * AimOffset contract (3P): the AnimBP feeds AimPitch/AimYaw into a 2D AimOffset blend space
 * (mesh-space additive, pitch -90..90) applied after locomotion, distributing pitch over
 * spine_01..spine_03 + neck. Because the *same* AnimBP runs on the dedicated server (Mesh3P
 * always ticks there), the head hitbox the server rewinds is posed exactly as every client
 * renders it. Never add client-only smoothing to AimPitch on the 3P mesh: it would desync
 * visuals from hitboxes.
 */
UCLASS()
class TACTICALDEPLOYMENT_API UTacticalAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;

protected:
	/** Degrees, -90 (down) .. 90 (up). AimOffset vertical axis. */
	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	float AimPitch = 0.f;

	/** Degrees between aim yaw and body yaw. ~0 while alive (controller drives yaw). */
	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	float AimYaw = 0.f;

	/** For a procedural alternative to the blend space: Transform (Modify) Bone per spine bone. */
	UPROPERTY(BlueprintReadOnly, Category = "Aim")
	float SpinePitchPerBone = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Aim", meta = (ClampMin = "1"))
	int32 NumSpineBones = 4;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float GroundSpeed = 0.f;

	/** -180..180, velocity direction relative to facing (strafe blend spaces). */
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float Direction = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bIsCrouching = false;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bIsFalling = false;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bIsShiftWalking = false;

	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bIsInteracting = false;

	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bIsDead = false;

private:
	TWeakObjectPtr<ATacticalCharacter> Character;

	// Snapshot taken on the game thread, consumed on the worker thread.
	FRotator AimRotation = FRotator::ZeroRotator;
	FRotator ActorRotation = FRotator::ZeroRotator;
	FVector Velocity = FVector::ZeroVector;
};
