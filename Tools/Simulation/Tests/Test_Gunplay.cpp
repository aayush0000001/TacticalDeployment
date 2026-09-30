// Copyright TacticalDeployment. All Rights Reserved.
// Pillar 3: deterministic gunplay. Real code: Weapons/WeaponStats.cpp, Weapons/ShotRules.h,
// Weapons/TacticalPhysicalMaterial.h.

#include "SimTest.h"
#include "Curves/CurveVector.h"
#include "Weapons/ShotRules.h"
#include "Weapons/TacticalPhysicalMaterial.h"
#include "Weapons/WeaponStats.h"
#include "VirtualCharacter.h"
#include "VirtualWorld.h"

#include <memory>

namespace
{
	/** A Vandal-like rifle: 40 dmg, x4 head, 9.75 rps, 25-bullet authored pattern. */
	struct FRifle
	{
		std::unique_ptr<UCurveVector> Pattern = std::make_unique<UCurveVector>();
		std::unique_ptr<UWeaponStats> Stats = std::make_unique<UWeaponStats>();

		FRifle()
		{
			for (int32 i = 0; i < 25; ++i)
			{
				// Vertical climb that plateaus, then a horizontal S-sway.
				const double Climb = std::min(i, 10) * 0.9 + std::max(0, i - 10) * 0.1;
				const double Sway = i < 10 ? 0.0 : std::sin((i - 10) * 0.6) * 1.5;
				Pattern->AddKey(float(i), FVector(Sway, Climb, 0));
			}
			Stats->RecoilPattern = Pattern.get();
			Stats->RangeBrackets = { { 5000.f, 1.f }, { 12000.f, 0.85f } };
		}
	};

	FWeaponSpreadInputs Inputs(float Speed, bool bAir = false, bool bCrouch = false, float FiringError = 0.f)
	{
		FWeaponSpreadInputs In;
		In.HorizontalSpeed = Speed;
		In.bIsAirborne = bAir;
		In.bIsCrouched = bCrouch;
		In.FiringError = FiringError;
		return In;
	}

	double AngleBetweenDeg(const FVector& A, const FVector& B)
	{
		return FMath::RadiansToDegrees(float(std::acos(FMath::Clamp(A.GetSafeNormal() | B.GetSafeNormal(), -1.0, 1.0))));
	}
}

SIM_TEST(Gunplay, FirstShotIsPinpointAndOnCrosshair)
{
	FRifle Rifle;
	FWeaponSprayState Spray;
	Spray.Recover(10.0, *Rifle.Stats);
	SIM_EXPECT_EQ(Spray.GetShotIndex(), 0);
	const FVector2D Recoil = Rifle.Stats->EvaluateRecoil(Spray.GetShotIndex());
	SIM_EXPECT_NEAR(Recoil.X, 0.0, 1e-9);
	SIM_EXPECT_NEAR(Recoil.Y, 0.0, 1e-9);
	SIM_EXPECT_NEAR(Rifle.Stats->ComputeSpread(Inputs(0.f)), Rifle.Stats->BaseSpread, 1e-6);
}

SIM_TEST(Gunplay, SprayFollowsAuthoredPatternByBulletIndex)
{
	FRifle Rifle;
	FWeaponSprayState Spray;
	const double Interval = Rifle.Stats->GetFireInterval();
	for (int32 Shot = 0; Shot < 30; ++Shot)
	{
		const double T = 5.0 + Shot * Interval;
		Spray.Recover(T, *Rifle.Stats);
		SIM_EXPECT_EQ(Spray.GetShotIndex(), Shot);
		const FVector2D Recoil = Rifle.Stats->EvaluateRecoil(Spray.GetShotIndex());
		const FVector Key = Rifle.Pattern->GetVectorValue(float(std::min(Shot, 24)));
		SIM_EXPECT_NEAR(Recoil.X, Key.X, 1e-6);
		SIM_EXPECT_NEAR(Recoil.Y, Key.Y, 1e-6); // Past the pattern: holds the last key.
		Spray.CommitShot(T, *Rifle.Stats);
	}
}

SIM_TEST(Gunplay, TappingStaysOnFirstBulletAndBurstsPartiallyReset)
{
	FRifle Rifle;
	FWeaponSprayState Tap;
	for (int32 Shot = 0; Shot < 10; ++Shot)
	{
		Tap.Recover(Shot * 0.5, *Rifle.Stats);
		SIM_EXPECT_EQ(Tap.GetShotIndex(), 0);
		SIM_EXPECT_NEAR(Tap.FiringError, 0.0, 1e-6);
		Tap.CommitShot(Shot * 0.5, *Rifle.Stats);
	}

	FWeaponSprayState Burst;
	const double Interval = Rifle.Stats->GetFireInterval();
	for (int32 Shot = 0; Shot < 10; ++Shot)
	{
		Burst.Recover(Shot * Interval, *Rifle.Stats);
		Burst.CommitShot(Shot * Interval, *Rifle.Stats);
	}
	const double Resume = 9 * Interval + 0.3;
	Burst.Recover(Resume, *Rifle.Stats);
	// Idle = 0.3 - interval; recovery starts after 0.1 s at 20 bullets/s.
	const double Expected = 10.0 - ((0.3 - Interval) - Rifle.Stats->RecoilRecoveryDelay) * Rifle.Stats->RecoilRecoveryRate;
	SIM_EXPECT_NEAR(Burst.SprayProgress, Expected, 1e-4);
	SimTest::Report("10-bullet burst then 300 ms pause: resumes at bullet %d (%.2f), firing error %.2f deg",
		Burst.GetShotIndex(), Burst.SprayProgress, Burst.FiringError);
}

SIM_TEST(Gunplay, ClientPredictionMatchesServerUnderNetworkJitter)
{
	// Both sides time the spray on the client's view-time stamps; the server receives them with
	// jitter. Timing the server on arrival times instead would desync the pattern.
	FRifle Rifle;
	FRandomStream Rng(7);
	FWeaponSprayState Client, ServerOnStamps, ServerOnArrival;
	int32 StampMismatches = 0, ArrivalMismatches = 0;
	const double Interval = Rifle.Stats->GetFireInterval();
	double Stamp = 20.0;
	for (int32 Burst = 0; Burst < 40; ++Burst)
	{
		const int32 Shots = 1 + int32(Rng.FRand() * 12);
		for (int32 Shot = 0; Shot < Shots; ++Shot)
		{
			const double Arrival = Stamp + 0.05 + Rng.FRand() * 0.04; // 40 ms of jitter.
			Client.Recover(Stamp, *Rifle.Stats);
			ServerOnStamps.Recover(Stamp, *Rifle.Stats);
			ServerOnArrival.Recover(Arrival, *Rifle.Stats);
			StampMismatches += Client.GetShotIndex() != ServerOnStamps.GetShotIndex() ? 1 : 0;
			ArrivalMismatches += Client.GetShotIndex() != ServerOnArrival.GetShotIndex() ? 1 : 0;
			Client.CommitShot(Stamp, *Rifle.Stats);
			ServerOnStamps.CommitShot(Stamp, *Rifle.Stats);
			ServerOnArrival.CommitShot(Arrival, *Rifle.Stats);
			Stamp += Interval;
		}
		Stamp += 0.1 + Rng.FRand() * 0.6; // Pause between bursts.
	}
	SIM_EXPECT_EQ(StampMismatches, 0);
	SimTest::Report("40 random bursts: recoil index mismatches, client vs server on stamps = %d, on arrival times = %d",
		StampMismatches, ArrivalMismatches);
}

SIM_TEST(Gunplay, SpreadRewardsStoppingAndPunishesMovement)
{
	FRifle Rifle;
	const UWeaponStats& S = *Rifle.Stats;
	const float Accurate = S.AccurateSpeedFraction * 675.f;
	SIM_EXPECT_NEAR(S.ComputeSpread(Inputs(Accurate - 1.f)), S.BaseSpread, 1e-6);  // Counter-strafed
	SIM_EXPECT_NEAR(S.ComputeSpread(Inputs(405.f)), S.BaseSpread + S.WalkSpread, 1e-4);
	SIM_EXPECT_NEAR(S.ComputeSpread(Inputs(675.f)), S.BaseSpread + S.RunSpread, 1e-4);
	SIM_EXPECT_NEAR(S.ComputeSpread(Inputs(0.f, true)), S.BaseSpread + S.AirborneSpread, 1e-4);
	SIM_EXPECT_NEAR(S.ComputeSpread(Inputs(0.f, false, true)), S.BaseSpread * S.CrouchSpreadMultiplier, 1e-6);
	SIM_EXPECT_NEAR(S.ComputeSpread(Inputs(0.f, false, false, 2.f)), S.BaseSpread + 2.f, 1e-6);

	float Previous = -1.f;
	for (float Speed = 0.f; Speed <= 675.f; Speed += 5.f)
	{
		const float Spread = S.ComputeSpread(Inputs(Speed));
		SIM_EXPECT_GE(Spread, Previous); // Monotonic in speed.
		Previous = Spread;
	}
	SIM_EXPECT_TRUE(UWeaponStats::ClassifyMovement(Inputs(100.f), S.AccurateSpeedFraction) == ETacticalMovementState::Stationary);
	SIM_EXPECT_TRUE(UWeaponStats::ClassifyMovement(Inputs(400.f), S.AccurateSpeedFraction) == ETacticalMovementState::Walking);
	SIM_EXPECT_TRUE(UWeaponStats::ClassifyMovement(Inputs(600.f), S.AccurateSpeedFraction) == ETacticalMovementState::Running);
	SimTest::Report("cone half-angle: still %.2f, walk %.2f, run %.2f, air %.2f, crouched %.2f deg",
		S.ComputeSpread(Inputs(0.f)), S.ComputeSpread(Inputs(405.f)), S.ComputeSpread(Inputs(675.f)),
		S.ComputeSpread(Inputs(0.f, true)), S.ComputeSpread(Inputs(0.f, false, true)));
}

SIM_TEST(Gunplay, RecoilAndSpreadProduceBulletsInsideTheCone)
{
	FRandomStream Rng(42);
	const FRotator Aim(5.0, 30.0, 0.0);
	const FVector2D Recoil(1.5, 4.0); // 1.5 deg right, 4 deg up.
	const FVector Punched = FRotator(Aim.Pitch + Recoil.Y, Aim.Yaw + Recoil.X, 0.0).Vector();
	double MaxAngle = 0.0;
	FVector Sum = FVector::ZeroVector;
	for (int32 i = 0; i < 20000; ++i)
	{
		const FVector Dir = UWeaponStats::ApplyRecoilAndSpread(Aim, Recoil, 2.5f, Rng);
		MaxAngle = std::max(MaxAngle, AngleBetweenDeg(Dir, Punched));
		Sum += Dir;
	}
	SIM_EXPECT_LE(MaxAngle, 2.5 + 1e-3);
	SIM_EXPECT_LE(AngleBetweenDeg(Sum, Punched), 0.05); // Unbiased around the punched aim.
	const FVector NoSpread = UWeaponStats::ApplyRecoilAndSpread(Aim, Recoil, 0.f, Rng);
	SIM_EXPECT_LE(AngleBetweenDeg(NoSpread, Punched), 1e-4);
	SIM_EXPECT_GT(NoSpread.Z, Aim.Vector().Z); // +pitch recoil climbs.
	SimTest::Report("20k bullets, 2.5 deg cone: max deviation %.3f deg, mean bias %.4f deg", MaxAngle, AngleBetweenDeg(Sum, Punched));
}

SIM_TEST(Gunplay, DamageByZoneAndRange)
{
	FRifle Rifle;
	const UWeaponStats& S = *Rifle.Stats;
	SIM_EXPECT_NEAR(S.BaseDamage * S.GetZoneMultiplier(EHitZone::Head) * S.GetRangeMultiplier(2000.f), 160.0, 1e-4);
	SIM_EXPECT_NEAR(S.BaseDamage * S.GetZoneMultiplier(EHitZone::Body) * S.GetRangeMultiplier(2000.f), 40.0, 1e-4);
	SIM_EXPECT_NEAR(S.BaseDamage * S.GetZoneMultiplier(EHitZone::Legs) * S.GetRangeMultiplier(2000.f), 34.0, 1e-4);
	SIM_EXPECT_NEAR(S.BaseDamage * S.GetZoneMultiplier(EHitZone::Body) * S.GetRangeMultiplier(8000.f), 34.0, 1e-4);
	SIM_EXPECT_NEAR(S.GetZoneMultiplier(EHitZone::None), 0.0, 0.0);
}

// ---------------------------------------------------------------------------------------
// Wallbangs through the virtual world with the real bullet solver.
// ---------------------------------------------------------------------------------------

namespace
{
	struct FShotOutcome
	{
		FBulletResult Result;
		EHitZone Zone = EHitZone::None;
	};

	FShotOutcome FireThrough(const FVirtualWorld& World, EWallPenetrationTier Tier, const FVector& TargetLocation, float MaxRange = 12000.f)
	{
		FVirtualCharacter Target;
		Target.Location = TargetLocation;
		const FCharacterPoseRecord Pose = Target.BuildPose();
		const FVector Start(0, 0, 34);
		const FVector Dir(1, 0, 0);
		FVirtualWorld::FBulletAdapter Adapter(World);
		FShotOutcome Out;
		Out.Result = ShotRules::SolveBulletPath(Start, Dir, MaxRange, UWeaponStats::GetPenetrationParams(Tier),
			[&](const FVector& From, const FVector& To, FBulletSurfaceHit& Hit) { return Adapter.TraceWorld(From, To, Hit); },
			[&](const FBulletSurfaceHit& Entry, const FVector& D, float MaxThickness, FVector& Exit) { return Adapter.ProbeExit(Entry, D, MaxThickness, Exit); },
			[&](const FVector& From, const FVector& To, FBulletBodyHit& Hit)
			{
				const FVector Delta = To - From;
				const float Length = float(Delta.Size());
				float T = 0.f;
				const int32 Index = TacticalHitboxMath::TracePose(Pose, From, FVector3f(Delta / Length), Length, T);
				if (Index == INDEX_NONE) { return false; }
				Out.Zone = Pose.Hitboxes[Index].Zone;
				Hit.Distance = T;
				Hit.Location = From + Delta / Length * T;
				return true;
			});
		return Out;
	}

	FVirtualWorld WallAt(double X, double Thickness, float Density)
	{
		FVirtualWorld World;
		World.AddBox(FVector(X, -500, -200), FVector(X + Thickness, 500, 400), Density, "Wall");
		return World;
	}
}

SIM_TEST(Gunplay, WallbangsSpendPenetrationBudget)
{
	const FVector Target(1000, 0, 0);
	const float Drywall = 0.4f, Concrete = 2.0f, Glass = 0.1f;

	// 10 cm drywall: Medium (25) passes, cost 4 -> 84% damage.
	FShotOutcome Out = FireThrough(WallAt(500, 10, Drywall), EWallPenetrationTier::Medium, Target);
	SIM_EXPECT_TRUE(Out.Result.bHitBody && Out.Zone == EHitZone::Body);
	SIM_EXPECT_NEAR(Out.Result.DamageScale, 1.0 - 4.0 / 25.0, 1e-4);
	SIM_EXPECT_NEAR(Out.Result.TravelledDistance, Out.Result.ImpactPoint.X, 1.0); // Straight shot: distance = X.
	SimTest::Report("10 cm drywall, Medium tier: body hit at %.0f cm, damage x%.2f", Out.Result.TravelledDistance, Out.Result.DamageScale);

	// 15 cm concrete (cost 30): Medium stops, High passes with 40%.
	Out = FireThrough(WallAt(500, 15, Concrete), EWallPenetrationTier::Medium, Target);
	SIM_EXPECT_FALSE(Out.Result.bHitBody);
	SIM_EXPECT_NEAR(Out.Result.ImpactPoint.X, 500.0, 1e-3);
	Out = FireThrough(WallAt(500, 15, Concrete), EWallPenetrationTier::High, Target);
	SIM_EXPECT_TRUE(Out.Result.bHitBody);
	SIM_EXPECT_NEAR(Out.Result.DamageScale, 1.0 - 30.0 / 50.0, 1e-4);
	SimTest::Report("15 cm concrete: Medium stops at the wall, High passes with damage x%.2f", Out.Result.DamageScale);

	// Two thin walls: Low tier allows one surface only.
	FVirtualWorld TwoWalls = WallAt(400, 5, Drywall);
	TwoWalls.AddBox(FVector(600, -500, -200), FVector(605, 500, 400), Drywall, "Wall2");
	Out = FireThrough(TwoWalls, EWallPenetrationTier::Low, Target);
	SIM_EXPECT_FALSE(Out.Result.bHitBody);
	SIM_EXPECT_EQ(Out.Result.SurfacesPenetrated, 1);
	Out = FireThrough(TwoWalls, EWallPenetrationTier::Medium, Target);
	SIM_EXPECT_TRUE(Out.Result.bHitBody);
	SIM_EXPECT_EQ(Out.Result.SurfacesPenetrated, 2);
	SIM_EXPECT_NEAR(Out.Result.DamageScale, (1.0 - 2.0 / 25.0) * (1.0 - 2.0 / 25.0), 1e-4);

	// Impenetrable boundary.
	Out = FireThrough(WallAt(500, 1, TNumericLimits<float>::Max()), EWallPenetrationTier::High, Target);
	SIM_EXPECT_FALSE(Out.Result.bHitBody);

	// Body in front of the wall is hit first, at full damage.
	Out = FireThrough(WallAt(1500, 10, Drywall), EWallPenetrationTier::Medium, Target);
	SIM_EXPECT_TRUE(Out.Result.bHitBody);
	SIM_EXPECT_NEAR(Out.Result.DamageScale, 1.0, 0.0);
	SIM_EXPECT_EQ(Out.Result.SurfacesPenetrated, 0);

	// Out of range.
	Out = FireThrough(FVirtualWorld(), EWallPenetrationTier::Medium, FVector(13000, 0, 0));
	SIM_EXPECT_FALSE(Out.Result.bHitBody);
	SIM_EXPECT_NEAR(Out.Result.TravelledDistance, 12000.0, 1e-3);
	(void)Glass;
}

SIM_TEST(Gunplay, WallsThickerThanTheTierCanProbeStopBullets)
{
	// Regression for the exit-probe defect: 50 cm of glass costs only 5, but the Medium tier can
	// only probe 40 cm. The backward probe starts *inside* the solid; UE simple collision reports
	// that as a start-penetrating hit at the probe point, which the old code accepted as the exit.
	const FVirtualWorld Thick = WallAt(500, 50, 0.1f);
	FShotOutcome Out = FireThrough(Thick, EWallPenetrationTier::Medium, FVector(1000, 0, 0));
	SIM_EXPECT_FALSE(Out.Result.bHitBody);
	SIM_EXPECT_EQ(Out.Result.SurfacesPenetrated, 0);

	// What the old acceptance rule would have done (hit accepted unless it equals the entry point).
	FVirtualWorld::FBulletAdapter Legacy(Thick);
	FBulletSurfaceHit Entry;
	SIM_EXPECT_TRUE(Legacy.TraceWorld(FVector(0, 0, 34), FVector(2000, 0, 34), Entry));
	const FVirtualTraceHit Probe = FVirtualWorld::TraceBox(Thick.Boxes[0], Entry.ImpactPoint + FVector(40, 0, 0), Entry.ImpactPoint);
	const bool bLegacyAccepted = Probe.bHit && FVector::DistSquared(Probe.ImpactPoint, Entry.ImpactPoint) >= 0.01;
	SIM_EXPECT_TRUE(bLegacyAccepted);
	SIM_EXPECT_TRUE(Probe.bStartPenetrating);

	// The High tier probes 80 cm, finds the real exit and pays for all 50 cm.
	Out = FireThrough(Thick, EWallPenetrationTier::High, FVector(1000, 0, 0));
	SIM_EXPECT_TRUE(Out.Result.bHitBody);
	SIM_EXPECT_NEAR(Out.Result.DamageScale, 1.0 - 5.0 / 50.0, 1e-4);
	SimTest::Report("50 cm glass: Medium (40 cm probe) now stops; old rule would have let it through as 40 cm. High passes, damage x%.2f",
		Out.Result.DamageScale);
}

SIM_TEST(Gunplay, PhysicalMaterialDensity)
{
	UTacticalPhysicalMaterial Wood;
	Wood.PenetrationDensity = 0.6f;
	UTacticalPhysicalMaterial Boundary;
	Boundary.bImpenetrable = true;
	UPhysicalMaterial Default;
	SIM_EXPECT_NEAR(UTacticalPhysicalMaterial::GetDensity(&Wood), 0.6, 1e-6);
	SIM_EXPECT_TRUE(UTacticalPhysicalMaterial::GetDensity(&Boundary) >= TNumericLimits<float>::Max());
	SIM_EXPECT_NEAR(UTacticalPhysicalMaterial::GetDensity(&Default), 1.5, 1e-6);
	SIM_EXPECT_NEAR(UTacticalPhysicalMaterial::GetDensity(nullptr), 1.5, 1e-6);
}

// ---------------------------------------------------------------------------------------
// Fire-rate anti-cheat with a model of UE's looping timer (fires on frame boundaries; several
// calls in one frame if more than one interval elapsed) at real, jittery frame rates.
// ---------------------------------------------------------------------------------------

namespace
{
	std::vector<double> ClientAutoFireTimes(double Fps, double FrameJitter, float Interval, int32 Shots, FRandomStream& Rng)
	{
		std::vector<double> Times;
		double Now = 0.0;
		double Expire = 0.0; // Trigger pulled on this frame: first shot fires immediately.
		while (int32(Times.size()) < Shots)
		{
			while (Now >= Expire && int32(Times.size()) < Shots)
			{
				Times.push_back(Now);
				Expire += Interval;
			}
			Now += (1.0 / Fps) * (1.0 + (Rng.FRand() * 2.0 - 1.0) * FrameJitter);
		}
		return Times;
	}

	struct FGateRun { int32 Sent = 0; int32 Accepted = 0; double Duration = 0.0; };

	/** Client fire times (client clock) -> stamps -> server arrivals -> gate. */
	FGateRun RunGate(const std::vector<double>& FireTimes, float Interval, double Down, double Up, double Jitter, FRandomStream& Rng,
		const std::function<double(int32, double)>& StampOverride = nullptr)
	{
		const double Interp = TacticalNet::ProxyInterpolationDelay;
		ShotRules::FFireCadenceGate Gate;
		FGateRun Run;
		for (int32 i = 0; i < int32(FireTimes.size()); ++i)
		{
			const double FireServerTime = 100.0 + FireTimes[i];
			double Stamp = FireServerTime - (Down + Jitter * 0.5) - Interp; // Honest view time (clock sync error ~0).
			if (StampOverride) { Stamp = StampOverride(i, Stamp); }
			const double Arrival = FireServerTime + Up + Rng.FRand() * Jitter;
			Run.Sent++;
			Run.Accepted += Gate.TryAcceptShot(Stamp, Arrival, Down + Up + Jitter + Interp, Interval) ? 1 : 0;
		}
		Run.Duration = FireTimes.back() - FireTimes.front();
		return Run;
	}
}

SIM_TEST(Gunplay, FireRateGateNeverRejectsHonestClients)
{
	FRifle Rifle;
	const float Interval = Rifle.Stats->GetFireInterval();
	FRandomStream Rng(3);
	for (const double Fps : { 30.0, 60.0, 144.0, 240.0, 360.0 })
	{
		const std::vector<double> Times = ClientAutoFireTimes(Fps, 0.25, Interval, 2000, Rng);
		const FGateRun Run = RunGate(Times, Interval, 0.035, 0.035, 0.01, Rng);
		SIM_EXPECT_EQ(Run.Accepted, Run.Sent);

		// The previous rule (gap >= 0.95 interval) on the same shots.
		int32 OldRejections = 0;
		for (size_t i = 1; i < Times.size(); ++i)
		{
			OldRejections += (Times[i] - Times[i - 1]) < Interval * 0.95 ? 1 : 0;
		}
		SimTest::Report("%3.0f FPS (+/-25%% frame jitter), 2000 auto-fire shots: rejected %d | previous gap rule would reject %d",
			Fps, Run.Sent - Run.Accepted, OldRejections);
	}
}

SIM_TEST(Gunplay, FireRateGateBoundsCheaters)
{
	FRifle Rifle;
	const float Interval = Rifle.Stats->GetFireInterval();
	FRandomStream Rng(11);
	const double MaxLegit = 5.0 / Interval + 1.0; // Shots a legit client fits in 5 s.

	// Cheat A: fire twice as fast with honest stamps.
	std::vector<double> Fast;
	for (double T = 0.0; T <= 5.0; T += Interval * 0.5) { Fast.push_back(T); }
	FGateRun A = RunGate(Fast, Interval, 0.03, 0.03, 0.005, Rng);
	SIM_EXPECT_LE(A.Accepted, MaxLegit + 1.0);

	// Cheat B: fire twice as fast but forge stamps a full interval apart (stamps outrun real time).
	FGateRun B = RunGate(Fast, Interval, 0.03, 0.03, 0.005, Rng, [&](int32 i, double Honest) { return (Honest - i * Interval * 0.5) + i * Interval; });
	SIM_EXPECT_LE(B.Accepted, MaxLegit + 1.0);

	// Cheat C: bank 1 s of shots, then dump 10 in one frame with back-dated, perfectly spaced stamps.
	std::vector<double> Dump(10, 0.0);
	FGateRun C = RunGate(Dump, Interval, 0.03, 0.03, 0.005, Rng, [&](int32 i, double Honest) { return Honest - 1.0 + i * Interval; });
	SIM_EXPECT_LE(C.Accepted, 2);

	SimTest::Report("2x fire rate, honest stamps: %d of %d accepted (legit max %.0f)", A.Accepted, A.Sent, MaxLegit);
	SimTest::Report("2x fire rate, forged spaced stamps: %d of %d accepted", B.Accepted, B.Sent);
	SimTest::Report("10-shot dump with back-dated stamps: %d accepted", C.Accepted);
}

SIM_TEST(Gunplay, ForgedStampGapsCannotBuyRecoilRecovery)
{
	// A no-recoil cheat fires full-auto but stamps each shot as if 1 s had passed. The server times
	// the spray on the gate's cadence time, so the pattern still climbs like an honest spray.
	FRifle Rifle;
	const float Interval = Rifle.Stats->GetFireInterval();
	ShotRules::FFireCadenceGate Gate;
	FWeaponSprayState Server, Honest;
	const double Age = 0.1;
	int32 Accepted = 0;
	for (int32 Shot = 0; Shot < 15; ++Shot)
	{
		const double ServerNow = 50.0 + Shot * Interval;
		const double ForgedStamp = ServerNow - 20.0 + Shot * 1.0; // Pretend 1 s between shots.
		double CadenceTime = 0.0;
		if (Gate.TryAcceptShot(ForgedStamp, ServerNow, Age, Interval, &CadenceTime))
		{
			++Accepted;
			Server.Recover(CadenceTime, *Rifle.Stats);
			Server.CommitShot(CadenceTime, *Rifle.Stats);
		}
		Honest.Recover(ServerNow - Age, *Rifle.Stats);
		Honest.CommitShot(ServerNow - Age, *Rifle.Stats);
	}
	SIM_EXPECT_GE(Accepted, 14);
	SIM_EXPECT_GE(Server.SprayProgress, Honest.SprayProgress - 1.0f);
	SimTest::Report("15-shot spray with forged 1 s gaps: server spray at bullet %.1f (honest %.1f), %d shots accepted",
		Server.SprayProgress, Honest.SprayProgress, Accepted);
}
