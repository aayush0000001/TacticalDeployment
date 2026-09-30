// Shim: UCurveVector with keyed channels. Weapon code only samples recoil patterns at integer
// key times (bullet indices), where every UE interpolation mode returns the key value exactly;
// between keys this shim interpolates linearly.
#pragma once
#include "CoreMinimal.h"

class UCurveVector : public UObject
{
public:
	/** Test helper: adds a key to all three channels. */
	void AddKey(float Time, const FVector& Value)
	{
		Keys.push_back({ Time, Value });
		std::sort(Keys.begin(), Keys.end(), [](const FKey& A, const FKey& B) { return A.Time < B.Time; });
	}

	FVector GetVectorValue(float InTime) const
	{
		if (Keys.empty()) { return FVector::ZeroVector; }
		if (InTime <= Keys.front().Time) { return Keys.front().Value; }
		if (InTime >= Keys.back().Time) { return Keys.back().Value; }
		for (size_t i = 1; i < Keys.size(); ++i)
		{
			if (InTime <= Keys[i].Time)
			{
				const double Alpha = (InTime - Keys[i - 1].Time) / (Keys[i].Time - Keys[i - 1].Time);
				return Keys[i - 1].Value + (Keys[i].Value - Keys[i - 1].Value) * Alpha;
			}
		}
		return Keys.back().Value;
	}

	void GetTimeRange(float& MinTime, float& MaxTime) const
	{
		MinTime = Keys.empty() ? 0.f : Keys.front().Time;
		MaxTime = Keys.empty() ? 0.f : Keys.back().Time;
	}

private:
	struct FKey { float Time; FVector Value; };
	std::vector<FKey> Keys;
};
