// Copyright TacticalDeployment. All Rights Reserved.
//
// Ground-velocity model of UCharacterMovementComponent (UE 5.4): PhysWalking's sub-stepping
// (GetSimulationTimeStep), CalcVelocity and ApplyVelocityBraking, reimplemented from the engine
// algorithm for flat ground with no root motion or path following. Driven by the project's real
// tuning (TacticalMovementTuning.h) so the counter-strafe numbers in the docs are measured, not
// estimated.

#pragma once

#include "CoreMinimal.h"
#include "Character/TacticalMovementTuning.h"
#include "Core/TacticalTypes.h"

struct FCmcModel
{
	static constexpr float MinTickTime = 1e-6f;
	static constexpr float BrakeToStopVelocity = 10.f;

	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;

	float MaxAcceleration = TacticalMovementTuning::MaxAcceleration;
	float GroundFriction = TacticalMovementTuning::GroundFriction;
	float BrakingDeceleration = TacticalMovementTuning::BrakingDeceleration;
	float BrakingFriction = TacticalMovementTuning::BrakingFriction;
	float BrakingFrictionFactor = TacticalMovementTuning::BrakingFrictionFactor;
	float BrakingSubStepTime = TacticalMovementTuning::BrakingSubStepTime;
	bool bUseSeparateBrakingFriction = true;
	float MaxSimulationTimeStep = TacticalNet::ServerFrameTime;
	int32 MaxSimulationIterations = 16;

	bool IsExceedingMaxSpeed(float MaxSpeed) const
	{
		return Velocity.SizeSquared() > FMath::Square(double(MaxSpeed)) * 1.01;
	}

	void ApplyVelocityBraking(float DeltaTime, float Friction, float BrakingDecel)
	{
		if (Velocity.IsZero() || DeltaTime < MinTickTime) { return; }
		Friction = FMath::Max(0.f, Friction * FMath::Max(0.f, BrakingFrictionFactor));
		BrakingDecel = FMath::Max(0.f, BrakingDecel);
		const bool bZeroFriction = Friction == 0.f;
		const bool bZeroBraking = BrakingDecel == 0.f;
		if (bZeroFriction && bZeroBraking) { return; }

		const FVector OldVel = Velocity;
		float RemainingTime = DeltaTime;
		const float MaxTimeStep = FMath::Clamp(BrakingSubStepTime, 1.f / 75.f, 1.f / 20.f);
		const FVector RevAccel = bZeroBraking ? FVector::ZeroVector : Velocity.GetSafeNormal() * double(-BrakingDecel);
		while (RemainingTime >= MinTickTime)
		{
			const float Dt = (RemainingTime > MaxTimeStep && !bZeroFriction) ? FMath::Min(MaxTimeStep, RemainingTime * 0.5f) : RemainingTime;
			RemainingTime -= Dt;
			Velocity = Velocity + (Velocity * double(-Friction) + RevAccel) * double(Dt);
			if ((Velocity | OldVel) <= 0.0)
			{
				Velocity = FVector::ZeroVector;
				return;
			}
		}
		const double VSizeSq = Velocity.SizeSquared();
		if (VSizeSq <= UE_KINDA_SMALL_NUMBER || (!bZeroBraking && VSizeSq <= FMath::Square(double(BrakeToStopVelocity))))
		{
			Velocity = FVector::ZeroVector;
		}
	}

	/** CalcVelocity for walking: Acceleration = input (|input| <= 1) * MaxAcceleration. */
	void CalcVelocity(float DeltaTime, const FVector& Acceleration, float MaxSpeed)
	{
		const bool bZeroAcceleration = Acceleration.IsZero();
		const bool bVelocityOverMax = IsExceedingMaxSpeed(MaxSpeed);

		if (bZeroAcceleration || bVelocityOverMax)
		{
			const FVector OldVelocity = Velocity;
			ApplyVelocityBraking(DeltaTime, bUseSeparateBrakingFriction ? BrakingFriction : GroundFriction, BrakingDeceleration);
			if (bVelocityOverMax && Velocity.SizeSquared() < FMath::Square(double(MaxSpeed)) && (Acceleration | OldVelocity) > 0.0)
			{
				Velocity = OldVelocity.GetSafeNormal() * double(MaxSpeed);
			}
		}
		else
		{
			// Friction bends velocity toward the input direction (the counter-strafe mechanic).
			const FVector AccelDir = Acceleration.GetSafeNormal();
			const double VelSize = Velocity.Size();
			Velocity = Velocity - (Velocity - AccelDir * VelSize) * double(FMath::Min(DeltaTime * GroundFriction, 1.f));
		}

		if (!bZeroAcceleration)
		{
			const double NewMaxInputSpeed = IsExceedingMaxSpeed(MaxSpeed) ? Velocity.Size() : double(MaxSpeed);
			Velocity = Velocity + Acceleration * double(DeltaTime);
			const double Size = Velocity.Size();
			if (Size > NewMaxInputSpeed) { Velocity = Velocity * (NewMaxInputSpeed / Size); }
		}
	}

	float GetSimulationTimeStep(float RemainingTime, int32 Iterations) const
	{
		if (RemainingTime > MaxSimulationTimeStep && Iterations < MaxSimulationIterations)
		{
			RemainingTime = FMath::Min(MaxSimulationTimeStep, RemainingTime * 0.5f);
		}
		return FMath::Max(MinTickTime, RemainingTime);
	}

	/** One client frame of PhysWalking with a constant input (unit-length or zero). */
	void SimulateFrame(float FrameDeltaTime, const FVector& InputDirection, float MaxSpeed)
	{
		const FVector Acceleration = InputDirection * double(MaxAcceleration);
		float RemainingTime = FrameDeltaTime;
		int32 Iterations = 0;
		while (RemainingTime >= MinTickTime && Iterations < MaxSimulationIterations)
		{
			++Iterations;
			const float TimeTick = GetSimulationTimeStep(RemainingTime, Iterations);
			RemainingTime -= TimeTick;
			CalcVelocity(TimeTick, Acceleration, MaxSpeed);
			Location = Location + Velocity * double(TimeTick);
		}
	}
};
