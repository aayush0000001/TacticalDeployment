// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

// Engine-light shot resolution rules: the bullet path solver (bodies + wallbangs) and fire
// timestamp validation. ATacticalWeapon supplies world/hitbox queries as callables; the
// simulation harness supplies virtual ones.

#include "CoreMinimal.h"
#include "Weapons/WeaponStats.h"

struct FBulletSurfaceHit
{
	FVector ImpactPoint = FVector::ZeroVector;
	float Distance = 0.f; // From the segment start.
	float Density = 1.f;  // TNumericLimits<float>::Max() = impenetrable.
};

struct FBulletBodyHit
{
	FVector Location = FVector::ZeroVector;
	float Distance = 0.f; // From the segment start.
};

struct FBulletResult
{
	FVector ImpactPoint = FVector::ZeroVector;
	float TravelledDistance = 0.f; // Total distance to the impact, including material traversed.
	float DamageScale = 1.f;       // Remaining after penetration losses.
	int32 SurfacesPenetrated = 0;
	bool bHitBody = false;
};

namespace ShotRules
{
	/**
	 * Spends penetration budget for one surface: cost = thickness * density.
	 * @return false if the round stops inside the surface.
	 */
	inline bool ConsumePenetration(const FPenetrationTierParams& Tier, float Thickness, float Density, float& InOutRemainingPower, float& InOutDamageScale)
	{
		if (Density >= TNumericLimits<float>::Max())
		{
			return false;
		}
		const float Cost = Thickness * Density;
		if (Cost >= InOutRemainingPower)
		{
			return false;
		}
		InOutRemainingPower -= Cost;
		InOutDamageScale *= 1.f - (Cost / Tier.Power);
		return true;
	}

	/**
	 * Decides whether a backward exit probe (traced from EntryPoint + Dir * MaxThickness back to
	 * EntryPoint against the entered component) found a real far face.
	 *  - No hit: open/single-sided geometry, or nothing within MaxThickness.
	 *  - Start-penetrating: the probe began *inside* the solid (simple/convex collision reports an
	 *    initial overlap at the probe point), so the wall is thicker than MaxThickness.
	 *  - Hit at the entry point: the probe re-hit the entry face from inside (double-sided complex
	 *    collision), same conclusion.
	 */
	inline bool IsValidExitProbe(bool bProbeHit, bool bProbeStartedInside, const FVector& ExitPoint, const FVector& EntryPoint)
	{
		return bProbeHit && !bProbeStartedInside && FVector::DistSquared(ExitPoint, EntryPoint) >= FMath::Square(0.1);
	}

	/**
	 * Walks one bullet: open air (bodies) -> surface -> exit probe -> penetration budget -> repeat.
	 *
	 * @param TraceWorld  bool(const FVector& From, const FVector& To, FBulletSurfaceHit& Out): nearest world surface.
	 * @param ProbeExit   bool(const FBulletSurfaceHit& Entry, const FVector& Dir, float MaxThickness, FVector& OutExit):
	 *                    far face of the entered surface within MaxThickness.
	 * @param TraceBodies bool(const FVector& From, const FVector& To, FBulletBodyHit& Out): nearest (rewound) hitbox.
	 */
	template<typename TraceWorldFn, typename ProbeExitFn, typename TraceBodiesFn>
	FBulletResult SolveBulletPath(const FVector& Start, const FVector& Direction, float MaxRange, const FPenetrationTierParams& Tier,
		TraceWorldFn&& TraceWorld, ProbeExitFn&& ProbeExit, TraceBodiesFn&& TraceBodies)
	{
		FBulletResult Result;
		FVector SegmentStart = Start;
		float RemainingPower = Tier.Power;

		for (;;)
		{
			const float RemainingRange = MaxRange - Result.TravelledDistance;
			if (RemainingRange <= 0.f)
			{
				Result.ImpactPoint = SegmentStart;
				return Result;
			}

			const FVector SegmentEnd = SegmentStart + Direction * RemainingRange;
			FBulletSurfaceHit Surface;
			const bool bHitWorld = TraceWorld(SegmentStart, SegmentEnd, Surface);
			const FVector OpenEnd = bHitWorld ? Surface.ImpactPoint : SegmentEnd;

			// Bodies in the open air before the next surface.
			FBulletBodyHit Body;
			if (TraceBodies(SegmentStart, OpenEnd, Body))
			{
				Result.bHitBody = true;
				Result.ImpactPoint = Body.Location;
				Result.TravelledDistance += Body.Distance;
				return Result;
			}

			if (!bHitWorld)
			{
				Result.ImpactPoint = SegmentEnd;
				Result.TravelledDistance = MaxRange;
				return Result;
			}

			Result.TravelledDistance += Surface.Distance;
			Result.ImpactPoint = Surface.ImpactPoint;
			if (Result.SurfacesPenetrated >= Tier.MaxSurfaces)
			{
				return Result;
			}

			FVector Exit;
			if (Surface.Density >= TNumericLimits<float>::Max() || !ProbeExit(Surface, Direction, Tier.MaxThickness, Exit))
			{
				return Result; // Impenetrable, or thicker than this tier can probe.
			}

			const float Thickness = static_cast<float>(FVector::Dist(Surface.ImpactPoint, Exit));
			if (!ConsumePenetration(Tier, Thickness, Surface.Density, RemainingPower, Result.DamageScale))
			{
				return Result;
			}

			++Result.SurfacesPenetrated;
			Result.TravelledDistance += Thickness;
			SegmentStart = Exit + Direction * 0.5f; // Step off the exit face.
			Result.TravelledDistance += 0.5f;
		}
	}

	/**
	 * Server-side fire-rate gate (anti rapid-fire), fed with the client's view-time stamps.
	 *
	 * GCRA (generic cell rate algorithm): each accepted shot pushes a theoretical arrival time
	 * (TAT) one fire interval ahead; a shot may be early relative to TAT by at most BurstTolerance.
	 * That absorbs frame quantization of the client's looping fire timer (gaps are whole frames,
	 * e.g. 97.2 ms for a 102.6 ms interval at 144 FPS) without allowing any sustained rate gain.
	 *
	 * Stamps are not trusted for banking: an honest stamp is about ExpectedStampAge (RTT + proxy
	 * interpolation) old, so anything older is raised to that floor before the cadence check, and
	 * anything from the future is rejected.
	 */
	struct FFireCadenceGate
	{
		double TheoreticalArrivalTime = -1.0e9;

		/**
		 * @param OutCadenceTime The stamp after the banking floor: use it for anything that rewards
		 *                       time between shots (spray recovery). Equals StampTime for honest clients.
		 */
		bool TryAcceptShot(double StampTime, double ServerNow, double ExpectedStampAge, float FireInterval, double* OutCadenceTime = nullptr,
			double StampTolerance = 0.05, double MaxBurstTolerance = 0.05, double FutureTolerance = 1.0 / 128.0)
		{
			if (StampTime > ServerNow + FutureTolerance)
			{
				return false; // Honest clients can only stamp the past.
			}
			const double CadenceTime = FMath::Max(StampTime, ServerNow - ExpectedStampAge - StampTolerance);
			const double BurstTolerance = FMath::Min(MaxBurstTolerance, 0.5 * FireInterval);
			if (CadenceTime < TheoreticalArrivalTime - BurstTolerance)
			{
				return false; // Faster than the weapon can cycle.
			}
			TheoreticalArrivalTime = FMath::Max(CadenceTime, TheoreticalArrivalTime) + FireInterval;
			if (OutCadenceTime)
			{
				*OutCadenceTime = CadenceTime;
			}
			return true;
		}

		void Reset() { TheoreticalArrivalTime = -1.0e9; }
	};
}
