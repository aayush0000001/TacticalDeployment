// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

// Engine-light NTP-style clock synchronisation, used by ATacticalPlayerController to stamp
// fire requests with the server time of the world the client is rendering.

#include "CoreMinimal.h"

class FTacticalClockSync
{
public:
	static constexpr int32 NumSamples = 8;

	/**
	 * One probe round trip: the client sent at ClientSendTime (client clock), the server stamped
	 * ServerTime, and the reply arrived at ClientReceiveTime (client clock).
	 * @return false if the sample was rejected as stale or bogus.
	 */
	bool AddSample(double ClientSendTime, double ServerTime, double ClientReceiveTime)
	{
		const double RoundTrip = ClientReceiveTime - ClientSendTime;
		if (RoundTrip < 0.0 || RoundTrip > 2.0)
		{
			return false;
		}

		// The server stamped ServerTime ~RTT/2 before the reply reached us.
		FSample& Sample = Samples[NextIndex];
		Sample.RoundTrip = RoundTrip;
		Sample.Offset = (ServerTime + RoundTrip * 0.5) - ClientReceiveTime;
		NextIndex = (NextIndex + 1) % NumSamples;
		NumValid = FMath::Min(NumValid + 1, NumSamples);

		// NTP clock filter: the lowest-RTT sample in the window has the least queuing error.
		int32 Best = 0;
		for (int32 i = 1; i < NumValid; ++i)
		{
			if (Samples[i].RoundTrip < Samples[Best].RoundTrip)
			{
				Best = i;
			}
		}

		// Slew instead of stepping so stamped view times never jump backwards mid-spray.
		const double TargetOffset = Samples[Best].Offset;
		Offset = bHasSync ? FMath::Lerp(Offset, TargetOffset, 0.25) : TargetOffset;
		SmoothedRoundTrip = bHasSync ? FMath::Lerp(SmoothedRoundTrip, RoundTrip, 0.2) : RoundTrip;
		bHasSync = true;
		return true;
	}

	bool HasSync() const { return bHasSync; }
	double GetOffset() const { return Offset; }
	double GetSmoothedRoundTrip() const { return SmoothedRoundTrip; }

	double EstimateServerTime(double ClientNow) const { return ClientNow + Offset; }

	/** Server time of the world being rendered: now - downstream leg (RTT/2) - proxy interpolation. */
	double EstimateViewTime(double ClientNow, double InterpolationDelay) const
	{
		return EstimateServerTime(ClientNow) - SmoothedRoundTrip * 0.5 - InterpolationDelay;
	}

private:
	struct FSample
	{
		double RoundTrip = 0.0;
		double Offset = 0.0;
	};

	FSample Samples[NumSamples];
	int32 NumValid = 0;
	int32 NextIndex = 0;
	double Offset = 0.0;
	double SmoothedRoundTrip = 0.0;
	bool bHasSync = false;
};
