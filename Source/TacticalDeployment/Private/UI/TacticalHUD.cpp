// Copyright TacticalDeployment. All Rights Reserved.

#include "UI/TacticalHUD.h"
#include "Character/TacticalCharacter.h"
#include "Character/TacticalCharacterMovementComponent.h"
#include "Core/HudMath.h"
#include "Core/TacticalGameUserSettings.h"
#include "Game/SpikeBase.h"
#include "Game/TacticalGameState.h"
#include "Game/TacticalPlayerController.h"
#include "Game/TacticalPlayerState.h"
#include "Weapons/TacticalWeapon.h"
#include "Weapons/WeaponStats.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"

namespace HudColors
{
	static const FLinearColor Text(0.95f, 0.95f, 0.92f, 1.f);
	static const FLinearColor Dim(0.6f, 0.6f, 0.6f, 0.85f);
	static const FLinearColor Ally(0.25f, 0.85f, 0.85f, 1.f);
	static const FLinearColor Enemy(1.f, 0.3f, 0.3f, 1.f);
	static const FLinearColor Headshot(1.f, 0.8f, 0.2f, 1.f);
	static const FLinearColor Panel(0.f, 0.f, 0.f, 0.45f);
	static const FLinearColor Outline(0.f, 0.f, 0.f, 0.9f);
}

ATacticalHUD::ATacticalHUD()
{
}

void ATacticalHUD::BeginPlay()
{
	Super::BeginPlay();
	ApplyHighlightColor();
}

void ATacticalHUD::ApplyHighlightColor()
{
	const UTacticalGameUserSettings* Settings = UTacticalGameUserSettings::Get();
	if (HighlightParameters && Settings)
	{
		UKismetMaterialLibrary::SetVectorParameterValue(this, HighlightParameters, HighlightColorParameter, Settings->GetEnemyHighlightColor());
	}
}

void ATacticalHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !PlayerOwner)
	{
		return;
	}

	UIScale = Canvas->ClipY / 1080.f;
	SmoothedFrameTime = FMath::Lerp(SmoothedFrameTime, GetWorld()->GetDeltaSeconds(), 0.05f);

	const ATacticalGameState* GameState = GetWorld()->GetGameState<ATacticalGameState>();
	const ATacticalPlayerState* LocalState = PlayerOwner->GetPlayerState<ATacticalPlayerState>();
	const ATacticalCharacter* Character = Cast<ATacticalCharacter>(PlayerOwner->GetPawn());

	if (Character && Character->IsAlive())
	{
		DrawCrosshair(Character);
		DrawHitMarker();
		DrawSpikeInteraction(Character, GameState);
	}
	if (GameState)
	{
		DrawRoundState(GameState, LocalState);
		DrawKillFeed(GameState);
	}
	DrawVitals(Character, GameState, LocalState);
	DrawNetStats();
}

// ---------------------------------------------------------------------------------------

void ATacticalHUD::DrawTextAligned(const FString& Text, const FLinearColor& Color, float X, float Y, UFont* Font, float Scale, float AlignX)
{
	float Width = 0.f, Height = 0.f;
	GetTextSize(Text, Width, Height, Font, Scale);
	const float Left = X - Width * AlignX;
	DrawText(Text, FLinearColor(0.f, 0.f, 0.f, Color.A * 0.8f), Left + 1.f, Y + 1.f, Font, Scale); // Drop shadow for readability on any background.
	DrawText(Text, Color, Left, Y, Font, Scale);
}

void ATacticalHUD::DrawBar(float X, float Y, float Width, float Height, float Fill, const FLinearColor& FillColor)
{
	DrawRect(HudColors::Panel, X, Y, Width, Height);
	DrawRect(FillColor, X, Y, Width * FMath::Clamp(Fill, 0.f, 1.f), Height);
}

void ATacticalHUD::DrawCrosshair(const ATacticalCharacter* Character)
{
	const UTacticalGameUserSettings* Settings = UTacticalGameUserSettings::Get();
	const FCrosshairSettings Crosshair = Settings ? Settings->Crosshair : FCrosshairSettings();
	const ATacticalWeapon* Weapon = Character->GetCurrentWeapon();
	if (!Weapon)
	{
		return;
	}

	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	const float Fov = PlayerOwner->PlayerCameraManager ? PlayerOwner->PlayerCameraManager->GetFOVAngle() : 103.f;

	// Honest crosshair: the gap opens by exactly the screen radius of the current spread cone.
	const float SpreadPixels = Crosshair.bShowSpread ? HudMath::SpreadToPixels(Weapon->GetPredictedSpreadDegrees(), Fov, Canvas->ClipX) : 0.f;
	const float Gap = Crosshair.Gap + SpreadPixels;
	const float Length = Crosshair.Length;
	const float Thick = Crosshair.Thickness;
	const float Half = Thick * 0.5f;

	auto DrawLines = [&](const FLinearColor& Color, float Grow)
	{
		DrawRect(Color, CenterX + Gap - Grow, CenterY - Half - Grow, Length + 2.f * Grow, Thick + 2.f * Grow);          // Right
		DrawRect(Color, CenterX - Gap - Length - Grow, CenterY - Half - Grow, Length + 2.f * Grow, Thick + 2.f * Grow); // Left
		DrawRect(Color, CenterX - Half - Grow, CenterY + Gap - Grow, Thick + 2.f * Grow, Length + 2.f * Grow);          // Down
		DrawRect(Color, CenterX - Half - Grow, CenterY - Gap - Length - Grow, Thick + 2.f * Grow, Length + 2.f * Grow); // Up
		if (Crosshair.bCenterDot)
		{
			DrawRect(Color, CenterX - Half - Grow, CenterY - Half - Grow, Thick + 2.f * Grow, Thick + 2.f * Grow);
		}
	};

	if (Crosshair.bOutline)
	{
		DrawLines(HudColors::Outline, 1.f);
	}
	DrawLines(Crosshair.Color, 0.f);
}

void ATacticalHUD::DrawHitMarker()
{
	const UTacticalGameUserSettings* Settings = UTacticalGameUserSettings::Get();
	const ATacticalPlayerController* PC = Cast<ATacticalPlayerController>(PlayerOwner);
	if (!PC || (Settings && !Settings->bShowHitMarkers))
	{
		return;
	}

	const ATacticalPlayerController::FHitFeedback& Hit = PC->GetLastHitFeedback();
	const float Alpha = HudMath::FadeAlpha(GetWorld()->GetTimeSeconds(), Hit.Time, HitMarkerDuration);
	if (Alpha <= 0.f)
	{
		return;
	}

	FLinearColor Color = Hit.bKill ? HudColors::Enemy : (Hit.Zone == EHitZone::Head ? HudColors::Headshot : HudColors::Text);
	Color.A = Alpha * (Hit.bWallbang ? 0.6f : 1.f);
	const float CX = Canvas->ClipX * 0.5f, CY = Canvas->ClipY * 0.5f;
	const float Inner = 9.f * UIScale, Outer = (Hit.bKill ? 22.f : 17.f) * UIScale;
	for (const FVector2D Dir : { FVector2D(1, 1), FVector2D(-1, 1), FVector2D(1, -1), FVector2D(-1, -1) })
	{
		DrawLine(CX + Dir.X * Inner, CY + Dir.Y * Inner, CX + Dir.X * Outer, CY + Dir.Y * Outer, Color, 2.f * UIScale);
	}
}

void ATacticalHUD::DrawRoundState(const ATacticalGameState* GameState, const ATacticalPlayerState* LocalState)
{
	const FTacticalRoundState& Round = GameState->GetRoundState();
	const ETacticalTeam LocalTeam = LocalState ? LocalState->GetTeam() : ETacticalTeam::TeamA;
	const ETacticalTeam OtherTeam = GetOpposingTeam(LocalTeam);
	const bool bAttacking = LocalTeam == GameState->GetAttackingTeam();

	UFont* Large = GEngine->GetLargeFont();
	UFont* Small = GEngine->GetSmallFont();
	const float CX = Canvas->ClipX * 0.5f;
	const float Top = 18.f * UIScale;

	// Scores: own team always on the left, in the ally colour.
	DrawRect(HudColors::Panel, CX - 150.f * UIScale, Top - 6.f * UIScale, 300.f * UIScale, 62.f * UIScale);
	DrawTextAligned(FString::FromInt(GameState->GetScore(LocalTeam)), HudColors::Ally, CX - 95.f * UIScale, Top, Large, 1.4f * UIScale, 0.5f);
	DrawTextAligned(FString::FromInt(GameState->GetScore(OtherTeam)), HudColors::Enemy, CX + 95.f * UIScale, Top, Large, 1.4f * UIScale, 0.5f);

	// Centre: phase clock. After the plant the clock is hidden on purpose: the spike is timed by ear.
	FString Clock;
	switch (Round.Phase)
	{
	case ETacticalMatchPhase::ActionPhase:
		if (Round.bSpikePlanted)
		{
			Clock = TEXT("SPIKE");
			break;
		}
		[[fallthrough]];
	case ETacticalMatchPhase::BuyPhase:
	case ETacticalMatchPhase::BarrierPhase:
	case ETacticalMatchPhase::PreMatch:
	{
		int32 Minutes = 0, Seconds = 0;
		HudMath::SplitClock(GameState->GetPhaseTimeRemaining(), Minutes, Seconds);
		Clock = FString::Printf(TEXT("%d:%02d"), Minutes, Seconds);
		break;
	}
	default:
		break;
	}
	DrawTextAligned(Clock, Round.bSpikePlanted && Round.Phase == ETacticalMatchPhase::ActionPhase ? HudColors::Enemy : HudColors::Text,
		CX, Top, Large, 1.2f * UIScale, 0.5f);

	FString Subtitle;
	switch (Round.Phase)
	{
	case ETacticalMatchPhase::PreMatch:     Subtitle = TEXT("WAITING FOR PLAYERS"); break;
	case ETacticalMatchPhase::BuyPhase:     Subtitle = TEXT("BUY PHASE"); break;
	case ETacticalMatchPhase::BarrierPhase: Subtitle = TEXT("BARRIERS DROP SOON"); break;
	case ETacticalMatchPhase::ActionPhase:  Subtitle = FString::Printf(TEXT("ROUND %d  -  %s"), Round.RoundNumber, bAttacking ? TEXT("ATTACK") : TEXT("DEFEND")); break;
	case ETacticalMatchPhase::PostRound:    Subtitle = Round.LastRoundWinner == LocalTeam ? TEXT("ROUND WON") : TEXT("ROUND LOST"); break;
	case ETacticalMatchPhase::MatchEnded:   Subtitle = GameState->GetScore(LocalTeam) > GameState->GetScore(OtherTeam) ? TEXT("VICTORY") : TEXT("DEFEAT"); break;
	}
	DrawTextAligned(Subtitle, HudColors::Dim, CX, Top + 36.f * UIScale, Small, UIScale, 0.5f);
}

void ATacticalHUD::DrawVitals(const ATacticalCharacter* Character, const ATacticalGameState* GameState, const ATacticalPlayerState* LocalState)
{
	UFont* Large = GEngine->GetLargeFont();
	UFont* Small = GEngine->GetSmallFont();
	const float Bottom = Canvas->ClipY - 70.f * UIScale;

	if (Character && Character->IsAlive())
	{
		const float X = 40.f * UIScale;
		DrawTextAligned(FString::Printf(TEXT("%d"), FMath::CeilToInt(Character->GetHealth())), HudColors::Text, X, Bottom, Large, 1.5f * UIScale, 0.f);
		DrawBar(X, Bottom + 42.f * UIScale, 180.f * UIScale, 5.f * UIScale, Character->GetHealth() / 100.f, HudColors::Text);
		if (Character->GetArmor() > 0.f)
		{
			DrawTextAligned(FString::Printf(TEXT("ARMOR %d"), FMath::CeilToInt(Character->GetArmor())), HudColors::Ally, X, Bottom - 22.f * UIScale, Small, UIScale, 0.f);
		}

		if (const ATacticalWeapon* Weapon = Character->GetCurrentWeapon())
		{
			const int32 Ammo = Weapon->GetPredictedAmmo();
			const int32 Magazine = Weapon->GetStats() ? Weapon->GetStats()->MagazineSize : 0;
			const bool bLow = Magazine > 0 && Ammo <= Magazine / 4;
			const float Right = Canvas->ClipX - 40.f * UIScale;
			DrawTextAligned(FString::Printf(TEXT("%d"), Ammo), bLow ? HudColors::Enemy : HudColors::Text, Right - 60.f * UIScale, Bottom, Large, 1.5f * UIScale, 1.f);
			DrawTextAligned(FString::Printf(TEXT("/ %d"), Magazine), HudColors::Dim, Right, Bottom + 14.f * UIScale, Small, UIScale, 1.f);
		}
	}

	// Credits are only shown while the shop is open (owner-only replicated anyway).
	if (LocalState && GameState && GameState->IsShopOpen())
	{
		DrawTextAligned(FString::Printf(TEXT("CREDITS  %d"), LocalState->GetCredits()), HudColors::Headshot, 40.f * UIScale, Canvas->ClipY * 0.3f, Large, UIScale, 0.f);
	}
}

void ATacticalHUD::DrawSpikeInteraction(const ATacticalCharacter* Character, const ATacticalGameState* GameState)
{
	const ASpikeBase* Spike = GameState ? GameState->GetSpike() : nullptr;
	if (!Spike || Spike->GetInteraction().Interactor != Character)
	{
		return;
	}

	const FSpikeInteractionState& Interaction = Spike->GetInteraction();
	const bool bDefusing = Interaction.Type == ESpikeInteraction::Defusing;
	const float Width = 320.f * UIScale, Height = 10.f * UIScale;
	const float X = Canvas->ClipX * 0.5f - Width * 0.5f;
	const float Y = Canvas->ClipY * 0.62f;

	DrawTextAligned(bDefusing ? TEXT("DEFUSING") : TEXT("PLANTING"), HudColors::Text, Canvas->ClipX * 0.5f, Y - 26.f * UIScale, GEngine->GetSmallFont(), UIScale, 0.5f);
	DrawBar(X, Y, Width, Height, Spike->GetInteractionProgress(), bDefusing ? HudColors::Ally : HudColors::Enemy);
	if (bDefusing)
	{
		// The 50% checkpoint tick, filled once it is banked for this plant.
		const float TickX = X + Width * Spike->DefuseCheckpointFraction;
		const bool bBanked = Spike->GetDefuseCheckpoint() >= Spike->DefuseCheckpointFraction;
		DrawRect(bBanked ? HudColors::Headshot : HudColors::Text, TickX - 1.5f * UIScale, Y - 4.f * UIScale, 3.f * UIScale, Height + 8.f * UIScale);
	}
}

void ATacticalHUD::DrawKillFeed(const ATacticalGameState* GameState)
{
	UFont* Small = GEngine->GetSmallFont();
	const double Now = GetWorld()->GetTimeSeconds();
	float Y = 20.f * UIScale;
	const float Right = Canvas->ClipX - 24.f * UIScale;

	const TArray<ATacticalGameState::FKillFeedLine>& Lines = GameState->GetRecentKills();
	for (int32 i = Lines.Num() - 1; i >= 0 && Y < Canvas->ClipY * 0.35f; --i)
	{
		const ATacticalGameState::FKillFeedLine& Line = Lines[i];
		const float Alpha = HudMath::FadeAlpha(Now, Line.Time, KillFeedDuration);
		if (Alpha <= 0.f)
		{
			continue;
		}
		FLinearColor KillerColor = Line.bKillerIsAlly ? HudColors::Ally : HudColors::Enemy;
		FLinearColor VictimColor = Line.bVictimIsAlly ? HudColors::Ally : HudColors::Enemy;
		KillerColor.A = VictimColor.A = FMath::Min(1.f, Alpha * 3.f);

		const FString Middle = FString::Printf(TEXT("  [%s%s%s]  "), *Line.Weapon, Line.bHeadshot ? TEXT(" HS") : TEXT(""), Line.bWallbang ? TEXT(" WB") : TEXT(""));
		float VictimW = 0.f, MiddleW = 0.f, H = 0.f;
		GetTextSize(Line.Victim, VictimW, H, Small, UIScale);
		GetTextSize(Middle, MiddleW, H, Small, UIScale);
		DrawTextAligned(Line.Victim, VictimColor, Right, Y, Small, UIScale, 1.f);
		DrawTextAligned(Middle, FLinearColor(1.f, 1.f, 1.f, KillerColor.A), Right - VictimW, Y, Small, UIScale, 1.f);
		DrawTextAligned(Line.Killer, KillerColor, Right - VictimW - MiddleW, Y, Small, UIScale, 1.f);
		Y += (H + 6.f * UIScale);
	}
}

void ATacticalHUD::DrawNetStats()
{
	const UTacticalGameUserSettings* Settings = UTacticalGameUserSettings::Get();
	const ATacticalPlayerController* PC = Cast<ATacticalPlayerController>(PlayerOwner);
	if (!PC || (Settings && !Settings->bShowNetStats))
	{
		return;
	}
	const float Fps = SmoothedFrameTime > 0.f ? 1.f / SmoothedFrameTime : 0.f;
	const FString Stats = FString::Printf(TEXT("%.0f FPS   %.0f ms"), Fps, PC->GetSmoothedRoundTripTime() * 1000.f);
	DrawTextAligned(Stats, HudColors::Dim, 12.f * UIScale, 8.f * UIScale, GEngine->GetSmallFont(), UIScale, 0.f);
}
