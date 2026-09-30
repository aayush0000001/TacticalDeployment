// Copyright TacticalDeployment. All Rights Reserved.
// Pillar 4: movement and tagging. Real code: Character/TacticalMovementTuning.h,
// Character/TakeDamageTagging.h; engine behaviour: Sim/CmcModel.h.

#include "SimTest.h"
#include "CmcModel.h"
#include "Character/TacticalMovementTuning.h"
#include "Character/TakeDamageTagging.h"
#include "Weapons/WeaponStats.h"

namespace
{
	constexpr float Run = TacticalMovementTuning::RunSpeed;

	struct FStopTiming
	{
		double TimeToAccurate = -1.0; // Speed <= 30% of run speed (weapon accuracy threshold).
		double TimeToStop = -1.0;     // Speed ~ 0 (or direction reversed).
		double Slide = 0.0;           // Distance covered after the input change (cm).
	};

	/** From full run speed along +Y, either counter-strafe (-Y input) or release, at a client frame rate. */
	FStopTiming MeasureStop(bool bCounterStrafe, double Fps)
	{
		FCmcModel Model;
		for (int32 i = 0; i < 256; ++i) { Model.SimulateFrame(float(TacticalNet::ServerFrameTime), FVector(0, 1, 0), Run); }
		const double StartY = Model.Location.Y;
		const float Accurate = UWeaponStats().AccurateSpeedFraction * Run;

		FStopTiming Out;
		double T = 0.0;
		const float Dt = float(1.0 / Fps);
		while (T < 1.0)
		{
			Model.SimulateFrame(Dt, bCounterStrafe ? FVector(0, -1, 0) : FVector::ZeroVector, Run);
			T += Dt;
			const double SpeedAlongRun = Model.Velocity.Y;
			if (Out.TimeToAccurate < 0.0 && std::abs(SpeedAlongRun) <= Accurate) { Out.TimeToAccurate = T; }
			if (SpeedAlongRun <= 1.0) { Out.TimeToStop = T; Out.Slide = Model.Location.Y - StartY; break; }
		}
		return Out;
	}
}

SIM_TEST(Movement, CounterStrafeStopsFarFasterThanReleasing)
{
	const FStopTiming Counter = MeasureStop(true, 128.0);
	const FStopTiming Release = MeasureStop(false, 128.0);
	SIM_EXPECT_GT(Counter.TimeToAccurate, 0.0);
	SIM_EXPECT_LE(Counter.TimeToAccurate, 0.05);
	SIM_EXPECT_LT(Counter.TimeToAccurate * 2.0, Release.TimeToAccurate);
	SIM_EXPECT_LT(Counter.TimeToStop, Release.TimeToStop);
	SimTest::Report("counter-strafe: accurate after %.1f ms, stopped after %.1f ms, slide %.1f cm",
		Counter.TimeToAccurate * 1000.0, Counter.TimeToStop * 1000.0, Counter.Slide);
	SimTest::Report("release key:    accurate after %.1f ms, stopped after %.1f ms, slide %.1f cm",
		Release.TimeToAccurate * 1000.0, Release.TimeToStop * 1000.0, Release.Slide);
}

SIM_TEST(Movement, AccelerationToRunSpeed)
{
	FCmcModel Model;
	double T = 0.0;
	double To95 = -1.0;
	while (T < 1.0 && To95 < 0.0)
	{
		Model.SimulateFrame(float(TacticalNet::ServerFrameTime), FVector(1, 0, 0), Run);
		T += TacticalNet::ServerFrameTime;
		if (Model.Velocity.Size() >= 0.95 * Run) { To95 = T; }
	}
	SIM_EXPECT_GT(To95, 0.0);
	SIM_EXPECT_LE(To95, 0.15);
	SIM_EXPECT_LE(Model.Velocity.Size(), Run + 1e-3); // Never exceeds max speed.
	SimTest::Report("0 -> 95%% run speed in %.1f ms", To95 * 1000.0);
}

SIM_TEST(Movement, FrameRateIndependenceOfTheStop)
{
	// Sub-steps are capped at 7.8125 ms; the server replays each client's exact steps, so this
	// only measures how different frame rates *feel*, not client/server agreement.
	double MinT = 1e9, MaxT = 0, MinSlide = 1e9, MaxSlide = 0;
	for (const double Fps : { 30.0, 60.0, 128.0, 144.0, 240.0, 360.0 })
	{
		const FStopTiming S = MeasureStop(true, Fps);
		MinT = std::min(MinT, S.TimeToAccurate); MaxT = std::max(MaxT, S.TimeToAccurate);
		MinSlide = std::min(MinSlide, S.Slide); MaxSlide = std::max(MaxSlide, S.Slide);
		SimTest::Report("%3.0f FPS: counter-strafe accurate after %.1f ms, slide %.2f cm", Fps, S.TimeToAccurate * 1000.0, S.Slide);
	}
	SIM_EXPECT_LE(MaxSlide - MinSlide, 5.0); // "A few centimetres" (CalcVelocity's friction term is linear in dt).
	SimTest::Report("spread across frame rates: accurate-time %.1f ms (frame quantization), slide %.2f cm", (MaxT - MinT) * 1000.0, MaxSlide - MinSlide);
}

SIM_TEST(Movement, MaxSpeedRules)
{
	using namespace TacticalMovementTuning;
	FMaxSpeedInputs In;
	SIM_EXPECT_NEAR(ComputeMaxSpeed(In), RunSpeed, 1e-6);

	FMaxSpeedInputs Walk = In; Walk.bWantsShiftWalk = true;
	SIM_EXPECT_NEAR(ComputeMaxSpeed(Walk), ShiftWalkSpeed, 1e-6);

	FMaxSpeedInputs Crouch = In; Crouch.BaseMaxSpeed = CrouchSpeed; Crouch.bCrouching = true; Crouch.bWantsShiftWalk = true;
	SIM_EXPECT_NEAR(ComputeMaxSpeed(Crouch), CrouchSpeed, 1e-6); // Walk key does not raise crouch speed.

	FMaxSpeedInputs Planting = In; Planting.bInteractLocked = true;
	SIM_EXPECT_NEAR(ComputeMaxSpeed(Planting), 0.0, 0.0);
	FMaxSpeedInputs PlantingInAir = Planting; PlantingInAir.bOnGround = false;
	SIM_EXPECT_NEAR(ComputeMaxSpeed(PlantingInAir), RunSpeed, 1e-6); // Lock applies on the ground only.

	FMaxSpeedInputs BuyPhase = In; BuyPhase.bPhaseLocked = true;
	SIM_EXPECT_NEAR(ComputeMaxSpeed(BuyPhase), 0.0, 0.0);

	FMaxSpeedInputs Tagged = In; Tagged.WeaponMultiplier = 0.9f; Tagged.TaggingScalar = 0.3f;
	SIM_EXPECT_NEAR(ComputeMaxSpeed(Tagged), RunSpeed * 0.9 * 0.3, 1e-3);
}

SIM_TEST(Movement, TaggingCurveAndStacking)
{
	FTaggingState Tag = FTaggingState::Stack(FTaggingState(), 10.f, 0.7f, 0.5f);
	SIM_EXPECT_NEAR(Tag.GetSlow(), 0.7, 1.0 / 255.0);
	SIM_EXPECT_NEAR(Tag.Evaluate(9.9f), 1.0, 0.0);                  // Before the hit.
	SIM_EXPECT_NEAR(Tag.Evaluate(10.f), 1.0 - Tag.GetSlow(), 1e-6); // Full slow on impact.
	SIM_EXPECT_NEAR(Tag.Evaluate(10.25f), 1.0 - Tag.GetSlow() * 0.5, 1e-4); // SmoothStep midpoint.
	SIM_EXPECT_NEAR(Tag.Evaluate(10.5f), 1.0, 0.0);                 // Recovered.
	float Previous = 0.f;
	for (float T = 10.f; T <= 10.5f; T += 0.01f)
	{
		SIM_EXPECT_GE(Tag.Evaluate(T), Previous); // Monotonic recovery.
		Previous = Tag.Evaluate(T);
	}

	// A weak hit during a strong tag never weakens it.
	const FTaggingState Weak = FTaggingState::Stack(Tag, 10.05f, 0.2f, 0.5f);
	SIM_EXPECT_GE(Weak.GetSlow() + 1.0 / 255.0, 1.f - Tag.Evaluate(10.05f)); // Within 8-bit quantization.
	SIM_EXPECT_NEAR(Weak.StartMoveTime, 10.05, 1e-6);

	// CMC timestamp reset (client clock wraps to ~0): tag expires instead of freezing the player.
	SIM_EXPECT_NEAR(Tag.Evaluate(0.2f), 1.0, 0.0);
	SimTest::Report("speed scalar after a 70%% tag: t=0 %.2f, 125 ms %.2f, 250 ms %.2f, 375 ms %.2f, 500 ms %.2f",
		Tag.Evaluate(10.f), Tag.Evaluate(10.125f), Tag.Evaluate(10.25f), Tag.Evaluate(10.375f), Tag.Evaluate(10.5f));
}

SIM_TEST(Movement, TaggingOnTheMoveClockReplaysExactly)
{
	// Client moves carry timestamps (move clock). The server tags at the last processed move's
	// timestamp; moves already in flight disagree once (the correction), and the replay after the
	// correction reproduces the server's max speed on every move. Evaluating on wall-clock time
	// instead keeps disagreeing through the whole ease-out.
	const float MoveDt = float(TacticalNet::ServerFrameTime);
	const float Rtt = 0.08f;
	const int32 InFlight = int32(std::ceil((Rtt * 0.5f) / MoveDt));
	const int32 TagMove = 200;

	std::vector<float> MoveTimes;
	for (int32 i = 0; i < 400; ++i) { MoveTimes.push_back(5.f + i * MoveDt); }

	// Server processes move TagMove, then applies the tag at that move's timestamp.
	const FTaggingState ServerTag = FTaggingState::Stack(FTaggingState(), MoveTimes[TagMove], 0.7f, 0.5f);

	int32 PredictionMismatches = 0, ReplayMismatches = 0, WallClockMismatches = 0;
	for (int32 i = TagMove + 1; i < 400; ++i)
	{
		const float ServerScalar = ServerTag.Evaluate(MoveTimes[i]);
		// Before the tag arrives the client predicts no slow for in-flight moves.
		const float Predicted = (i <= TagMove + InFlight) ? 1.f : ServerTag.Evaluate(MoveTimes[i]);
		PredictionMismatches += std::abs(Predicted - ServerScalar) > 1e-6f ? 1 : 0;
		// After the correction the client replays with the replicated tag at the same timestamps.
		ReplayMismatches += std::abs(ServerTag.Evaluate(MoveTimes[i]) - ServerScalar) > 1e-6f ? 1 : 0;
		// Alternative design: server evaluates at its own clock, which runs RTT/2 ahead of the move's.
		WallClockMismatches += std::abs(ServerTag.Evaluate(MoveTimes[i] + Rtt * 0.5f) - ServerScalar) > 1e-3f ? 1 : 0;
	}
	SIM_EXPECT_EQ(ReplayMismatches, 0);
	SIM_EXPECT_LE(PredictionMismatches, InFlight + 1);
	SimTest::Report("80 ms RTT: %d in-flight moves corrected once, %d mismatches after replay; wall-clock evaluation would mismatch %d moves",
		PredictionMismatches, ReplayMismatches, WallClockMismatches);
}
