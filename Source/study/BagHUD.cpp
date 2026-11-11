// Fill out your copyright notice in the Description page of Project Settings.


#include "BagHUD.h"
#include "studyCharacter.h"
#include "DropItem.h"
#include "studyGameState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

namespace
{
	/** 含金系数 → 档位词（DESIGN-dig §9.6：用档位词而不是百分比，保住"挖出来才知道"的赌感）
	 *  阈值按 v0.3 的新分布重标：出矿概率 35%，出矿后含金系数按 V^2 偏斜，多数落在低段 */
	FString GoldWord(float GoldContent)
	{
		if (GoldContent < 0.001f) { return TEXT("石渣"); }
		if (GoldContent < 0.05f) { return TEXT("含金：微"); }
		if (GoldContent < 0.14f) { return TEXT("含金：中"); }
		return TEXT("含金：富");
	}
}

void ABagHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	// ---------------- 顶部：对局阶段 + 倒计时 ----------------
	// 放在取 Character 之前：就算暂时没有角色，也能看到对局状态
	if (const UWorld* World = GetWorld())
	{
		if (const AstudyGameState* GS = World->GetGameState<AstudyGameState>())
		{
			FString PhaseText;
			FLinearColor PhaseColor = FLinearColor::White;

			switch (GS->GetPhase())
			{
			case EGamePhase::WaitingToStart:
				PhaseText = TEXT("准备中……");
				PhaseColor = FLinearColor(1.f, 0.82f, 0.2f, 1.f);
				break;

			case EGamePhase::InProgress:
			{
				// 上取整：剩 0.4 秒时仍显示 0:01，不会提前跳到 0:00
				const int32 Total = FMath::CeilToInt(GS->GetTimeRemaining());
				PhaseText = FString::Printf(TEXT("剩余   %d:%02d"), Total / 60, Total % 60);
				if (Total <= 30)
				{
					PhaseColor = FLinearColor(1.f, 0.3f, 0.2f, 1.f);	// 最后 30 秒变红
				}
				break;
			}

			case EGamePhase::Finished:
				PhaseText = TEXT("时间到");
				PhaseColor = FLinearColor(1.f, 0.55f, 0.1f, 1.f);
				break;
			}

			const float PhaseFontScale = FontScale * 1.5f;
			float PhaseW = 0.f, PhaseH = 0.f;
			GetTextSize(PhaseText, PhaseW, PhaseH, nullptr, PhaseFontScale);
			DrawText(PhaseText, PhaseColor,
				Canvas->SizeX * 0.5f - PhaseW * 0.5f, 40.f,
				nullptr, PhaseFontScale, /*bScalePosition*/ false);
		}
	}

	const AstudyCharacter* Character = Cast<AstudyCharacter>(GetOwningPawn());
	if (!Character)
	{
		return;
	}

	const float CenterX = Canvas->SizeX * 0.5f;
	const float CenterY = Canvas->SizeY * 0.5f;

	// ---------------- 准星 ----------------
	// 对准石头时准星变绿，平时白色半透明
	const ADropItem* Focused = Character->GetFocusedDrop();
	const FLinearColor CrossColor = Focused
		? FLinearColor(0.2f, 1.f, 0.35f, 1.f)
		: FLinearColor(1.f, 1.f, 1.f, 0.55f);

	const float Arm = Focused ? 14.f : 10.f;      // 对准时准星稍微变大
	const float Thick = 2.5f;
	DrawLine(CenterX - Arm, CenterY, CenterX + Arm, CenterY, CrossColor, Thick);
	DrawLine(CenterX, CenterY - Arm, CenterX, CenterY + Arm, CrossColor, Thick);

	// ---------------- 对准石头时显示信息 ----------------
	if (Focused)
	{
		const FString InfoText = FString::Printf(TEXT("%.1f kg   %s   [E] 拾取"),
			Focused->WeightKg, *GoldWord(Focused->GoldContent));

		// 画在准星下方一点。用 Canvas 的 StrLen 估算宽度，让文字水平居中
		float TextW = 0.f, TextH = 0.f;
		GetTextSize(InfoText, TextW, TextH, /*Font*/ nullptr, FontScale * 0.8f);

		DrawText(InfoText, FLinearColor(1.f, 1.f, 0.85f, 1.f),
			CenterX - TextW * 0.5f, CenterY + 40.f,
			/*Font*/ nullptr, FontScale * 0.8f, /*bScalePosition*/ false);
	}

	// ---------------- 左下角：背包 ----------------
	const float Weight = Character->GetCarriedWeightKg();
	const float MaxWeight = FMath::Max(Character->GetMaxCarryKg(), 1.f);
	const float Ratio = Character->GetLoadRatio();

	FLinearColor Color = FLinearColor::White;
	if (Ratio >= 0.999f)
	{
		Color = FLinearColor(1.f, 0.25f, 0.2f, 1.f);
	}
	else if (Ratio >= 0.8f)
	{
		Color = FLinearColor(1.f, 0.8f, 0.2f, 1.f);
	}

	const float Y = Canvas->SizeY - ScreenYFromBottom;

	const FString WeightText = FString::Printf(TEXT("背包   %.1f / %.1f kg   (%.0f%%)"),
		Weight, MaxWeight, Ratio * 100.f);
	DrawText(WeightText, Color, ScreenX, Y, /*Font*/ nullptr, FontScale, /*bScalePosition*/ false);

	FString SpeedText;
	if (const APawn* Pawn = GetOwningPawn())
	{
		if (const UCharacterMovementComponent* MoveComp = Pawn->FindComponentByClass<UCharacterMovementComponent>())
		{
			SpeedText = FString::Printf(TEXT("移速   %.0f"), MoveComp->MaxWalkSpeed);
		}
	}
	if (!SpeedText.IsEmpty())
	{
		DrawText(SpeedText, FLinearColor(0.75f, 0.85f, 1.f, 1.f),
			ScreenX, Y + LineHeight, nullptr, FontScale * 0.75f, false);
	}
}
