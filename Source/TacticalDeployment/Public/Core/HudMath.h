// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

// Engine-light HUD math used by ATacticalHUD and tested in Tools/Simulation.

#include "CoreMinimal.h"

namespace HudMath
{
	/**
	 * Screen-space radius (px) of a spread cone of the given half-angle. UE keeps the horizontal
	 * FOV fixed (AspectRatio_MaintainXFOV), so this holds for any aspect ratio. An "honest"
	 * crosshair drawn at this radius encloses exactly where bullets can land.
	 */
	inline float SpreadToPixels(float SpreadHalfAngleDegrees, float HorizontalFovDegrees, float ViewportWidth)
	{
		const float HalfFov = FMath::DegreesToRadians(FMath::Clamp(HorizontalFovDegrees, 1.f, 170.f) * 0.5f);
		const float Spread = FMath::DegreesToRadians(FMath::Clamp(SpreadHalfAngleDegrees, 0.f, 89.f));
		return FMath::Tan(Spread) / FMath::Tan(HalfFov) * (ViewportWidth * 0.5f);
	}

	/** 1 at the event, linearly to 0 after Duration; 0 before the event. */
	inline float FadeAlpha(double Now, double EventTime, float Duration)
	{
		if (Duration <= 0.f || Now < EventTime)
		{
			return 0.f;
		}
		return FMath::Clamp(1.f - static_cast<float>((Now - EventTime) / Duration), 0.f, 1.f);
	}

	/** "m:ss" for countdowns; never negative. */
	inline void SplitClock(float Seconds, int32& OutMinutes, int32& OutSeconds)
	{
		const int32 Total = FMath::Max(0, FMath::CeilToInt(Seconds));
		OutMinutes = Total / 60;
		OutSeconds = Total % 60;
	}
}
