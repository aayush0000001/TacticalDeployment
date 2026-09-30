// Copyright TacticalDeployment. All Rights Reserved.
//
// A standing character in the virtual world: 34 x 88 capsule (same as ATacticalCharacter) plus a
// 13-piece hitbox rig laid out like the one authored on the character Blueprint.

#pragma once

#include "CoreMinimal.h"
#include "Combat/HitboxRewind.h"

struct FVirtualCharacter
{
	static constexpr float CapsuleRadius = 34.f;
	static constexpr float CapsuleHalfHeight = 88.f;
	static constexpr float EyeHeight = 64.f; // BaseEyeHeight above the capsule centre.

	FVector Location = FVector::ZeroVector; // Capsule centre.
	FVector Velocity = FVector::ZeroVector;
	float YawDegrees = 0.f;

	FVector EyeLocation() const { return Location + FVector(0, 0, EyeHeight); }
	FVector HeadCenter() const { return Location + FVector(0, 0, 70); }

	struct FRigPiece
	{
		FVector Offset; // Local, X forward, Y right, Z up.
		float Radius;
		float HalfSegment;
		EHitZone Zone;
	};

	static const FRigPiece* Rig(int32& OutCount)
	{
		static const FRigPiece Pieces[] =
		{
			{ { 0, 0, 70 }, 11.f, 0.f, EHitZone::Head },
			{ { 0, 0, 57 }, 7.f, 3.f, EHitZone::Body },     // Neck
			{ { 0, 0, 34 }, 18.f, 9.f, EHitZone::Body },    // Chest
			{ { 0, 0, 12 }, 16.f, 7.f, EHitZone::Body },    // Stomach
			{ { 0, 0, -8 }, 16.f, 5.f, EHitZone::Body },    // Pelvis
			{ { 0, -25, 36 }, 6.f, 11.f, EHitZone::Arms },  // Upper arm L
			{ { 0, 25, 36 }, 6.f, 11.f, EHitZone::Arms },   // Upper arm R
			{ { 0, -25, 12 }, 5.f, 10.f, EHitZone::Arms },  // Lower arm L
			{ { 0, 25, 12 }, 5.f, 10.f, EHitZone::Arms },   // Lower arm R
			{ { 0, -10, -36 }, 9.f, 16.f, EHitZone::Legs }, // Thigh L
			{ { 0, 10, -36 }, 9.f, 16.f, EHitZone::Legs },  // Thigh R
			{ { 0, -10, -68 }, 7.f, 14.f, EHitZone::Legs }, // Calf L
			{ { 0, 10, -68 }, 7.f, 14.f, EHitZone::Legs },  // Calf R
		};
		OutCount = static_cast<int32>(sizeof(Pieces) / sizeof(Pieces[0]));
		return Pieces;
	}

	/** What ULagCompensationComponent::RecordCharacter produces for this character. */
	FCharacterPoseRecord BuildPose() const
	{
		FCharacterPoseRecord Pose;
		FMemory::Memzero(&Pose, sizeof(Pose));
		const FQuat4f Rotation(FVector3f(0, 0, 1), FMath::DegreesToRadians(YawDegrees));
		int32 Count = 0;
		const FRigPiece* Pieces = Rig(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			FHitboxSnapshot& Snap = Pose.Hitboxes[Pose.NumHitboxes++];
			Snap.Center = FVector3f(Location) + Rotation.RotateVector(FVector3f(Pieces[i].Offset));
			Snap.Rotation = Rotation;
			Snap.Radius = Pieces[i].Radius;
			Snap.HalfSegment = Pieces[i].HalfSegment;
			Snap.Zone = Pieces[i].Zone;
		}
		TacticalHitboxMath::FinalizePose(Pose);
		return Pose;
	}
};
