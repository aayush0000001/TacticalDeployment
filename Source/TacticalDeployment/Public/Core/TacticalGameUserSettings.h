// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "TacticalGameUserSettings.generated.h"

/** Colour-blind-friendly enemy outline choices (drives the custom-depth outline material). */
UENUM(BlueprintType)
enum class EEnemyHighlightColor : uint8
{
	Red,     // Default.
	Yellow,  // Protanopia / deuteranopia.
	Purple,  // Tritanopia.
};

USTRUCT(BlueprintType)
struct FCrosshairSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	FLinearColor Color = FLinearColor(0.f, 1.f, 0.6f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "1", ClampMax = "10"))
	float Thickness = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "0", ClampMax = "40"))
	float Length = 7.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "0", ClampMax = "40"))
	float Gap = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	bool bOutline = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	bool bCenterDot = false;

	/** Open the lines by the weapon's real spread (movement + firing error). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	bool bShowSpread = true;
};

/**
 * Player settings persisted to GameUserSettings.ini. Registered via
 * [/Script/Engine.Engine] GameUserSettingsClassName in DefaultEngine.ini.
 */
UCLASS(config = GameUserSettings, configdonotcheckdefaults)
class TACTICALDEPLOYMENT_API UTacticalGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	static UTacticalGameUserSettings* Get();

	FLinearColor GetEnemyHighlightColor() const;

	//~ UGameUserSettings
	virtual void SetToDefaults() override;
	virtual void ApplyNonResolutionSettings() override;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "0.05", ClampMax = "10"))
	float MouseSensitivity = 1.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "HUD")
	FCrosshairSettings Crosshair;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "HUD")
	bool bShowHitMarkers = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "HUD")
	bool bShowNetStats = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Visibility")
	EEnemyHighlightColor EnemyHighlight = EEnemyHighlightColor::Red;

	/**
	 * Trades a little CPU/GPU overlap for input latency: the game thread waits for the RHI
	 * thread (r.GTSyncType=1) and the render thread may not lag a frame (r.OneFrameThreadLag=0).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Performance")
	bool bLowLatencyMode = true;
};
