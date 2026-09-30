// Copyright TacticalDeployment. All Rights Reserved.
// Presentation layer: viewmodel motion (Core/ViewmodelMotion.h) and HUD math (Core/HudMath.h).

#include "SimTest.h"
#include "Core/HudMath.h"
#include "Core/ViewmodelMotion.h"
#include "Weapons/WeaponStats.h"

#include <cmath>

SIM_TEST(Presentation, CriticalSpringIsFrameRateIndependent)
{
	// Same impulse, same 100 ms of simulated time (mid-motion), very different frame rates: same state.
	float Reference = 0.f;
	for (const int32 Fps : { 30, 60, 120, 240, 360 })
	{
		FCriticalSpring Spring;
		Spring.Value = 3.f;
		Spring.AddImpulse(120.f);
		const int32 Steps = Fps / 10; // Exactly 100 ms at every rate listed.
		for (int32 i = 0; i < Steps; ++i) { Spring.Update(0.f, 24.f, 1.f / Fps); }
		if (Fps == 30) { Reference = Spring.Value; }
		SIM_EXPECT_NEAR(Spring.Value, Reference, 1e-4 * std::abs(Reference) + 1e-5);
	}
	SimTest::Report("spring state 100 ms after a kick at 30..360 FPS: %.5f (identical to 0.01%%)", Reference);
}

SIM_TEST(Presentation, CriticalSpringIsStableForHugeSteps)
{
	FCriticalSpring Spring;
	Spring.Value = 5.f;
	Spring.AddImpulse(1000.f);
	for (int32 i = 0; i < 10; ++i)
	{
		Spring.Update(0.f, 24.f, 2.0f); // 2 s hitch per step.
		SIM_EXPECT_TRUE(std::isfinite(Spring.Value) && std::abs(Spring.Value) < 5.f);
	}
	SIM_EXPECT_NEAR(Spring.Value, 0.0, 1e-3);
}

SIM_TEST(Presentation, ShotKickRecoversQuicklyWithoutOvershoot)
{
	FViewmodelMotionSettings Settings;
	FViewmodelMotion Motion;
	Motion.AddShotKick(1.f, Settings);
	float Peak = 0.f, MinAfterPeak = 1e9f;
	double RecoveredAt = -1.0;
	for (int32 i = 0; i < 128; ++i)
	{
		FViewmodelMotionInput In;
		In.DeltaTime = 1.f / 128.f;
		Motion.Update(In, Settings);
		const float Back = -float(Motion.GetLocationOffset().X);
		Peak = std::max(Peak, Back);
		if (Back < Peak) { MinAfterPeak = std::min(MinAfterPeak, Back); }
		if (RecoveredAt < 0.0 && Peak > 0.f && Back < Peak * 0.05f) { RecoveredAt = (i + 1) / 128.0; }
	}
	SIM_EXPECT_GT(Peak, 1.f);
	SIM_EXPECT_GE(MinAfterPeak, -1e-3); // Critically damped: never springs forward past rest.
	SIM_EXPECT_GT(RecoveredAt, 0.0);
	SIM_EXPECT_LE(RecoveredAt, 0.25);
	SimTest::Report("shot kick: %.2f cm back, recovered to 5%% in %.0f ms, no overshoot", Peak, RecoveredAt * 1000.0);
}

SIM_TEST(Presentation, SwayIsBoundedAndSettles)
{
	FViewmodelMotionSettings Settings;
	FViewmodelMotion Motion;
	float MaxYaw = 0.f;
	for (int32 i = 0; i < 64; ++i) // Violent 5000 deg/s flick for half a second.
	{
		FViewmodelMotionInput In;
		In.DeltaTime = 1.f / 128.f;
		In.LookYawRate = 5000.f;
		Motion.Update(In, Settings);
		MaxYaw = std::max(MaxYaw, std::abs(float(Motion.GetRotationOffset().Yaw)));
	}
	SIM_EXPECT_LE(MaxYaw, Settings.MaxSwayDegrees + 1e-3);
	for (int32 i = 0; i < 128; ++i)
	{
		FViewmodelMotionInput In;
		In.DeltaTime = 1.f / 128.f;
		Motion.Update(In, Settings);
	}
	SIM_EXPECT_NEAR(Motion.GetRotationOffset().Yaw, 0.0, 0.01);
	SimTest::Report("5000 deg/s flick: sway capped at %.2f deg, settled to <0.01 deg within 1 s", MaxYaw);
}

SIM_TEST(Presentation, BobOnlyWhileMovingOnGround)
{
	FViewmodelMotionSettings Settings;
	auto MaxBob = [&](float SpeedRatio, bool bOnGround)
	{
		FViewmodelMotion Motion;
		double Max = 0.0;
		for (int32 i = 0; i < 512; ++i)
		{
			FViewmodelMotionInput In;
			In.DeltaTime = 1.f / 128.f;
			In.SpeedRatio = SpeedRatio;
			In.bOnGround = bOnGround;
			Motion.Update(In, Settings);
			if (i > 256) { Max = std::max(Max, std::abs(Motion.GetLocationOffset().Y)); }
		}
		return Max;
	};
	SIM_EXPECT_NEAR(MaxBob(0.f, true), 0.0, 1e-6);
	SIM_EXPECT_NEAR(MaxBob(1.f, false), 0.0, 1e-3); // Airborne: no stride bob.
	const double Walk = MaxBob(0.6f, true), Run = MaxBob(1.f, true);
	SIM_EXPECT_GT(Run, Walk);
	SIM_EXPECT_LE(Run, Settings.BobAmplitude + 1e-3);
	SimTest::Report("side bob: still 0, shift-walk %.2f cm, run %.2f cm, airborne 0", Walk, Run);
}

SIM_TEST(Presentation, CrosshairRadiusMatchesProjectedSpread)
{
	// Project the edge of a spread cone through a pinhole camera and compare with SpreadToPixels.
	const float Fov = 103.f, Width = 1920.f;
	for (const float Spread : { 0.1f, 1.f, 5.f, 10.f })
	{
		const double Focal = (Width * 0.5) / std::tan(FMath::DegreesToRadians(Fov * 0.5f));
		const double Projected = Focal * std::tan(FMath::DegreesToRadians(Spread));
		SIM_EXPECT_NEAR(HudMath::SpreadToPixels(Spread, Fov, Width), Projected, 1e-2);
	}
	UWeaponStats Rifle;
	SimTest::Report("103 deg FOV at 1920 px: still %.1f px, walking %.1f px, running %.1f px radius",
		HudMath::SpreadToPixels(Rifle.BaseSpread, Fov, Width),
		HudMath::SpreadToPixels(Rifle.BaseSpread + Rifle.WalkSpread, Fov, Width),
		HudMath::SpreadToPixels(Rifle.BaseSpread + Rifle.RunSpread, Fov, Width));
	SIM_EXPECT_NEAR(HudMath::SpreadToPixels(0.f, Fov, Width), 0.0, 0.0);
}

SIM_TEST(Presentation, HudTimersAndFades)
{
	int32 M = 0, S = 0;
	HudMath::SplitClock(100.f, M, S);  SIM_EXPECT_TRUE(M == 1 && S == 40);
	HudMath::SplitClock(0.2f, M, S);   SIM_EXPECT_TRUE(M == 0 && S == 1);  // Rounds up: never shows 0:00 early.
	HudMath::SplitClock(-3.f, M, S);   SIM_EXPECT_TRUE(M == 0 && S == 0);
	SIM_EXPECT_NEAR(HudMath::FadeAlpha(10.0, 10.0, 0.25f), 1.0, 1e-6);
	SIM_EXPECT_NEAR(HudMath::FadeAlpha(10.125, 10.0, 0.25f), 0.5, 1e-6);
	SIM_EXPECT_NEAR(HudMath::FadeAlpha(11.0, 10.0, 0.25f), 0.0, 0.0);
	SIM_EXPECT_NEAR(HudMath::FadeAlpha(9.0, 10.0, 0.25f), 0.0, 0.0);
}
