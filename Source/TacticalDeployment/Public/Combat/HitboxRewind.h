// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

// Engine-light core of server-side rewind: plain data + math, no UObjects. Used by
// ULagCompensationComponent and compiled directly by Tools/Simulation.

#include "CoreMinimal.h"
#include "Core/TacticalTypes.h"

namespace LagCompensation
{
	inline constexpr int32 MaxTrackedCharacters = 12;    // 10 players + reconnect slack.
	inline constexpr int32 MaxHitboxesPerCharacter = 16; // Head, neck, 3 spine, 2x(upper/lower arm), pelvis, 2x(thigh/calf/foot).
	inline constexpr float HistorySeconds = 1.f;
	/** 1000 ms at 128 Hz = 128 frames, plus slack so a hitch never evicts the frame we need. */
	inline constexpr int32 HistoryCapacity = 136;
}

/**
 * One hitbox in world space at one server frame. Self-contained (shape + zone included), so a
 * rewound trace never has to consult the live character, which may have changed or be gone.
 */
struct FHitboxSnapshot
{
	FVector3f Center;
	FQuat4f Rotation;  // Capsule axis is local Z.
	float Radius;
	float HalfSegment; // 0 = sphere.
	EHitZone Zone;
};

/** One character's hitbox set at one server frame. */
struct FCharacterPoseRecord
{
	FVector3f BoundsCenter; // Broad-phase sphere.
	float BoundsRadius;
	uint8 NumHitboxes;
	bool bValid;            // Slot occupied, alive and recorded this frame.
	FHitboxSnapshot Hitboxes[LagCompensation::MaxHitboxesPerCharacter];
};

/**
 * The rewind unit: every tracked character's hitboxes at one server frame, stamped with
 * server time. POD, fixed-size, lives in a pre-allocated ring buffer: zero allocations per tick.
 */
struct FFrameRecord
{
	double ServerTime;
	FCharacterPoseRecord Characters[LagCompensation::MaxTrackedCharacters];
};

namespace TacticalHitboxMath
{
	/** Entry distance along normalized Rd, 0 if Ro is inside, -1 on miss. */
	inline float IntersectRaySphere(const FVector3f& Ro, const FVector3f& Rd, const FVector3f& Center, float Radius)
	{
		const FVector3f Oc = Ro - Center;
		const float B = Oc | Rd;
		const float C = (Oc | Oc) - Radius * Radius;
		if (C > 0.f && B > 0.f)
		{
			return -1.f; // Outside and pointing away.
		}
		const float H = B * B - C;
		if (H < 0.f)
		{
			return -1.f;
		}
		return FMath::Max(0.f, -B - FMath::Sqrt(H));
	}

	/** Ray vs. capsule (segment Pa-Pb swept by Radius). Entry distance, 0 if inside, -1 on miss. */
	inline float IntersectRayCapsule(const FVector3f& Ro, const FVector3f& Rd, const FVector3f& Pa, const FVector3f& Pb, float Radius)
	{
		const FVector3f Ba = Pb - Pa;
		const float Baba = Ba | Ba;
		if (Baba < KINDA_SMALL_NUMBER)
		{
			return IntersectRaySphere(Ro, Rd, Pa, Radius);
		}

		if (FMath::PointDistToSegmentSquared(FVector(Ro), FVector(Pa), FVector(Pb)) <= FMath::Square(Radius))
		{
			return 0.f; // Muzzle inside the hitbox.
		}

		const FVector3f Oa = Ro - Pa;
		const float Bard = Ba | Rd;
		const float Baoa = Ba | Oa;
		const float A = Baba - Bard * Bard;

		if (A > KINDA_SMALL_NUMBER)
		{
			const float B = Baba * (Rd | Oa) - Baoa * Bard;
			const float C = Baba * (Oa | Oa) - Baoa * Baoa - Radius * Radius * Baba;
			const float H = B * B - A * C;
			if (H < 0.f)
			{
				return -1.f; // Misses the infinite cylinder, therefore both caps too.
			}
			const float T = (-B - FMath::Sqrt(H)) / A;
			const float Y = Baoa + T * Bard;
			if (T >= 0.f && Y > 0.f && Y < Baba)
			{
				return T; // Cylindrical body.
			}
		}

		// Hemispherical caps: nearest valid sphere entry.
		const float Ta = IntersectRaySphere(Ro, Rd, Pa, Radius);
		const float Tb = IntersectRaySphere(Ro, Rd, Pb, Radius);
		if (Ta < 0.f) { return Tb; }
		if (Tb < 0.f) { return Ta; }
		return FMath::Min(Ta, Tb);
	}

	/** Recomputes the broad-phase sphere from the hitboxes; marks the pose valid if non-empty. */
	inline void FinalizePose(FCharacterPoseRecord& Pose)
	{
		if (Pose.NumHitboxes == 0)
		{
			Pose.bValid = false;
			return;
		}
		FVector3f Min(TNumericLimits<float>::Max());
		FVector3f Max(-TNumericLimits<float>::Max());
		float MaxExtent = 0.f;
		for (int32 i = 0; i < Pose.NumHitboxes; ++i)
		{
			const FHitboxSnapshot& Hitbox = Pose.Hitboxes[i];
			Min = Min.ComponentMin(Hitbox.Center);
			Max = Max.ComponentMax(Hitbox.Center);
			MaxExtent = FMath::Max(MaxExtent, Hitbox.Radius + Hitbox.HalfSegment);
		}
		Pose.BoundsCenter = (Min + Max) * 0.5f;
		Pose.BoundsRadius = (Max - Min).Size() * 0.5f + MaxExtent;
		Pose.bValid = true;
	}

	inline void LerpPose(const FCharacterPoseRecord& A, const FCharacterPoseRecord& B, float Alpha, FCharacterPoseRecord& Out)
	{
		// Alive in only one of the bracketing frames (spawn/death boundary): snap to the valid one.
		if (!A.bValid || !B.bValid || A.NumHitboxes != B.NumHitboxes)
		{
			const FCharacterPoseRecord& Source = (B.bValid && (Alpha >= 0.5f || !A.bValid)) ? B : A;
			FMemory::Memcpy(&Out, &Source, sizeof(FCharacterPoseRecord));
			return;
		}

		Out.bValid = true;
		Out.NumHitboxes = A.NumHitboxes;
		Out.BoundsCenter = FMath::Lerp(A.BoundsCenter, B.BoundsCenter, Alpha);
		// Each lerped hitbox offset is a convex blend of two offsets that fit their radius: max suffices.
		Out.BoundsRadius = FMath::Max(A.BoundsRadius, B.BoundsRadius);
		for (int32 i = 0; i < A.NumHitboxes; ++i)
		{
			Out.Hitboxes[i] = A.Hitboxes[i];
			Out.Hitboxes[i].Center = FMath::Lerp(A.Hitboxes[i].Center, B.Hitboxes[i].Center, Alpha);
			Out.Hitboxes[i].Rotation = FQuat4f::FastLerp(A.Hitboxes[i].Rotation, B.Hitboxes[i].Rotation, Alpha).GetNormalized();
		}
	}

	/**
	 * Nearest hitbox of one pose hit by the ray (Start, Dir) within MaxDistance.
	 * @return Hitbox index, or INDEX_NONE. OutDistance is along Dir.
	 */
	inline int32 TracePose(const FCharacterPoseRecord& Pose, const FVector& Start, const FVector3f& Dir, float MaxDistance, float& OutDistance)
	{
		if (!Pose.bValid)
		{
			return INDEX_NONE;
		}

		// Work relative to the pose centre: keeps float math precise and cheap.
		const FVector3f Origin = FVector3f(Start - FVector(Pose.BoundsCenter));
		const float BoundsT = IntersectRaySphere(Origin, Dir, FVector3f::ZeroVector, Pose.BoundsRadius);
		if (BoundsT < 0.f || BoundsT > MaxDistance)
		{
			return INDEX_NONE;
		}

		int32 Best = INDEX_NONE;
		float BestT = MaxDistance;
		for (int32 i = 0; i < Pose.NumHitboxes; ++i)
		{
			const FHitboxSnapshot& Hitbox = Pose.Hitboxes[i];
			const FVector3f Center = Hitbox.Center - Pose.BoundsCenter;
			const FVector3f Axis = Hitbox.Rotation.GetAxisZ() * Hitbox.HalfSegment;
			const float T = IntersectRayCapsule(Origin, Dir, Center - Axis, Center + Axis, Hitbox.Radius);
			if (T >= 0.f && T <= BestT)
			{
				BestT = T;
				Best = i;
			}
		}
		OutDistance = BestT;
		return Best;
	}
}

/** Fixed-capacity, time-ordered ring buffer of frame records. */
class FFrameHistory
{
public:
	void Allocate()
	{
		Frames.SetNumZeroed(LagCompensation::HistoryCapacity);
		NewestIndex = INDEX_NONE;
		Count = 0;
	}

	bool IsAllocated() const { return Frames.Num() == LagCompensation::HistoryCapacity; }
	int32 Num() const { return Count; }

	/** Claims the next slot (overwriting the oldest when full) and stamps it. */
	FFrameRecord& AddFrame(double ServerTime)
	{
		NewestIndex = (NewestIndex + 1) % LagCompensation::HistoryCapacity;
		Count = FMath::Min(Count + 1, LagCompensation::HistoryCapacity);
		FFrameRecord& Frame = Frames[NewestIndex];
		Frame.ServerTime = ServerTime;
		return Frame;
	}

	/** Logical index 0 = oldest recorded frame. */
	const FFrameRecord& GetFrame(int32 LogicalIndex) const
	{
		check(LogicalIndex >= 0 && LogicalIndex < Count);
		const int32 Oldest = NewestIndex - (Count - 1);
		return Frames[(Oldest + LogicalIndex + LagCompensation::HistoryCapacity) % LagCompensation::HistoryCapacity];
	}

	double GetNewestTime() const { return Count > 0 ? Frames[NewestIndex].ServerTime : 0.0; }
	double GetOldestTime() const { return Count > 0 ? GetFrame(0).ServerTime : 0.0; }

	/**
	 * Finds the frames bracketing Time (binary search) and the blend alpha between them.
	 * Times outside the recorded range clamp to the oldest/newest frame.
	 */
	bool FindBracket(double Time, const FFrameRecord*& OutOlder, const FFrameRecord*& OutNewer, float& OutAlpha) const
	{
		if (Count == 0)
		{
			return false;
		}

		int32 Lo = 0;
		int32 Hi = Count - 1;
		if (Time >= GetFrame(Hi).ServerTime)
		{
			Lo = Hi;
		}
		else if (Time <= GetFrame(0).ServerTime)
		{
			Hi = 0;
		}
		else
		{
			while (Hi - Lo > 1)
			{
				const int32 Mid = (Lo + Hi) / 2;
				if (GetFrame(Mid).ServerTime <= Time)
				{
					Lo = Mid;
				}
				else
				{
					Hi = Mid;
				}
			}
		}

		OutOlder = &GetFrame(FMath::Min(Lo, Hi));
		OutNewer = &GetFrame(FMath::Max(Lo, Hi));
		const double Span = OutNewer->ServerTime - OutOlder->ServerTime;
		OutAlpha = Span > UE_DOUBLE_SMALL_NUMBER ? static_cast<float>(FMath::Clamp((Time - OutOlder->ServerTime) / Span, 0.0, 1.0)) : 1.f;
		return true;
	}

private:
	TArray<FFrameRecord> Frames;
	int32 NewestIndex = INDEX_NONE;
	int32 Count = 0;
};

namespace TacticalRewind
{
	/**
	 * Server time at which a shot must be evaluated.
	 *
	 * Total rewind = downstream leg (RTT/2: the snapshot the client rendered was already this old)
	 *              + upstream leg   (RTT/2: the fire RPC's trip back)
	 *              + proxy interpolation delay.
	 * The client's stamped view time is used only if it agrees with this estimate within
	 * Tolerance; the result is clamped to [ServerNow - MaxRewind, ServerNow].
	 */
	inline double ResolveRewindTime(double ServerNow, double RoundTripSeconds, double ClientViewTime, double InterpDelay,
		double Tolerance, double MaxRewind, bool* bOutAcceptedClientTime = nullptr)
	{
		const double Estimated = ServerNow - RoundTripSeconds - InterpDelay;
		const bool bAccept = FMath::Abs(ClientViewTime - Estimated) <= Tolerance;
		if (bOutAcceptedClientTime)
		{
			*bOutAcceptedClientTime = bAccept;
		}
		return FMath::Clamp(bAccept ? ClientViewTime : Estimated, ServerNow - MaxRewind, ServerNow);
	}
}
