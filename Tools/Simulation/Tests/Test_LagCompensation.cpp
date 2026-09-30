// Copyright TacticalDeployment. All Rights Reserved.
// Pillar 2: lag compensation. Real code: Combat/HitboxRewind.h, Core/ClockSync.h.

#include "SimTest.h"
#include "CmcModel.h"
#include "Combat/HitboxRewind.h"
#include "Core/ClockSync.h"
#include "VirtualCharacter.h"

#include <memory>

using namespace TacticalHitboxMath;

SIM_TEST(LagCompensation, RayCapsuleIntersection)
{
	const FVector3f Pa(0, 0, -10), Pb(0, 0, 10);
	const float R = 5.f;
	SIM_EXPECT_NEAR(IntersectRayCapsule({ 20, 0, 0 }, { -1, 0, 0 }, Pa, Pb, R), 15.0, 1e-4);   // Body
	SIM_EXPECT_NEAR(IntersectRayCapsule({ 0, 0, 40 }, { 0, 0, -1 }, Pa, Pb, R), 25.0, 1e-4);   // Top cap
	SIM_EXPECT_NEAR(IntersectRayCapsule({ 0, 0, -40 }, { 0, 0, 1 }, Pa, Pb, R), 25.0, 1e-4);   // Bottom cap
	SIM_EXPECT_LT(IntersectRayCapsule({ 20, 6, 0 }, { -1, 0, 0 }, Pa, Pb, R), 0.0);            // Miss beside
	SIM_EXPECT_LT(IntersectRayCapsule({ 20, 0, 0 }, { 1, 0, 0 }, Pa, Pb, R), 0.0);             // Pointing away
	SIM_EXPECT_NEAR(IntersectRayCapsule({ 1, 0, 0 }, { 1, 0, 0 }, Pa, Pb, R), 0.0, 1e-6);      // Starts inside
	SIM_EXPECT_NEAR(IntersectRayCapsule({ 20, 0, 12 }, { -1, 0, 0 }, Pa, Pb, R), 20.0 - std::sqrt(21.0), 1e-3); // Cap graze
	SIM_EXPECT_NEAR(IntersectRayCapsule({ 10, 0, 0 }, { -1, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, 3), 7.0, 1e-4);   // Sphere
	SIM_EXPECT_NEAR(IntersectRayCapsule({ 2, 0, 50 }, { 0, 0, -1 }, Pa, Pb, R), 40.0 - std::sqrt(21.0), 1e-3);  // Along axis
}

SIM_TEST(LagCompensation, TracePosePicksNearestHitboxAndZone)
{
	FVirtualCharacter Target;
	Target.Location = FVector(1000, 0, 0);
	const FCharacterPoseRecord Pose = Target.BuildPose();
	float T = 0.f;

	int32 Hit = TracePose(Pose, FVector(0, 0, 70), FVector3f(1, 0, 0), 5000.f, T);
	SIM_EXPECT_TRUE(Hit != INDEX_NONE && Pose.Hitboxes[Hit].Zone == EHitZone::Head);
	SIM_EXPECT_NEAR(T, 1000.0 - 11.0, 1e-2);

	Hit = TracePose(Pose, FVector(0, 0, 30), FVector3f(1, 0, 0), 5000.f, T);
	SIM_EXPECT_TRUE(Hit != INDEX_NONE && Pose.Hitboxes[Hit].Zone == EHitZone::Body);

	Hit = TracePose(Pose, FVector(0, 10, -60), FVector3f(1, 0, 0), 5000.f, T);
	SIM_EXPECT_TRUE(Hit != INDEX_NONE && Pose.Hitboxes[Hit].Zone == EHitZone::Legs);

	Hit = TracePose(Pose, FVector(0, 0, 100), FVector3f(1, 0, 0), 5000.f, T);
	SIM_EXPECT_EQ(Hit, INDEX_NONE); // Over the head.

	Hit = TracePose(Pose, FVector(0, 0, 70), FVector3f(1, 0, 0), 900.f, T);
	SIM_EXPECT_EQ(Hit, INDEX_NONE); // Segment ends before the target.
}

SIM_TEST(LagCompensation, RingBufferWrapsAndBracketsExactly)
{
	auto History = std::make_unique<FFrameHistory>();
	History->Allocate();
	const int32 Total = LagCompensation::HistoryCapacity * 2 + 7; // Wrap twice.
	for (int32 i = 0; i < Total; ++i)
	{
		FFrameRecord& Frame = History->AddFrame(i * double(TacticalNet::ServerFrameTime));
		Frame.Characters[0].bValid = true;
		Frame.Characters[0].NumHitboxes = 1;
		Frame.Characters[0].Hitboxes[0].Center = FVector3f(float(i), 0, 0);
		Frame.Characters[0].Hitboxes[0].Rotation = FQuat4f::Identity;
		Frame.Characters[0].BoundsRadius = 1.f;
	}
	SIM_EXPECT_EQ(History->Num(), LagCompensation::HistoryCapacity);
	const double Newest = (Total - 1) * double(TacticalNet::ServerFrameTime);
	SIM_EXPECT_NEAR(History->GetNewestTime(), Newest, 1e-9);
	SIM_EXPECT_NEAR(History->GetNewestTime() - History->GetOldestTime(), (LagCompensation::HistoryCapacity - 1) * double(TacticalNet::ServerFrameTime), 1e-9);
	SIM_EXPECT_GE(History->GetNewestTime() - History->GetOldestTime(), double(LagCompensation::HistorySeconds)); // >= 1000 ms kept.

	for (int32 i = 1; i < History->Num(); ++i)
	{
		SIM_EXPECT_LT(History->GetFrame(i - 1).ServerTime, History->GetFrame(i).ServerTime);
	}

	// A time 40% of the way between two frames interpolates the recorded state by 40%.
	const FFrameRecord* Older = nullptr;
	const FFrameRecord* Newer = nullptr;
	float Alpha = 0.f;
	const double Query = Newest - 10.6 * double(TacticalNet::ServerFrameTime);
	SIM_EXPECT_TRUE(History->FindBracket(Query, Older, Newer, Alpha));
	FCharacterPoseRecord Out;
	LerpPose(Older->Characters[0], Newer->Characters[0], Alpha, Out);
	SIM_EXPECT_NEAR(Out.Hitboxes[0].Center.X, (Total - 1) - 10.6, 1e-3);

	// Outside the buffer: clamps to the ends.
	SIM_EXPECT_TRUE(History->FindBracket(Newest + 1.0, Older, Newer, Alpha));
	SIM_EXPECT_NEAR(Newer->ServerTime, Newest, 1e-9);
	SIM_EXPECT_TRUE(History->FindBracket(-5.0, Older, Newer, Alpha));
	SIM_EXPECT_NEAR(Older->ServerTime, History->GetOldestTime(), 1e-9);
}

SIM_TEST(LagCompensation, ResolveRewindTimeAcceptsHonestAndRejectsForgedStamps)
{
	const double Now = 100.0, Rtt = 0.08, Interp = TacticalNet::ProxyInterpolationDelay, Tol = 0.05 + TacticalNet::ServerFrameTime;
	bool bAccepted = false;

	const double Honest = Now - Rtt - Interp + 0.012; // Within jitter.
	SIM_EXPECT_NEAR(TacticalRewind::ResolveRewindTime(Now, Rtt, Honest, Interp, Tol, 0.35, &bAccepted), Honest, 1e-12);
	SIM_EXPECT_TRUE(bAccepted);

	const double Forged = Now - 0.3; // "I saw them 300 ms ago" with an 80 ms ping: backtrack exploit.
	SIM_EXPECT_NEAR(TacticalRewind::ResolveRewindTime(Now, Rtt, Forged, Interp, Tol, 0.35, &bAccepted), Now - Rtt - Interp, 1e-12);
	SIM_EXPECT_FALSE(bAccepted);

	// 500 ms ping: estimate is clamped to the 350 ms fairness cap.
	SIM_EXPECT_NEAR(TacticalRewind::ResolveRewindTime(Now, 0.5, Now - 0.5 - Interp, Interp, Tol, 0.35), Now - 0.35, 1e-12);
}

SIM_TEST(LagCompensation, ClockSyncConvergesUnderJitterAndAsymmetry)
{
	struct FCase { double Down, Up, Jitter; };
	for (const FCase C : { FCase{ 0.020, 0.020, 0.002 }, FCase{ 0.040, 0.040, 0.008 }, FCase{ 0.030, 0.050, 0.005 }, FCase{ 0.075, 0.075, 0.015 } })
	{
		FRandomStream Rng(1234);
		FTacticalClockSync Sync;
		const double TrueOffset = 1234.5678; // Server clock = client clock + offset.
		double ClientNow = 50.0;
		for (int32 Probe = 0; Probe < 20; ++Probe, ClientNow += 0.5)
		{
			const double Up = C.Up + Rng.FRand() * C.Jitter;
			const double Down = C.Down + Rng.FRand() * C.Jitter;
			const double ServerStamp = ClientNow + TrueOffset + Up;
			Sync.AddSample(ClientNow, ServerStamp, ClientNow + Up + Down);
		}
		const double OffsetError = Sync.GetOffset() - TrueOffset;
		// NTP's floor: path asymmetry shows up as an offset error of (Up - Down) / 2. It cancels in
		// EstimateViewTime (offset - RTT/2 = true offset - Down), see the duel test.
		SIM_EXPECT_LE(std::abs(OffsetError - (C.Up - C.Down) * 0.5), C.Jitter + 0.001);
		const double ViewTimeError = (Sync.EstimateViewTime(100.0, 0.0) - (100.0 + TrueOffset)) + (C.Down + C.Jitter * 0.5);
		SIM_EXPECT_LE(std::abs(ViewTimeError), C.Jitter + 0.001);
		SimTest::Report("down %2.0f / up %2.0f ms, jitter %2.0f ms: offset error %+5.1f ms, view-time error %+5.1f ms, smoothed RTT %5.1f ms",
			C.Down * 1000, C.Up * 1000, C.Jitter * 1000, OffsetError * 1000.0, ViewTimeError * 1000.0, Sync.GetSmoothedRoundTrip() * 1000.0);
	}
}

// ---------------------------------------------------------------------------------------
// End-to-end: a target strafes (A-D spam through the real movement model); the server records
// 128 Hz frames; a client with latency renders the target and fires at the displayed head
// centre; the server rewinds and traces. Compares the implemented rewind with two naive ones.
// ---------------------------------------------------------------------------------------

namespace
{
	struct FDuelResult
	{
		int32 Shots = 0;
		int32 HeadHits = 0;
		int32 AnyHits = 0;
		int32 AcceptedStamps = 0;
	};

	enum class ERewindMode { Implemented, HalfRttOnly, None };

	FDuelResult SimulateDuel(double Down, double Up, double Jitter, ERewindMode Mode, double DisplayTimingError = 0.0)
	{
		constexpr double Frame = TacticalNet::ServerFrameTime;
		const double Interp = TacticalNet::ProxyInterpolationDelay;
		FRandomStream Rng(99);

		// Server simulation: target strafing between two points using the real movement model.
		auto History = std::make_unique<FFrameHistory>();
		History->Allocate();
		std::vector<FVector> TargetPath; // Target location per server frame.
		FCmcModel Move;
		Move.Location = FVector(2000, 0, 0);
		const int32 NumFrames = 128 * 12;
		for (int32 F = 0; F < NumFrames; ++F)
		{
			const bool bRight = (F / 45) % 2 == 0; // Reverse every ~350 ms.
			Move.SimulateFrame(float(Frame), FVector(0, bRight ? 1.0 : -1.0, 0), TacticalMovementTuning::RunSpeed);
			TargetPath.push_back(Move.Location);
		}
		auto TargetAt = [&](double ServerTime)
		{
			const double Index = FMath::Clamp(ServerTime / Frame, 0.0, double(NumFrames - 1));
			const int32 I0 = int32(std::floor(Index));
			const int32 I1 = std::min(I0 + 1, NumFrames - 1);
			return TargetPath[I0] + (TargetPath[I1] - TargetPath[I0]) * (Index - I0);
		};

		// Client clock sync with the same network.
		FTacticalClockSync Sync;
		const double ClockOffset = 777.0;
		for (int32 Probe = 0; Probe < 16; ++Probe)
		{
			const double Send = 1.0 + Probe * 0.5 - ClockOffset;
			const double U = Up + Rng.FRand() * Jitter, D = Down + Rng.FRand() * Jitter;
			Sync.AddSample(Send, Send + ClockOffset + U, Send + U + D);
		}

		// Shots arrive over time; record frames up to each shot's arrival, then rewind + trace.
		FDuelResult Result;
		int32 RecordedFrames = 0;
		const FVector ShooterEye(0, 0, 64);
		for (double FireServerTime = 2.0; FireServerTime < 11.0; FireServerTime += 0.0371)
		{
			const double D = Down + Rng.FRand() * Jitter;
			const double U = Up + Rng.FRand() * Jitter;
			// Client displays the target as of (newest snapshot) - interpolation. Optional error
			// models a mismatch between assumed and real proxy smoothing.
			const double DisplayedWorldTime = FireServerTime - D - Interp + DisplayTimingError;
			FVirtualCharacter Displayed;
			Displayed.Location = TargetAt(DisplayedWorldTime);
			const FVector Aim = (Displayed.HeadCenter() - ShooterEye).GetSafeNormal();
			const double ViewTime = Sync.EstimateViewTime(FireServerTime - ClockOffset, Interp);

			// Server receives the RPC after the upstream leg.
			const double Arrival = FireServerTime + U;
			const int32 ArrivalFrame = int32(std::floor(Arrival / Frame));
			for (; RecordedFrames <= ArrivalFrame && RecordedFrames < NumFrames; ++RecordedFrames)
			{
				FVirtualCharacter Server;
				Server.Location = TargetPath[RecordedFrames];
				FFrameRecord& Record = History->AddFrame(RecordedFrames * Frame);
				FMemory::Memzero(&Record.Characters, sizeof(Record.Characters));
				Record.Characters[0] = Server.BuildPose();
			}
			const double ServerNow = (RecordedFrames - 1) * Frame;
			const double MeasuredRtt = Down + Up + Jitter; // PlayerState ping: smoothed average RTT.

			double RewindTime = ServerNow;
			bool bAccepted = false;
			switch (Mode)
			{
			case ERewindMode::Implemented:
				RewindTime = TacticalRewind::ResolveRewindTime(ServerNow, MeasuredRtt, ViewTime, Interp, 0.05 + Frame, 0.35, &bAccepted);
				break;
			case ERewindMode::HalfRttOnly:
				RewindTime = ServerNow - MeasuredRtt * 0.5;
				break;
			case ERewindMode::None:
				break;
			}

			const FFrameRecord* Older = nullptr;
			const FFrameRecord* Newer = nullptr;
			float Alpha = 0.f;
			History->FindBracket(RewindTime, Older, Newer, Alpha);
			FCharacterPoseRecord Rewound;
			LerpPose(Older->Characters[0], Newer->Characters[0], Alpha, Rewound);
			float T = 0.f;
			const int32 Hit = TracePose(Rewound, ShooterEye, FVector3f(Aim), 10000.f, T);

			++Result.Shots;
			Result.AcceptedStamps += bAccepted ? 1 : 0;
			Result.AnyHits += Hit != INDEX_NONE ? 1 : 0;
			Result.HeadHits += (Hit != INDEX_NONE && Rewound.Hitboxes[Hit].Zone == EHitZone::Head) ? 1 : 0;
		}
		return Result;
	}
}

SIM_TEST(LagCompensation, HeadshotsOnStrafingTargetRegisterAcrossPings)
{
	struct FNet { double Down, Up, Jitter; };
	for (const FNet N : { FNet{ 0.010, 0.010, 0.002 }, FNet{ 0.035, 0.035, 0.006 }, FNet{ 0.060, 0.060, 0.010 }, FNet{ 0.100, 0.100, 0.012 }, FNet{ 0.030, 0.060, 0.008 } })
	{
		const FDuelResult Impl = SimulateDuel(N.Down, N.Up, N.Jitter, ERewindMode::Implemented);
		const FDuelResult Half = SimulateDuel(N.Down, N.Up, N.Jitter, ERewindMode::HalfRttOnly);
		const FDuelResult None = SimulateDuel(N.Down, N.Up, N.Jitter, ERewindMode::None);
		const double ImplRate = double(Impl.HeadHits) / Impl.Shots;
		SIM_EXPECT_GE(ImplRate, 0.99);
		SimTest::Report("RTT %3.0f ms (%2.0f/%2.0f, jitter %2.0f): headshots registered %5.1f%% (stamp accepted %5.1f%%) | RTT/2-only rewind %5.1f%% | no rewind %5.1f%%",
			(N.Down + N.Up) * 1000, N.Down * 1000, N.Up * 1000, N.Jitter * 1000, ImplRate * 100.0, 100.0 * Impl.AcceptedStamps / Impl.Shots,
			100.0 * Half.HeadHits / Half.Shots, 100.0 * None.HeadHits / None.Shots);
	}
}

SIM_TEST(LagCompensation, ToleratesProxySmoothingMismatch)
{
	// If real proxy smoothing differs from the assumed 15.6 ms, how much margin is left?
	for (const double ErrorMs : { -8.0, -4.0, 4.0, 8.0, 16.0 })
	{
		const FDuelResult R = SimulateDuel(0.035, 0.035, 0.006, ERewindMode::Implemented, ErrorMs / 1000.0);
		const double Rate = double(R.HeadHits) / R.Shots;
		if (std::abs(ErrorMs) <= 8.0) { SIM_EXPECT_GE(Rate, 0.95); }
		SimTest::Report("display timing off by %+5.1f ms: headshots registered %5.1f%%, any hit %5.1f%%", ErrorMs, Rate * 100.0, 100.0 * R.AnyHits / R.Shots);
	}
}
