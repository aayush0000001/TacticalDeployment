// Copyright TacticalDeployment. All Rights Reserved.

#include "Core/TacticalGameUserSettings.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

namespace
{
	void SetConsoleInt(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByGameSetting);
		}
	}
}

UTacticalGameUserSettings* UTacticalGameUserSettings::Get()
{
	return GEngine ? Cast<UTacticalGameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

FLinearColor UTacticalGameUserSettings::GetEnemyHighlightColor() const
{
	switch (EnemyHighlight)
	{
	case EEnemyHighlightColor::Yellow: return FLinearColor(1.f, 0.9f, 0.05f);
	case EEnemyHighlightColor::Purple: return FLinearColor(0.75f, 0.2f, 1.f);
	default:                           return FLinearColor(1.f, 0.12f, 0.12f);
	}
}

void UTacticalGameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	// Competitive defaults: uncapped frame rate, no VSync (VSync adds up to a frame of latency).
	SetVSyncEnabled(false);
	SetFrameRateLimit(0.f);
	MouseSensitivity = 1.f;
	Crosshair = FCrosshairSettings();
	bShowHitMarkers = true;
	bShowNetStats = true;
	EnemyHighlight = EEnemyHighlightColor::Red;
	bLowLatencyMode = true;
}

void UTacticalGameUserSettings::ApplyNonResolutionSettings()
{
	Super::ApplyNonResolutionSettings();
	SetConsoleInt(TEXT("r.GTSyncType"), bLowLatencyMode ? 1 : 0);
	SetConsoleInt(TEXT("r.OneFrameThreadLag"), bLowLatencyMode ? 0 : 1);
}
