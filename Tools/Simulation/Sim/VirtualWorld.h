// Copyright TacticalDeployment. All Rights Reserved.
//
// Virtual level made of axis-aligned boxes (walls, crates) with a penetration density each.
// Trace semantics follow UE simple-collision queries: a ray starting inside a box reports an
// initial-overlap ("start penetrating") hit at distance 0.

#pragma once

#include "CoreMinimal.h"
#include "Weapons/ShotRules.h"

#include <string>
#include <vector>

struct FVirtualBox
{
	FVector Min;
	FVector Max;
	float Density = 1.f; // TNumericLimits<float>::Max() = impenetrable.
	std::string Name;

	bool Contains(const FVector& P) const
	{
		return P.X > Min.X && P.X < Max.X && P.Y > Min.Y && P.Y < Max.Y && P.Z > Min.Z && P.Z < Max.Z;
	}
};

struct FVirtualTraceHit
{
	bool bHit = false;
	bool bStartPenetrating = false;
	double Distance = 0.0;
	FVector ImpactPoint;
	int32 BoxIndex = INDEX_NONE;
};

class FVirtualWorld
{
public:
	std::vector<FVirtualBox> Boxes;

	int32 AddBox(const FVector& Min, const FVector& Max, float Density, const char* Name)
	{
		Boxes.push_back({ Min, Max, Density, Name });
		return static_cast<int32>(Boxes.size()) - 1;
	}

	/** Slab test. Entry/exit distances along the (unnormalized-safe) direction Dir. */
	static bool RayBox(const FVector& Origin, const FVector& Dir, const FVirtualBox& Box, double& OutEnter, double& OutExit)
	{
		double Enter = -1.0e30, Exit = 1.0e30;
		const double O[3] = { Origin.X, Origin.Y, Origin.Z };
		const double D[3] = { Dir.X, Dir.Y, Dir.Z };
		const double Lo[3] = { Box.Min.X, Box.Min.Y, Box.Min.Z };
		const double Hi[3] = { Box.Max.X, Box.Max.Y, Box.Max.Z };
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			if (std::abs(D[Axis]) < 1e-12)
			{
				if (O[Axis] < Lo[Axis] || O[Axis] > Hi[Axis]) { return false; }
				continue;
			}
			double T0 = (Lo[Axis] - O[Axis]) / D[Axis];
			double T1 = (Hi[Axis] - O[Axis]) / D[Axis];
			if (T0 > T1) { std::swap(T0, T1); }
			Enter = std::max(Enter, T0);
			Exit = std::min(Exit, T1);
			if (Enter > Exit) { return false; }
		}
		OutEnter = Enter;
		OutExit = Exit;
		return Exit >= 0.0;
	}

	/** Line trace against one box (UE LineTraceComponent-like). */
	static FVirtualTraceHit TraceBox(const FVirtualBox& Box, const FVector& Start, const FVector& End)
	{
		FVirtualTraceHit Hit;
		const FVector Delta = End - Start;
		const double Length = Delta.Size();
		if (Length <= 0.0) { return Hit; }
		const FVector Dir = Delta / Length;
		if (Box.Contains(Start))
		{
			Hit.bHit = true;
			Hit.bStartPenetrating = true;
			Hit.ImpactPoint = Start;
			return Hit;
		}
		double Enter, Exit;
		if (RayBox(Start, Dir, Box, Enter, Exit) && Enter >= 0.0 && Enter <= Length)
		{
			Hit.bHit = true;
			Hit.Distance = Enter;
			Hit.ImpactPoint = Start + Dir * Enter;
		}
		return Hit;
	}

	/** Nearest blocking box along the segment (boxes the start lies inside are skipped, like a bullet leaving an exit face). */
	FVirtualTraceHit Trace(const FVector& Start, const FVector& End) const
	{
		FVirtualTraceHit Best;
		Best.Distance = 1.0e30;
		for (int32 i = 0; i < static_cast<int32>(Boxes.size()); ++i)
		{
			if (Boxes[i].Contains(Start)) { continue; }
			const FVirtualTraceHit Hit = TraceBox(Boxes[i], Start, End);
			if (Hit.bHit && Hit.Distance < Best.Distance)
			{
				Best = Hit;
				Best.BoxIndex = i;
			}
		}
		if (!Best.bHit) { Best.Distance = 0.0; }
		return Best;
	}

	bool IsSegmentBlocked(const FVector& Start, const FVector& End) const
	{
		for (const FVirtualBox& Box : Boxes)
		{
			if (Box.Contains(Start) || TraceBox(Box, Start, End).bHit) { return true; }
		}
		return false;
	}

	/**
	 * Adapters for ShotRules::SolveBulletPath, mirroring ATacticalWeapon::TraceWithPenetration +
	 * FindExitPoint (the exit probe traces backwards from Entry + Dir * MaxThickness).
	 */
	struct FBulletAdapter
	{
		explicit FBulletAdapter(const FVirtualWorld& InWorld) : World(InWorld) {}

		const FVirtualWorld& World;
		int32 LastBox = INDEX_NONE;
		FVector LastEntry = FVector::ZeroVector;

		bool TraceWorld(const FVector& From, const FVector& To, FBulletSurfaceHit& Out)
		{
			const FVirtualTraceHit Hit = World.Trace(From, To);
			if (!Hit.bHit) { return false; }
			LastBox = Hit.BoxIndex;
			LastEntry = Hit.ImpactPoint;
			Out.ImpactPoint = Hit.ImpactPoint;
			Out.Distance = static_cast<float>(Hit.Distance);
			Out.Density = World.Boxes[LastBox].Density;
			return true;
		}

		bool ProbeExit(const FBulletSurfaceHit& Entry, const FVector& Dir, float MaxThickness, FVector& OutExit)
		{
			const FVector Probe = Entry.ImpactPoint + Dir * MaxThickness;
			const FVirtualTraceHit Hit = TraceBox(World.Boxes[LastBox], Probe, Entry.ImpactPoint);
			if (!ShotRules::IsValidExitProbe(Hit.bHit, Hit.bStartPenetrating, Hit.ImpactPoint, Entry.ImpactPoint))
			{
				return false;
			}
			OutExit = Hit.ImpactPoint;
			return true;
		}
	};
};
