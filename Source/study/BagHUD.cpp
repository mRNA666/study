// Fill out your copyright notice in the Description page of Project Settings.


#include "BagHUD.h"
#include "studyCharacter.h"
#include "DropItem.h"
#include "studyGameState.h"
#include "UnloadZone.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
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

	// ---------------- 左下角第三行：已交回的黄金 ----------------
	if (const UWorld* World = GetWorld())
	{
		if (const AstudyGameState* GS = World->GetGameState<AstudyGameState>())
		{
			const FString GoldText = FString::Printf(TEXT("已交回   %.1f g   （背上 %.1f g）"),
				GS->GetGoldGrams(0), Character->GetCarriedGoldGrams());

			DrawText(GoldText, FLinearColor(1.f, 0.85f, 0.35f, 1.f),
				ScreenX, Y + LineHeight * 2.f, nullptr, FontScale * 0.75f, false);
		}
	}

	// ---------------- 卸货区标记（红框 + 中文标签） ----------------
	DrawUnloadZones();
}

void ABagHUD::DrawUnloadZones()
{
	UWorld* World = GetWorld();
	APlayerController* PC = GetOwningPlayerController();
	if (!World || !PC || !Canvas)
	{
		return;
	}

	// 每 2 秒重扫一次场景里的卸货区。比每帧 GetAllActorsOfClass 便宜得多，
	// 而且这样才做得出来"你走过去之前就能看见它在哪"。
	const double Now = World->GetTimeSeconds();
	if (Now >= NextZoneScanTime || CachedZones.Num() == 0)
	{
		NextZoneScanTime = Now + 2.0;

		CachedZones.Reset();
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(this, AUnloadZone::StaticClass(), Found);
		for (AActor* FoundActor : Found)
		{
			CachedZones.Add(FoundActor);
		}
	}

	if (CachedZones.Num() == 0)
	{
		return;
	}

	// 遮挡测试的起点用相机位置（不是角色位置）—— 视线从眼睛出发，不是从脚下
	const APawn* Pawn = PC->GetPawn();
	const FVector ViewLoc = PC->PlayerCameraManager
		? PC->PlayerCameraManager->GetCameraLocation()
		: (Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector);

	FCollisionQueryParams TraceParams(FName(TEXT("UnloadZoneMarker")), /*bTraceComplex*/ false);
	if (Pawn)
	{
		TraceParams.AddIgnoredActor(Pawn);		// 别打到自己身上的胶囊
	}

	// 每条边切 3 段 → 一圈 12 个采样点。点够密才看得出"被墙挡住的地方断开了"
	const int32 SamplesPerEdge = 3;

	for (const TWeakObjectPtr<AActor>& WeakZone : CachedZones)
	{
		const AActor* Zone = WeakZone.Get();
		if (!Zone)
		{
			continue;
		}

		const FBox Box = Zone->GetComponentsBoundingBox(/*bNonColliding*/ true);
		if (!Box.IsValid)
		{
			continue;
		}

		// 太远就不画了，省掉一堆遮挡射线
		if (FVector::Dist(Box.GetCenter(), ViewLoc) > 4000.f)
		{
			continue;
		}

		// 遮挡测试：从相机往目标点打一条射线，中间没东西挡住才算可见
		TraceParams.AddIgnoredActor(Zone);
		auto IsVisible = [&](const FVector& Pt) -> bool
		{
			FHitResult Hit;
			const bool bBlocked = World->LineTraceSingleByChannel(
				Hit, ViewLoc, Pt, ECC_Visibility, TraceParams);
			return !bBlocked;
		};

		// ---- 地板上的矩形：取盒子底面，抬高 2cm 免得和地板重合 ----
		const float FloorZ = Box.Min.Z + 2.f;
		const FVector Quad[4] =
		{
			{Box.Min.X, Box.Min.Y, FloorZ},
			{Box.Max.X, Box.Min.Y, FloorZ},
			{Box.Max.X, Box.Max.Y, FloorZ},
			{Box.Min.X, Box.Max.Y, FloorZ}
		};

		TArray<FVector> Ring;
		Ring.Reserve(4 * SamplesPerEdge);
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector& A = Quad[i];
			const FVector& B = Quad[(i + 1) % 4];
			for (int32 s = 0; s < SamplesPerEdge; ++s)
			{
				Ring.Add(FMath::Lerp(A, B, static_cast<float>(s) / static_cast<float>(SamplesPerEdge)));
			}
		}

		const int32 Num = Ring.Num();
		TArray<FVector2D> ScreenPts;
		ScreenPts.SetNum(Num);
		TArray<bool> PtVisible;
		PtVisible.SetNum(Num);

		for (int32 i = 0; i < Num; ++i)
		{
			// 被挡住 或 在相机背后 → 这个点不画
			PtVisible[i] = IsVisible(Ring[i])
				&& UGameplayStatics::ProjectWorldToScreen(PC, Ring[i], ScreenPts[i], /*bPlayerViewportRelative*/ true);
		}

		for (int32 i = 0; i < Num; ++i)
		{
			const int32 J = (i + 1) % Num;
			if (PtVisible[i] && PtVisible[J])
			{
				DrawLine(ScreenPts[i].X, ScreenPts[i].Y, ScreenPts[J].X, ScreenPts[J].Y, ZoneEdgeColor, 3.f);
			}
		}

		// ---- 标签：盒子顶部中心上方 60cm，同样要过遮挡测试 ----
		const FVector LabelWorld = Box.GetCenter() + FVector(0.f, 0.f, Box.GetExtent().Z + 60.f);
		if (!IsVisible(LabelWorld))
		{
			continue;
		}

		FVector2D LabelScreen;
		if (!UGameplayStatics::ProjectWorldToScreen(PC, LabelWorld, LabelScreen, true))
		{
			continue;
		}

		// 近大远小
		const float Dist = FVector::Dist(LabelWorld, ViewLoc);
		const float DistAlpha = FMath::Clamp((Dist - 300.f) / 2700.f, 0.f, 1.f);
		const float LabelScale = FMath::Lerp(1.8f, 0.7f, DistAlpha) * FontScale * 0.5f;

		const FString Label = TEXT("卸货区");
		float TextW = 0.f, TextH = 0.f;
		GetTextSize(Label, TextW, TextH, nullptr, LabelScale);

		DrawText(Label, ZoneTextColor,
			LabelScreen.X - TextW * 0.5f, LabelScreen.Y - TextH * 0.5f,
			nullptr, LabelScale, /*bScalePosition*/ false);
	}
}
