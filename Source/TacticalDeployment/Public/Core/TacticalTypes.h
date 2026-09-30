// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "TacticalTypes.generated.h"

TACTICALDEPLOYMENT_API DECLARE_LOG_CATEGORY_EXTERN(LogTactical, Log, All);
TACTICALDEPLOYMENT_API DECLARE_LOG_CATEGORY_EXTERN(LogTacticalHitReg, Log, All);

DECLARE_STATS_GROUP(TEXT("Tactical"), STATGROUP_Tactical, STATCAT_Advanced);

/** Simulation constants shared by every system that reasons about server frames. */
namespace TacticalNet
{
	inline constexpr float ServerTickRate = 128.f;
	inline constexpr float ServerFrameTime = 1.f / ServerTickRate; // 7.8125 ms

	/**
	 * Remote-proxy render delay on clients. Must match the CMC's Linear smoothing window
	 * (UTacticalCharacterMovementComponent::NetworkSimulatedSmoothLocationTime), because that
	 * is how far behind the newest snapshot a client actually *sees* enemies.
	 */
	inline constexpr float ProxyInterpolationDelay = 2.f * ServerFrameTime;
}

/** Must match [/Script/Engine.CollisionProfile] in DefaultEngine.ini. */
namespace TacticalCollision
{
	inline constexpr ECollisionChannel WeaponTrace = ECC_GameTraceChannel1;
	inline constexpr ECollisionChannel FogOcclusion = ECC_GameTraceChannel2;
	inline constexpr ECollisionChannel SpawnBarrier = ECC_GameTraceChannel3;
}

/** Team identity is stable for the whole match; the attacking *side* swaps at halftime. */
UENUM(BlueprintType)
enum class ETacticalTeam : uint8
{
	TeamA,
	TeamB,
	Spectator,
};

UENUM(BlueprintType)
enum class ETacticalMatchPhase : uint8
{
	PreMatch,
	BuyPhase,      // Frozen in spawn, shop open.
	BarrierPhase,  // Free movement inside spawn barriers, shop open.
	ActionPhase,   // Barriers down, weapons live, spike can be planted.
	PostRound,     // Result shown, no damage.
	MatchEnded,
};

UENUM(BlueprintType)
enum class ETacticalRoundEndReason : uint8
{
	None,
	Elimination,
	SpikeDetonated,
	SpikeDefused,
	TimeExpired,
};

/** Hit zones map directly to damage multipliers on UWeaponStats. */
UENUM(BlueprintType)
enum class EHitZone : uint8
{
	None,
	Head,
	Body,
	Arms,
	Legs,
};

UENUM(BlueprintType)
enum class EWallPenetrationTier : uint8
{
	Low,
	Medium,
	High,
};

/** Coarse movement state used by the spread model (derived, never replicated). */
UENUM(BlueprintType)
enum class ETacticalMovementState : uint8
{
	Stationary, // Below the accuracy threshold (counter-strafed or standing).
	Walking,    // Shift-walk: silent and moderately accurate.
	Running,
	Airborne,
};

FORCEINLINE uint8 TeamToIndex(ETacticalTeam Team) { return static_cast<uint8>(Team); }

FORCEINLINE ETacticalTeam GetOpposingTeam(ETacticalTeam Team)
{
	return Team == ETacticalTeam::TeamA ? ETacticalTeam::TeamB
		: Team == ETacticalTeam::TeamB ? ETacticalTeam::TeamA
		: ETacticalTeam::Spectator;
}

/** 16-bit pitch quantization used for the 3P aim offset (8-bit engine default is too coarse for head hitboxes). */
namespace TacticalQuantize
{
	FORCEINLINE uint16 PackPitch(float PitchDegrees)
	{
		const float Clamped = FMath::Clamp(FRotator::NormalizeAxis(PitchDegrees), -90.f, 90.f);
		return static_cast<uint16>(FMath::RoundToInt((Clamped + 90.f) * (65535.f / 180.f)));
	}

	FORCEINLINE float UnpackPitch(uint16 Packed)
	{
		return static_cast<float>(Packed) * (180.f / 65535.f) - 90.f;
	}
}
