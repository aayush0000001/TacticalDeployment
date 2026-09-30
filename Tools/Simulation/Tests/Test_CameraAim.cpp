// Copyright TacticalDeployment. All Rights Reserved.
// Pillar 6: aim synchronisation. Real code: Core/TacticalTypes.h (TacticalQuantize).

#include "SimTest.h"
#include "Core/TacticalTypes.h"

SIM_TEST(CameraAim, SixteenBitPitchIsPreciseEnoughForHeadHitboxes)
{
	double MaxError = 0.0;
	for (int32 i = -9000; i <= 9000; ++i)
	{
		const float Pitch = i * 0.01f;
		const float RoundTrip = TacticalQuantize::UnpackPitch(TacticalQuantize::PackPitch(Pitch));
		MaxError = std::max(MaxError, double(std::abs(RoundTrip - Pitch)));
	}
	const double Step16 = 180.0 / 65535.0;
	SIM_EXPECT_LE(MaxError, Step16 * 0.5 + 5e-5); // Half a 16-bit step plus float rounding.

	// Head offset from the spine pivot ~60 cm: position error on the aim-offset pose.
	const double Lever = 60.0;
	const double HeadError16 = Lever * FMath::DegreesToRadians(float(MaxError));
	const double HeadError8 = Lever * FMath::DegreesToRadians(float(360.0 / 256.0 * 0.5)); // Engine default RemoteViewPitch.
	SIM_EXPECT_LE(HeadError16, 0.01);
	SimTest::Report("16-bit pitch: max error %.5f deg -> head pose error %.4f mm (8-bit engine default: %.1f mm)",
		MaxError, HeadError16 * 10.0, HeadError8 * 10.0);
}

SIM_TEST(CameraAim, PitchPackingNormalizesAndClamps)
{
	using namespace TacticalQuantize;
	SIM_EXPECT_NEAR(UnpackPitch(PackPitch(0.f)), 0.0, 180.0 / 65535.0);
	SIM_EXPECT_NEAR(UnpackPitch(PackPitch(90.f)), 90.0, 1e-3);
	SIM_EXPECT_NEAR(UnpackPitch(PackPitch(-90.f)), -90.0, 1e-3);
	SIM_EXPECT_NEAR(UnpackPitch(PackPitch(350.f)), -10.0, 0.01);  // Controller pitch arrives as 0..360.
	SIM_EXPECT_NEAR(UnpackPitch(PackPitch(120.f)), 90.0, 1e-3);   // Clamped to straight up.
	SIM_EXPECT_NEAR(UnpackPitch(PackPitch(-135.f)), -90.0, 1e-3); // Clamped to straight down.
}

SIM_TEST(CameraAim, TeamHelpers)
{
	SIM_EXPECT_TRUE(GetOpposingTeam(ETacticalTeam::TeamA) == ETacticalTeam::TeamB);
	SIM_EXPECT_TRUE(GetOpposingTeam(ETacticalTeam::TeamB) == ETacticalTeam::TeamA);
	SIM_EXPECT_TRUE(GetOpposingTeam(ETacticalTeam::Spectator) == ETacticalTeam::Spectator);
	SIM_EXPECT_NEAR(TacticalNet::ServerFrameTime * 1000.0, 7.8125, 1e-6);
}
