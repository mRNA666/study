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

	// ---------------- 左上角：第几天 + 时刻 ----------------
	// 放在取 Character 之前：就算暂时没有角色，也能看到时间
	if (const UWorld* World = GetWorld())
	{
		if (const AstudyGameState* GS = World->GetGameState<AstudyGameState>())
		{
			const int32 TotalMin = FMath::FloorToInt(GS->GetClockMinutes());
			const FString ClockText = FString::Printf(TEXT("第 %d 天    %02d:%02d"),
				GS->GetDayIndex(), TotalMin / 60, TotalMin % 60);

			const float ClockScale = FontScale * 1.1f;
			float ClockW = 0.f, ClockH = 0.f;
			GetTextSize(ClockText, ClockW, ClockH, nullptr, ClockScale);

			const float BoxPadX = 18.f;
			const float BoxPadY = 8.f;
			const float BoxX = ScreenX;
			const float BoxY = 36.f;

			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.42f),
				BoxX, BoxY, ClockW + BoxPadX * 2.f, ClockH + BoxPadY * 2.f);

			DrawText(ClockText, FLinearColor(1.f, 0.95f, 0.82f, 1.f),
				BoxX + BoxPadX, BoxY + BoxPadY, nullptr, ClockScale, /*bScalePosition*/ false);

			// 在矿洞里：显示开采到几点收工
			if (GS->GetMineStopClock() > 0.f)
			{
				const int32 StopMin = FMath::FloorToInt(GS->GetMineStopClock());
				const FString MineText = FString::Printf(TEXT("开采至   %02d:%02d"), StopMin / 60, StopMin % 60);

				const float MineScale = FontScale * 0.95f;
				float MW = 0.f, MH = 0.f;
				GetTextSize(MineText, MW, MH, nullptr, MineScale);

				const float MY = BoxY + ClockH + BoxPadY * 2.f + 8.f;
				DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.42f), ScreenX, MY, MW + BoxPadX * 2.f, MH + BoxPadY * 2.f);
				DrawText(MineText, FLinearColor(1.f, 0.62f, 0.28f, 1.f), ScreenX + BoxPadX, MY + BoxPadY, nullptr, MineScale, false);
			}

			// 回营地时的一次性提示：这一趟从矿洞带回来多少黄金
			if (GS->GetNoticeTimeLeft() > 0.f && GS->GetNoticeGoldGrams() > 0.f)
			{
				const FString Notice = FString::Printf(TEXT("本趟带回   %.1f g   黄金"),
					GS->GetNoticeGoldGrams());

				const float NoticeScale = FontScale * 1.2f;
				float NW = 0.f, NH = 0.f;
				GetTextSize(Notice, NW, NH, nullptr, NoticeScale);

				// 最后 1.5 秒淡出
				const float Alpha = FMath::Clamp(GS->GetNoticeTimeLeft() / 1.5f, 0.f, 1.f);

				const float NX = Canvas->SizeX * 0.5f - NW * 0.5f;
				const float NY = Canvas->SizeY * 0.32f;

				DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f * Alpha),
					NX - 22.f, NY - 8.f, NW + 44.f, NH + 16.f);

				DrawText(Notice, FLinearColor(1.f, 0.85f, 0.3f, Alpha),
					NX, NY, nullptr, NoticeScale, /*bScalePosition*/ false);
			}
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

	// ---------------- 左下角面板：背包 + 已交回 ----------------
	const float Weight = Character->GetCarriedWeightKg();
	const float MaxWeight = FMath::Max(Character->GetMaxCarryKg(), 1.f);
	const float Ratio = Character->GetLoadRatio();

	FLinearColor LoadColor(0.92f, 0.95f, 1.f, 1.f);
	if (Ratio >= 0.999f)
	{
		LoadColor = FLinearColor(1.f, 0.28f, 0.22f, 1.f);		// 满了：红
	}
	else if (Ratio >= 0.8f)
	{
		LoadColor = FLinearColor(1.f, 0.80f, 0.25f, 1.f);		// 快满：黄
	}

	// 金重 = 现在背包里背着的黄金（交进卸货区/暂存点后就清零）
	float GoldGrams = Character->GetCarriedGoldGrams();
	int32 Cash = 0;
	if (const UWorld* World = GetWorld())
	{
		if (const AstudyGameState* GS = World->GetGameState<AstudyGameState>())
		{
			Cash = GS->GetCash(0);
		}
	}

	const float LabelScale = FontScale * 0.52f;
	const float ValueScale = FontScale * 0.90f;
	const float Pad = 18.f;
	const float BarH = 12.f;
	const FLinearColor LabelColor(0.68f, 0.72f, 0.78f, 1.f);

	auto Measure = [this](const FString& S, float Scale) -> FVector2D
	{
		float MW = 0.f, MH = 0.f;
		GetTextSize(S, MW, MH, nullptr, Scale);
		return FVector2D(MW, MH);
	};

	const FString BagLabel = TEXT("背包");
	const FString WeightVal = FString::Printf(TEXT("%.1f / %.1f kg"), Weight, MaxWeight);
	const FString PctVal = FString::Printf(TEXT("%.0f%%"), Ratio * 100.f);
	const FString GoldLabel = TEXT("金重");
	const FString GoldVal = FString::Printf(TEXT("%.1f g"), GoldGrams);
	const FString CashLabel = TEXT("现金");
	const FString CashVal = FText::AsNumber(Cash).ToString();

	const FVector2D SzBag = Measure(BagLabel, LabelScale);
	const FVector2D SzW = Measure(WeightVal, ValueScale);
	const FVector2D SzP = Measure(PctVal, ValueScale);
	const FVector2D SzGL = Measure(GoldLabel, LabelScale);
	const FVector2D SzGV = Measure(GoldVal, ValueScale);
	const FVector2D SzCL = Measure(CashLabel, LabelScale);
	const FVector2D SzCV = Measure(CashVal, ValueScale);

	// 资源行：左小标签 + 右大数值，两行（金重 / 现金）
	const float ResourceRowH = FMath::Max(SzGL.Y + SzGV.Y + 2.f, SzCL.Y + SzCV.Y + 2.f);

	const float RowWidth = FMath::Max(
		FMath::Max(SzW.X + 14.f + SzP.X, SzGV.X),
		FMath::Max(SzCV.X, FMath::Max(SzBag.X, SzGL.X)));
	const float PanelW = RowWidth + Pad * 2.f;

	const float ContentH =
		SzBag.Y + 3.f +
		FMath::Max(SzW.Y, SzP.Y) + 5.f +
		BarH + 14.f +
		ResourceRowH + 10.f +
		ResourceRowH;
	const float PanelH = ContentH + Pad * 2.f;

	const float PanelX = ScreenX;
	// 参数就是字面意思：面板底边离屏幕底部多少像素（原来是 +90 的历史补偿，已去掉）
	const float PanelY = FMath::Max(Canvas->SizeY - ScreenYFromBottom - PanelH, 0.f);

	// 背板 + 左侧色条：让文字在任何背景上都看得清
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.42f), PanelX, PanelY, PanelW, PanelH);
	DrawRect(FLinearColor(LoadColor.R, LoadColor.G, LoadColor.B, 0.6f), PanelX, PanelY, 3.f, PanelH);

	float CurX = PanelX + Pad;
	float CurY = PanelY + Pad;

	// ① 背包（小标签）
	DrawText(BagLabel, LabelColor, CurX, CurY, nullptr, LabelScale, false);
	CurY += SzBag.Y + 3.f;

	// ② 重量 + 百分比（大字，左对齐 / 右对齐）
	DrawText(WeightVal, LoadColor, CurX, CurY, nullptr, ValueScale, false);
	DrawText(PctVal, LoadColor, PanelX + PanelW - Pad - SzP.X, CurY, nullptr, ValueScale, false);
	CurY += FMath::Max(SzW.Y, SzP.Y) + 5.f;

	// ③ 载重条
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), CurX, CurY, RowWidth, BarH);
	if (Ratio > 0.f)
	{
		DrawRect(LoadColor, CurX, CurY, RowWidth * FMath::Clamp(Ratio, 0.f, 1.f), BarH);
	}
	CurY += BarH + 14.f;

	const FLinearColor GoldColor(1.f, 0.84f, 0.30f, 1.f);

	// ④ 金重（小标签 + 金色大字，数值右对齐）
	DrawText(GoldLabel, LabelColor, CurX, CurY, nullptr, LabelScale, false);
	DrawText(GoldVal, GoldColor, PanelX + PanelW - Pad - SzGV.X, CurY + SzGL.Y + 2.f, nullptr, ValueScale, false);
	CurY += ResourceRowH + 10.f;

	// ⑤ 现金
	DrawText(CashLabel, LabelColor, CurX, CurY, nullptr, LabelScale, false);
	DrawText(CashVal, GoldColor, PanelX + PanelW - Pad - SzCV.X, CurY + SzCL.Y + 2.f, nullptr, ValueScale, false);

	// ---------------- 结算面板 ----------------
	DrawSettlement();

	// ---------------- 商人对话框 ----------------
	DrawDialog();

	// ---------------- 卸货区标记（红框 + 中文标签） ----------------
	DrawUnloadZones();
}

void ABagHUD::DrawDialog()
{
	const AstudyCharacter* Character = Cast<AstudyCharacter>(GetOwningPawn());
	if (!Character || !Character->IsDialogOpen() || !Canvas)
	{
		return;
	}

	const float TitleScale = FontScale * 1.05f;
	const float BodyScale = FontScale * 0.80f;
	const float OptScale = FontScale * 0.90f;
	const float Pad = 22.f;

	auto Measure = [this](const FString& S, float Scale) -> FVector2D
	{
		float MW = 0.f, MH = 0.f;
		GetTextSize(S, MW, MH, nullptr, Scale);
		return FVector2D(MW, MH);
	};

	// ---- 先把内容拆成行 ----
	TArray<FString> BodyLines;
	Character->GetDialogBody().ParseIntoArray(BodyLines, TEXT("\n"), false);

	const FVector2D TitleSz = Measure(Character->GetDialogTitle(), TitleScale);

	float ContentW = TitleSz.X;
	float ContentH = TitleSz.Y + 10.f;

	TArray<FVector2D> BodySizes;
	for (const FString& Line : BodyLines)
	{
		const FVector2D Sz = Measure(Line, BodyScale);
		BodySizes.Add(Sz);
		ContentW = FMath::Max(ContentW, Sz.X);
		ContentH += Sz.Y + 2.f;
	}
	ContentH += 8.f;

	TArray<FVector2D> OptSizes;
	const TArray<FString>& Options = Character->GetDialogOptions();
	for (int32 i = 0; i < Options.Num(); ++i)
	{
		const FString Line = FString::Printf(TEXT("%d.   %s"), i + 1, *Options[i]);
		const FVector2D Sz = Measure(Line, OptScale);
		OptSizes.Add(Sz);
		ContentW = FMath::Max(ContentW, Sz.X);
		ContentH += Sz.Y + 6.f;
	}

	// 数字输入模式
	FString InputLine;
	if (Character->IsDialogNumberMode())
	{
		InputLine = FString::Printf(TEXT("%s   %s_"), *Character->GetDialogPrompt(), *Character->GetDialogInput());
		if (Character->GetDialogInput().IsEmpty())
		{
			InputLine = FString::Printf(TEXT("%s   _"), *Character->GetDialogPrompt());
		}
		const FVector2D Sz = Measure(InputLine, TitleScale);
		ContentW = FMath::Max(ContentW, Sz.X);
		ContentH += Sz.Y + 8.f;
	}

	// 底部提示
	const FString Hint = Character->IsDialogNumberMode()
		? TEXT("数字键输入 · 回车确认 · Esc 取消")
		: TEXT("按 1 / 2 / 3 选择 · E 或 Esc 关闭");
	const FVector2D HintSz = Measure(Hint, BodyScale * 0.9f);
	ContentW = FMath::Max(ContentW, HintSz.X);
	ContentH += HintSz.Y + 8.f;

	const float PanelW = ContentW + Pad * 2.f;
	const float PanelH = ContentH + Pad * 2.f;
	const float PanelX = Canvas->SizeX * 0.5f - PanelW * 0.5f;
	const float PanelY = Canvas->SizeY * 0.60f;

	// ---- 背板 ----
	DrawRect(FLinearColor(0.02f, 0.02f, 0.03f, 0.88f), PanelX, PanelY, PanelW, PanelH);
	// 顶部标题条
	const float TitleBarH = TitleSz.Y + 14.f;
	DrawRect(FLinearColor(0.9f, 0.65f, 0.2f, 0.85f), PanelX, PanelY, PanelW, TitleBarH);
	// 左侧强调条
	DrawRect(FLinearColor(0.9f, 0.65f, 0.2f, 0.9f), PanelX, PanelY, 3.f, PanelH);

	float CurY = PanelY + 7.f;
	DrawText(Character->GetDialogTitle(), FLinearColor(0.08f, 0.06f, 0.03f, 1.f),
		PanelX + Pad, CurY, nullptr, TitleScale, false);

	CurY = PanelY + TitleBarH + 10.f;

	// ---- 正文 ----
	for (int32 i = 0; i < BodyLines.Num(); ++i)
	{
		DrawText(BodyLines[i], FLinearColor(0.78f, 0.80f, 0.85f, 1.f),
			PanelX + Pad, CurY, nullptr, BodyScale, false);
		CurY += BodySizes[i].Y + 2.f;
	}

	CurY += 8.f;

	// ---- 数字输入 ----
	if (Character->IsDialogNumberMode())
	{
		DrawText(InputLine, FLinearColor(1.f, 0.88f, 0.35f, 1.f),
			PanelX + Pad, CurY, nullptr, TitleScale, false);
		CurY += Measure(InputLine, TitleScale).Y + 8.f;
	}

	// ---- 选项 ----
	for (int32 i = 0; i < Options.Num(); ++i)
	{
		const FString Line = FString::Printf(TEXT("%d.   %s"), i + 1, *Options[i]);
		DrawText(Line, FLinearColor(0.95f, 0.92f, 0.80f, 1.f),
			PanelX + Pad, CurY, nullptr, OptScale, false);
		CurY += OptSizes[i].Y + 6.f;
	}

	// ---- 底部提示 ----
	DrawText(Hint, FLinearColor(0.55f, 0.58f, 0.64f, 1.f),
		PanelX + Pad, CurY + 2.f, nullptr, BodyScale * 0.9f, false);
}

void ABagHUD::DrawSettlement()
{
	UWorld* World = GetWorld();
	if (!World || !Canvas)
	{
		return;
	}

	const AstudyGameState* GS = World->GetGameState<AstudyGameState>();
	if (!GS || GS->GetPhase() != EGamePhase::Finished)
	{
		return;
	}

	const float TitleScale = FontScale * 1.2f;
	const float GoldScale = FontScale * 1.8f;
	const float InfoScale = FontScale * 0.8f;
	const float HintScale = FontScale * 0.75f;

	const FString TitleText = TEXT("本局结束");
	const FString GoldLabel = TEXT("最终现金");
	const FString GoldText = FText::AsNumber(GS->GetFinalCash()).ToString();
	const FString InfoText = FString::Printf(TEXT("卸货 %d 趟   ·   未卖黄金已按 65%% 折算"),
		GS->GetUnloadCount());
	const FString HintText = TEXT("按  R  再来一局");

	auto Measure = [this](const FString& S, float Scale) -> FVector2D
	{
		float MW = 0.f, MH = 0.f;
		GetTextSize(S, MW, MH, nullptr, Scale);
		return FVector2D(MW, MH);
	};

	const FVector2D SzTitle = Measure(TitleText, TitleScale);
	const FVector2D SzGoldLabel = Measure(GoldLabel, GoldScale * 0.55f);
	const FVector2D SzGold = Measure(GoldText, GoldScale);
	const FVector2D SzInfo = Measure(InfoText, InfoScale);
	const FVector2D SzHint = Measure(HintText, HintScale);

	const float PadX = 60.f;
	const float PadY = 28.f;
	const float GoldRowW = SzGoldLabel.X + 18.f + SzGold.X;

	const float ContentW = FMath::Max3(
		static_cast<float>(SzTitle.X),
		GoldRowW,
		static_cast<float>(FMath::Max(SzInfo.X, SzHint.X)));
	const float ContentH =
		SzTitle.Y + 18.f +
		FMath::Max(SzGold.Y, SzGoldLabel.Y) + 12.f +
		SzInfo.Y + 26.f +
		SzHint.Y;

	const float PanelW = ContentW + PadX * 2.f;
	const float PanelH = ContentH + PadY * 2.f;
	const float PanelX = (Canvas->SizeX - PanelW) * 0.5f;
	const float PanelY = (Canvas->SizeY - PanelH) * 0.5f;

	// 背板 + 上下两条金线
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.72f), PanelX, PanelY, PanelW, PanelH);
	DrawRect(FLinearColor(1.f, 0.84f, 0.30f, 0.9f), PanelX, PanelY, PanelW, 3.f);
	DrawRect(FLinearColor(1.f, 0.84f, 0.30f, 0.9f), PanelX, PanelY + PanelH - 3.f, PanelW, 3.f);

	float CurY = PanelY + PadY;

	// 标题
	DrawText(TitleText, FLinearColor(0.92f, 0.95f, 1.f, 1.f),
		PanelX + (PanelW - SzTitle.X) * 0.5f, CurY, nullptr, TitleScale, false);
	CurY += SzTitle.Y + 18.f;

	// 黄金：标签 + 金色大数字
	const float GoldRowX = PanelX + (PanelW - GoldRowW) * 0.5f;
	DrawText(GoldLabel, FLinearColor(0.68f, 0.72f, 0.78f, 1.f),
		GoldRowX, CurY + (SzGold.Y - SzGoldLabel.Y) * 0.5f, nullptr, GoldScale * 0.55f, false);
	DrawText(GoldText, FLinearColor(1.f, 0.84f, 0.30f, 1.f),
		GoldRowX + SzGoldLabel.X + 18.f, CurY, nullptr, GoldScale, false);
	CurY += SzGold.Y + 12.f;

	// 卸货趟数
	DrawText(InfoText, FLinearColor(0.80f, 0.84f, 0.90f, 1.f),
		PanelX + (PanelW - SzInfo.X) * 0.5f, CurY, nullptr, InfoScale, false);
	CurY += SzInfo.Y + 26.f;

	// 提示：缓慢呼吸闪烁
	const float Blink = 0.55f + 0.45f * FMath::Sin(World->GetTimeSeconds() * 3.0);
	DrawText(HintText, FLinearColor(1.f, 1.f, 1.f, Blink),
		PanelX + (PanelW - SzHint.X) * 0.5f, CurY, nullptr, HintScale, false);
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
