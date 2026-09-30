// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

// Engine-light procedural viewmodel motion (weapon sway, movement bob, shot kick, landing dip).
// Purely cosmetic: it moves Mesh1P only, never the camera or the aim. Built on exact critically
// damped springs, so a 60 FPS and a 360 FPS client see the same motion. Tested in Tools/Simulation.

#include "CoreMinimal.h"

/**
 * Exact critically damped spring. Integrates the closed-form solution instead of Euler steps, so
 * it is frame-rate independent for a constant target and unconditionally stable for any step.
 */
struct FCriticalSpring
{
	float Value = 0.f;
	float Velocity = 0.f;

	void Update(float Target, float Omega, float DeltaTime)
	{
		const float X0 = Value - Target;
		const float Decay = FMath::Exp(-Omega * DeltaTime);
		const float Carry = (Velocity + Omega * X0) * DeltaTime;
		Value = Target + (X0 + Carry) * Decay;
		Velocity = (Velocity - Omega * Carry) * Decay;
	}

	void AddImpulse(float DeltaVelocity) { Velocity += DeltaVelocity; }
};

struct FViewmodelMotionSettings
{
	// Sway: the weapon lags behind fast mouse movement, then settles.
	float SwayGain = 0.012f;        // Degrees of lag per deg/s of look rate.
	float MaxSwayDegrees = 3.5f;
	float SwayStiffness = 14.f;     // Spring omega (rad/s).

	// Bob: stride-synced figure-eight while moving on the ground.
	float BobAmplitude = 0.55f;     // cm at full run speed.
	float BobFrequency = 1.9f;      // Strides per second at full run speed.
	float BobStiffness = 10.f;

	// Shot kick: backward shove + muzzle climb, per unit of kick strength.
	float KickBackVelocity = 140.f; // cm/s impulse.
	float KickPitchVelocity = 55.f; // deg/s impulse.
	float KickStiffness = 24.f;

	// Landing dip, proportional to landing speed.
	float LandingDipPerSpeed = 0.012f; // cm/s of dip velocity per cm/s of impact.
	float MaxLandingDipVelocity = 18.f;
	float LandingStiffness = 12.f;
};

struct FViewmodelMotionInput
{
	float DeltaTime = 0.f;
	float LookYawRate = 0.f;   // deg/s, +right.
	float LookPitchRate = 0.f; // deg/s, +up.
	float SpeedRatio = 0.f;    // Horizontal speed / run speed.
	bool bOnGround = true;
};

class FViewmodelMotion
{
public:
	void AddShotKick(float Strength, const FViewmodelMotionSettings& Settings)
	{
		KickBack.AddImpulse(Settings.KickBackVelocity * Strength);
		KickPitch.AddImpulse(Settings.KickPitchVelocity * Strength);
	}

	void AddLanding(float ImpactSpeed, const FViewmodelMotionSettings& Settings)
	{
		LandingDrop.AddImpulse(-FMath::Min(ImpactSpeed * Settings.LandingDipPerSpeed, Settings.MaxLandingDipVelocity));
	}

	void Update(const FViewmodelMotionInput& In, const FViewmodelMotionSettings& Settings)
	{
		// Hitches: the springs are exact for any step, but a huge bob phase jump reads as a glitch.
		const float Dt = FMath::Clamp(In.DeltaTime, 0.f, 0.1f);

		const float MaxSway = Settings.MaxSwayDegrees;
		SwayYaw.Update(FMath::Clamp(-In.LookYawRate * Settings.SwayGain, -MaxSway, MaxSway), Settings.SwayStiffness, Dt);
		SwayPitch.Update(FMath::Clamp(-In.LookPitchRate * Settings.SwayGain, -MaxSway, MaxSway), Settings.SwayStiffness, Dt);

		const float Speed = FMath::Clamp(In.SpeedRatio, 0.f, 1.2f);
		BobWeight.Update(In.bOnGround ? Speed : 0.f, Settings.BobStiffness, Dt);
		BobPhase = FMath::Fmod(BobPhase + 2.f * UE_PI * Settings.BobFrequency * Speed * Dt, 2.f * UE_PI);

		KickBack.Update(0.f, Settings.KickStiffness, Dt);
		KickPitch.Update(0.f, Settings.KickStiffness, Dt);
		LandingDrop.Update(0.f, Settings.LandingStiffness, Dt);

		const float Bob = Settings.BobAmplitude * BobWeight.Value;
		LocationOffset = FVector(-KickBack.Value, FMath::Sin(BobPhase) * Bob, -FMath::Abs(FMath::Cos(BobPhase)) * Bob * 0.6f + LandingDrop.Value);
		RotationOffset = FRotator(SwayPitch.Value + KickPitch.Value, SwayYaw.Value, SwayYaw.Value * 0.6f);
	}

	const FVector& GetLocationOffset() const { return LocationOffset; }
	const FRotator& GetRotationOffset() const { return RotationOffset; }

	void Reset() { *this = FViewmodelMotion(); }

private:
	FCriticalSpring SwayYaw;
	FCriticalSpring SwayPitch;
	FCriticalSpring BobWeight;
	FCriticalSpring KickBack;
	FCriticalSpring KickPitch;
	FCriticalSpring LandingDrop;
	float BobPhase = 0.f;
	FVector LocationOffset = FVector::ZeroVector;
	FRotator RotationOffset = FRotator::ZeroRotator;
};
