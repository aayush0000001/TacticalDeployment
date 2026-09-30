// Copyright TacticalDeployment. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "TacticalHUD.generated.h"

class UFont;
class UMaterialParameterCollection;
class ATacticalCharacter;
class ATacticalGameState;
class ATacticalPlayerState;

/**
 * Asset-free competitive HUD drawn with Canvas: spread-accurate crosshair, hit markers, round /
 * score / spike state, vitals, ammo, spike interaction bar with the 50% defuse checkpoint, kill
 * feed and net stats. Everything reads replicated state the client already has; nothing here
 * can reveal information the fog of war withheld.
 */
UCLASS()
class TACTICALDEPLOYMENT_API ATacticalHUD : public AHUD
{
	GENERATED_BODY()

public:
	ATacticalHUD();

	/** Pushes the player's enemy-outline colour into the outline material's parameter collection. */
	void ApplyHighlightColor();

	//~ AHUD
	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

protected:
	void DrawCrosshair(const ATacticalCharacter* Character);
	void DrawHitMarker();
	void DrawRoundState(const ATacticalGameState* GameState, const ATacticalPlayerState* LocalState);
	void DrawVitals(const ATacticalCharacter* Character, const ATacticalGameState* GameState, const ATacticalPlayerState* LocalState);
	void DrawSpikeInteraction(const ATacticalCharacter* Character, const ATacticalGameState* GameState);
	void DrawKillFeed(const ATacticalGameState* GameState);
	void DrawNetStats();

	void DrawTextAligned(const FString& Text, const FLinearColor& Color, float X, float Y, UFont* Font, float Scale, float AlignX);
	void DrawBar(float X, float Y, float Width, float Height, float Fill, const FLinearColor& FillColor);

	/** Material Parameter Collection read by the post-process outline material (stencil 1 = enemy). */
	UPROPERTY(EditDefaultsOnly, Category = "Visibility")
	TObjectPtr<UMaterialParameterCollection> HighlightParameters;

	UPROPERTY(EditDefaultsOnly, Category = "Visibility")
	FName HighlightColorParameter = TEXT("EnemyHighlightColor");

	UPROPERTY(EditDefaultsOnly, Category = "HUD", meta = (Units = "s"))
	float HitMarkerDuration = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category = "HUD", meta = (Units = "s"))
	float KillFeedDuration = 6.f;

private:
	float UIScale = 1.f;
	float SmoothedFrameTime = 1.f / 144.f;
};
