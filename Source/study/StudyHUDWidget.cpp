// Fill out your copyright notice in the Description page of Project Settings.


#include "StudyHUDWidget.h"
#include "studyCharacter.h"
#include "studyGameState.h"
#include "studyGameInstance.h"
#include "Merchant.h"
#include "MineGenerator.h"
#include "DropItem.h"
#include "StudySounds.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/EditableTextBox.h"
#include "Components/SizeBox.h"
#include "Styling/CoreStyle.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/** 统一取字体：FCoreStyle 的默认字体带 CJK 回退，中文能正常显示 */
	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	UTextBlock* MakeText(UWidgetTree* Tree, const FString& Str, int32 Size,
		const FLinearColor& Color, bool bBold = false)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		T->SetText(FText::FromString(Str));
		T->SetFont(Font(Size, bBold));
		T->SetColorAndOpacity(FSlateColor(Color));
		return T;
	}

	void Place(UCanvasPanel* Canvas, UWidget* W, const FVector2D& Anchor, const FVector2D& Align,
		const FVector2D& Pos)
	{
		if (UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Canvas->AddChildToCanvas(W)))
		{
			Slot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
			Slot->SetAlignment(Align);
			Slot->SetAutoSize(true);
			Slot->SetPosition(Pos);
		}
	}
}

// ---------------- 搭控件树 ----------------

TSharedRef<SWidget> UStudyHUDWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildLayout();
	}
	return Super::RebuildWidget();
}

void UStudyHUDWidget::BuildLayout()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	WidgetTree->RootWidget = Root;

	const FLinearColor Gold(1.f, 0.84f, 0.30f, 1.f);
	const FLinearColor Dim(0.68f, 0.72f, 0.78f, 1.f);

	// ---------------- 左上角：天数 + 时间 ----------------
	ClockBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	ClockBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.45f));
	ClockBorder->SetPadding(FMargin(16.f, 8.f));
	{
		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		ClockText = MakeText(WidgetTree, TEXT("第 1 天   08:00"), 24, FLinearColor(1.f, 0.95f, 0.82f, 1.f), true);
		Box->AddChildToVerticalBox(ClockText);

		MineTimeText = MakeText(WidgetTree, TEXT(""), 18, FLinearColor(1.f, 0.62f, 0.28f, 1.f));
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->AddChildToVerticalBox(MineTimeText)))
		{
			S->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
		}

		ClockBorder->SetContent(Box);
	}
	Place(Root, ClockBorder, FVector2D(0.f, 0.f), FVector2D(0.f, 0.f), FVector2D(36.f, 36.f));

	// ---------------- 左下角：背包 / 金重 / 现金 ----------------
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Panel->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.45f));
	Panel->SetPadding(FMargin(18.f, 14.f));
	{
		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

		Box->AddChildToVerticalBox(MakeText(WidgetTree, TEXT("背包"), 16, Dim));

		// 重量 + 百分比
		UHorizontalBox* WRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		WeightText = MakeText(WidgetTree, TEXT("0.0 / 50.0 kg"), 26, FLinearColor::White, true);
		PctText = MakeText(WidgetTree, TEXT("0%"), 26, FLinearColor(0.4f, 0.9f, 0.5f, 1.f), true);
		WRow->AddChildToHorizontalBox(WeightText);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(WRow->AddChildToHorizontalBox(PctText)))
		{
			S->SetPadding(FMargin(18.f, 0.f, 0.f, 0.f));
		}
		Box->AddChildToVerticalBox(WRow);

		// 载重条
		LoadBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
		LoadBar->SetPercent(0.f);
		LoadBar->SetFillColorAndOpacity(FLinearColor(0.4f, 0.9f, 0.5f, 1.f));
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->AddChildToVerticalBox(LoadBar)))
		{
			S->SetPadding(FMargin(0.f, 6.f, 0.f, 10.f));
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

		// 仓库黄金（累计已交回的，只在回营地时变）
		UHorizontalBox* GoldRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		GoldRow->AddChildToHorizontalBox(MakeText(WidgetTree, TEXT("仓库黄金"), 16, Dim));
		GoldText = MakeText(WidgetTree, TEXT("0.0 g"), 24, Gold, true);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(GoldRow->AddChildToHorizontalBox(GoldText)))
		{
			S->SetPadding(FMargin(16.f, 0.f, 0.f, 0.f));
		}
		Box->AddChildToVerticalBox(GoldRow);

		// 现金
		UHorizontalBox* CashRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		CashRow->AddChildToHorizontalBox(MakeText(WidgetTree, TEXT("现金"), 16, Dim));
		CashText = MakeText(WidgetTree, TEXT("0"), 24, Gold, true);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(CashRow->AddChildToHorizontalBox(CashText)))
		{
			S->SetPadding(FMargin(16.f, 0.f, 0.f, 0.f));
		}
		Box->AddChildToVerticalBox(CashRow);

		Panel->SetContent(Box);
	}
	Place(Root, Panel, FVector2D(0.f, 1.f), FVector2D(0.f, 1.f), FVector2D(36.f, -36.f));

	// ---------------- 准星 ----------------
	UTextBlock* Cross = MakeText(WidgetTree, TEXT("+"), 22, FLinearColor(1.f, 1.f, 1.f, 0.75f));
	Place(Root, Cross, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FVector2D(0.f, 0.f));

	// ---------------- 准星下方的提示 ----------------
	HintText = MakeText(WidgetTree, TEXT(""), 18, FLinearColor(1.f, 0.9f, 0.5f, 1.f));
	Place(Root, HintText, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.f), FVector2D(0.f, 40.f));

	// ---------------- 一次性提示（本趟带回） ----------------
	NoticeText = MakeText(WidgetTree, TEXT(""), 26, Gold, true);
	Place(Root, NoticeText, FVector2D(0.5f, 0.32f), FVector2D(0.5f, 0.5f), FVector2D(0.f, 0.f));

	// ---------------- 结算面板 ----------------
	SettlementBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	SettlementBorder->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.92f));
	SettlementBorder->SetPadding(FMargin(40.f, 28.f));
	SettlementBorder->SetVisibility(ESlateVisibility::Collapsed);
	{
		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

		Box->AddChildToVerticalBox(MakeText(WidgetTree, TEXT("本局结束"), 34, FLinearColor(1.f, 0.95f, 0.85f, 1.f), true));
		Box->AddChildToVerticalBox(MakeText(WidgetTree, TEXT("最终现金"), 18, Dim));

		SettlementCashText = MakeText(WidgetTree, TEXT("0"), 44, Gold, true);
		Box->AddChildToVerticalBox(SettlementCashText);

		SettlementInfoText = MakeText(WidgetTree, TEXT(""), 18, Dim);
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->AddChildToVerticalBox(SettlementInfoText)))
		{
			S->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
		}

		Box->AddChildToVerticalBox(MakeText(WidgetTree, TEXT("按  R  再来一局"), 22, FLinearColor(1.f, 0.85f, 0.4f, 1.f), true));

		SettlementBorder->SetContent(Box);
	}
	Place(Root, SettlementBorder, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FVector2D(0.f, 0.f));

	// ---------------- 对话框 ----------------
	DialogBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	DialogBorder->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.94f));
	DialogBorder->SetPadding(FMargin(30.f, 22.f));
	DialogBorder->SetVisibility(ESlateVisibility::Collapsed);
	{
		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

		DialogTitleText = MakeText(WidgetTree, TEXT("商人"), 28, Gold, true);
		Box->AddChildToVerticalBox(DialogTitleText);

		DialogBodyText = MakeText(WidgetTree, TEXT(""), 18, FLinearColor(0.80f, 0.82f, 0.88f, 1.f));
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->AddChildToVerticalBox(DialogBodyText)))
		{
			S->SetPadding(FMargin(0.f, 8.f, 0.f, 14.f));
		}

		// 选项按钮（最多 4 个，多余的隐藏）
		for (int32 i = 0; i < 4; ++i)
		{
			UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
			UTextBlock* Label = MakeText(WidgetTree, TEXT(""), 20, FLinearColor(0.06f, 0.06f, 0.06f, 1.f), true);
			Btn->AddChild(Label);

			switch (i)
			{
			case 0: Btn->OnClicked.AddDynamic(this, &UStudyHUDWidget::OnOption1); break;
			case 1: Btn->OnClicked.AddDynamic(this, &UStudyHUDWidget::OnOption2); break;
			case 2: Btn->OnClicked.AddDynamic(this, &UStudyHUDWidget::OnOption3); break;
			default: Btn->OnClicked.AddDynamic(this, &UStudyHUDWidget::OnOption4); break;
			}

			Btn->SetVisibility(ESlateVisibility::Collapsed);
			if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->AddChildToVerticalBox(Btn)))
			{
				S->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
			}

			OptionButtons.Add(Btn);
		}

		// 行内数字输入：卖 [____] g  [确认]
		NumberRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		NumberRow->AddChildToHorizontalBox(
			MakeText(WidgetTree, TEXT("卖"), 20, FLinearColor(0.85f, 0.87f, 0.92f, 1.f), true));

		NumberBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		NumberBox->SetHintText(FText::FromString(TEXT("克数")));
		{
			USizeBox* Box2 = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			Box2->SetWidthOverride(130.f);
			Box2->AddChild(NumberBox);

			if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(NumberRow->AddChildToHorizontalBox(Box2)))
			{
				S->SetPadding(FMargin(8.f, 0.f, 8.f, 0.f));
			}
		}

		NumberRow->AddChildToHorizontalBox(
			MakeText(WidgetTree, TEXT("g"), 20, FLinearColor(0.85f, 0.87f, 0.92f, 1.f), true));

		NumberSubmitButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		NumberSubmitButton->AddChild(
			MakeText(WidgetTree, TEXT("确认卖出"), 18, FLinearColor(0.06f, 0.06f, 0.06f, 1.f), true));
		NumberSubmitButton->OnClicked.AddDynamic(this, &UStudyHUDWidget::OnNumberSubmit);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(NumberRow->AddChildToHorizontalBox(NumberSubmitButton)))
		{
			S->SetPadding(FMargin(14.f, 0.f, 0.f, 0.f));
		}

		NumberRow->SetVisibility(ESlateVisibility::Collapsed);
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->AddChildToVerticalBox(NumberRow)))
		{
			S->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		}

		// 不放开「关闭」按钮：商人自己都有"算了 / 不买了"，
		// 想直接走开就按 E（或 Esc）
		Box->AddChildToVerticalBox(
			MakeText(WidgetTree, TEXT("按  E  或  Esc  关闭"), 15, FLinearColor(0.5f, 0.53f, 0.6f, 1.f)));

		DialogBorder->SetContent(Box);
	}
	Place(Root, DialogBorder, FVector2D(0.5f, 0.62f), FVector2D(0.5f, 0.5f), FVector2D(0.f, 0.f));

	// ---------------- 矿洞加载遮罩（全屏，必须最后加，才能盖住所有东西） ----------------
	LoadingBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	LoadingBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 1.f));
	LoadingBorder->SetHorizontalAlignment(HAlign_Center);
	LoadingBorder->SetVerticalAlignment(VAlign_Center);
	{
		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

		LoadingText = MakeText(WidgetTree, TEXT("正在开挖矿洞……"), 30,
			FLinearColor(0.92f, 0.90f, 0.85f, 1.f), true);
		Box->AddChildToVerticalBox(LoadingText);

		USizeBox* BarBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		BarBox->SetWidthOverride(400.f);
		BarBox->SetHeightOverride(14.f);

		LoadingBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
		LoadingBar->SetPercent(0.f);
		LoadingBar->SetFillColorAndOpacity(FLinearColor(0.95f, 0.72f, 0.25f, 1.f));
		BarBox->AddChild(LoadingBar);

		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->AddChildToVerticalBox(BarBox)))
		{
			S->SetPadding(FMargin(0.f, 18.f, 0.f, 0.f));
			S->SetHorizontalAlignment(HAlign_Center);
		}

		LoadingBorder->SetContent(Box);
	}
	LoadingBorder->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* LoadSlot = Cast<UCanvasPanelSlot>(Root->AddChildToCanvas(LoadingBorder)))
	{
		// 四边贴满屏幕
		LoadSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		LoadSlot->SetOffsets(FMargin(0.f));
	}
}

// ---------------- 每帧刷新 ----------------

AstudyCharacter* UStudyHUDWidget::GetStudyCharacter() const
{
	return Cast<AstudyCharacter>(GetOwningPlayerPawn());
}

AstudyGameState* UStudyHUDWidget::GetStudyState() const
{
	UWorld* World = GetWorld();
	return World ? World->GetGameState<AstudyGameState>() : nullptr;
}

void UStudyHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 关键：每进一个新关卡都先把输入模式复位。
	// SetInputMode / SetIgnoreMoveInput / SetIgnoreLookInput 是挂在 PlayerController 上的，
	// 切关卡不会自动清掉 —— 上次对话开着时传送到矿洞，就会把 GameAndUI + 锁视角带过去，
	// 表现为"鼠标一直露着、必须按住左键才能转视角"。
	SetDialogInputMode(false);
	SetLoadingState(false);		// 万一是从"加载中被切走"的场景回来，先把输入还回去
	bWasDialogOpen = false;
	bWasLoading = false;
}

void UStudyHUDWidget::NativeDestruct()
{
	// Widget 被销毁（关卡切换 / 结束播放）时也要把输入还回去
	SetDialogInputMode(false);
	SetLoadingState(false);
	Super::NativeDestruct();
}

void UStudyHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Refresh();
}

void UStudyHUDWidget::Refresh()
{
	AstudyCharacter* Character = GetStudyCharacter();
	AstudyGameState* GS = GetStudyState();
	if (!Character)
	{
		return;
	}

	// ---- 时间 ----
	if (GS && ClockText)
	{
		const int32 Total = FMath::FloorToInt(GS->GetClockMinutes());
		ClockText->SetText(FText::FromString(
			FString::Printf(TEXT("第 %d 天    %02d:%02d"), GS->GetDayIndex(), Total / 60, Total % 60)));

		if (GS->GetMineStopClock() > 0.f)
		{
			const int32 Stop = FMath::FloorToInt(GS->GetMineStopClock());
			MineTimeText->SetText(FText::FromString(FString::Printf(TEXT("开采至   %02d:%02d"), Stop / 60, Stop % 60)));
			MineTimeText->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			MineTimeText->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// ---- 背包 ----
	const float Weight = Character->GetCarriedWeightKg();
	const float MaxWeight = Character->GetMaxCarryKg();
	const float Ratio = Character->GetLoadRatio();

	if (WeightText)
	{
		WeightText->SetText(FText::FromString(FString::Printf(TEXT("%.1f / %.1f kg"), Weight, MaxWeight)));
	}
	if (PctText)
	{
		PctText->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), Ratio * 100.f)));
	}

	// 载重颜色：绿 → 黄 → 红
	const FLinearColor LoadColor = (Ratio < 0.6f)
		? FLinearColor(0.4f, 0.9f, 0.5f, 1.f)
		: (Ratio < 0.9f ? FLinearColor(1.f, 0.8f, 0.25f, 1.f) : FLinearColor(1.f, 0.35f, 0.3f, 1.f));

	if (LoadBar)
	{
		LoadBar->SetPercent(FMath::Clamp(Ratio, 0.f, 1.f));
		LoadBar->SetFillColorAndOpacity(LoadColor);
	}
	if (PctText)
	{
		PctText->SetColorAndOpacity(FSlateColor(LoadColor));
	}
	if (WeightText)
	{
		WeightText->SetColorAndOpacity(FSlateColor(LoadColor));
	}

	// ---- 仓库黄金 / 现金 ----
	// 金重（背包里背着的）不显示：矿洞里只该看到背包重量在变。
	// 交到暂存点/卸货区之后才累加进"仓库黄金"，回营地时统一结算。
	int32 Cash = 0;
	float StoredGold = 0.f;
	if (UstudyGameInstance* GI = Cast<UstudyGameInstance>(UGameplayStatics::GetGameInstance(this)))
	{
		Cash = GI->Cash;
		StoredGold = GI->GoldGrams;
	}

	if (GoldText)
	{
		GoldText->SetText(FText::FromString(FString::Printf(TEXT("%.1f g"), StoredGold)));
	}
	if (CashText)
	{
		CashText->SetText(FText::FromString(FText::AsNumber(Cash).ToString()));
	}

	// ---- 准星提示（对准的掉落物 / 附近商人） ----
	if (HintText)
	{
		FString Hint;
		if (Character->IsFocusingLadder())
		{
			Hint = TEXT("按  E  爬梯");
		}
		else if (const ADropItem* Drop = Character->GetFocusedDrop())
		{
			// 档位词 + 重量：给「要不要捡」的依据，但不报精确百分比（保住「挖出来才知道」）
			Hint = FString::Printf(TEXT("按  E  拾取      %.1f kg   %s"),
				Drop->WeightKg, *ADropItem::GoldGradeWord(Drop->GoldContent));
		}
		if (AMerchant* M = Character->GetNearbyMerchant())
		{
			// 床用专门的提示语
			if (M->Kind == EMerchantKind::Bed)
			{
				Hint = TEXT("按  E  睡觉");
			}
			else
			{
				Hint = FString::Printf(TEXT("按  E  和「%s」说话"), *M->DisplayName);
			}
		}
		HintText->SetText(FText::FromString(Hint));
		HintText->SetVisibility(Hint.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	// ---- 一次性提示 ----
	if (NoticeText && GS)
	{
		FString Msg;
		if (!GS->GetNoticeText().IsEmpty())
		{
			Msg = GS->GetNoticeText();
		}
		else if (GS->GetNoticeGoldGrams() > 0.f)
		{
			Msg = FString::Printf(TEXT("本趟带回   %.1f g   黄金"), GS->GetNoticeGoldGrams());
		}

		const bool bShow = (!Msg.IsEmpty() && GS->GetNoticeTimeLeft() > 0.f);

		if (bShow)
		{
			NoticeText->SetText(FText::FromString(Msg));
			NoticeText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.84f, 0.30f,
				FMath::Clamp(GS->GetNoticeTimeLeft() / 1.5f, 0.f, 1.f))));
		}

		NoticeText->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	// ---- 结算面板 ----
	if (SettlementBorder && GS)
	{
		const bool bFinished = (GS->GetPhase() == EGamePhase::Finished);
		SettlementBorder->SetVisibility(bFinished ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

		if (bFinished)
		{
			const int32 Total = GS->GetFinalCash();
			const int32 Unsold = GS->GetFinalUnsoldCash();
			const int32 Sold = Total - Unsold;

			SettlementCashText->SetText(FText::FromString(FText::AsNumber(Total).ToString()));
			SettlementInfoText->SetText(FText::FromString(FString::Printf(
				TEXT("卖金所得 %d  +  未卖黄金折算 %d\n卸货 %d 趟"),
				Sold, Unsold, GS->GetUnloadCount())));
		}
	}

	// ---- 加载遮罩 ----
	// 两种来源：
	//   ① 主角身上的黑屏过渡（睡觉 / 传送回营地，1.5 秒）
	//   ② 矿洞生成器还没铺完石头
	// 两种期间都把玩家输入锁掉。
	{
		bool bLoading = false;
		bool bShowBar = false;

		if (Character->IsTransitioning())
		{
			bLoading = true;
			if (LoadingText)
			{
				LoadingText->SetText(FText::FromString(Character->GetTransitionText()));
			}
		}
		else
		{
			if (!CachedMineGen.IsValid())
			{
				if (UWorld* World = GetWorld())
				{
					for (TActorIterator<AMineGenerator> It(World); It; ++It)
					{
						CachedMineGen = *It;
						break;
					}
				}
			}

			if (CachedMineGen.IsValid() && !CachedMineGen->IsBuildFinished())
			{
				bLoading = true;
				bShowBar = true;

				if (LoadingText)
				{
					LoadingText->SetText(FText::FromString(TEXT("正在开挖矿洞……")));
				}
				if (LoadingBar)
				{
					LoadingBar->SetPercent(CachedMineGen->GetBuildProgress());
				}
			}
		}

		if (LoadingBar)
		{
			LoadingBar->SetVisibility(bShowBar ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}

		if (bLoading != bWasLoading)
		{
			SetLoadingState(bLoading);
			bWasLoading = bLoading;
		}
	}

	// ---- 对话框 ----
	const bool bDialogOpen = Character->IsDialogOpen();

	if (bDialogOpen != bWasDialogOpen)
	{
		SetDialogInputMode(bDialogOpen);
		bWasDialogOpen = bDialogOpen;
	}

	if (DialogBorder)
	{
		DialogBorder->SetVisibility(bDialogOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

		if (bDialogOpen)
		{
			DialogTitleText->SetText(FText::FromString(Character->GetDialogTitle()));
			DialogBodyText->SetText(FText::FromString(Character->GetDialogBody()));

			const TArray<FString>& Options = Character->GetDialogOptions();
			for (int32 i = 0; i < OptionButtons.Num(); ++i)
			{
				UButton* Btn = OptionButtons[i];
				if (!Btn)
				{
					continue;
				}

				const bool bValid = Options.IsValidIndex(i);
				Btn->SetVisibility(bValid ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

				if (bValid)
				{
					if (UTextBlock* Label = Cast<UTextBlock>(Btn->GetChildAt(0)))
					{
						Label->SetText(FText::FromString(Options[i]));
					}
				}
			}

			// 行内数字输入：只在商人允许"自己填克数"时出现
			if (NumberRow)
			{
				const bool bShowNumber = (Character->GetDialogGoldHeld() > 0.f);
				NumberRow->SetVisibility(bShowNumber ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			}
		}
	}
}

// ---------------- 交互 ----------------

void UStudyHUDWidget::ChooseOption(int32 Index)
{
	// 对话里所有可点的东西（选项 / 确认卖出）都走这个音效
	UGameplayStatics::PlaySound2D(this, StudySounds::Click(), 0.8f);

	if (AstudyCharacter* Character = GetStudyCharacter())
	{
		Character->ChooseDialogOption(Index);
	}
}

void UStudyHUDWidget::OnNumberSubmit()
{
	UGameplayStatics::PlaySound2D(this, StudySounds::Click(), 0.8f);

	AstudyCharacter* Character = GetStudyCharacter();
	if (!Character || !NumberBox)
	{
		return;
	}

	const float Value = FCString::Atof(*NumberBox->GetText().ToString());
	Character->DialogSubmitNumber(Value);
}

void UStudyHUDWidget::OnDialogClose()
{
	if (AstudyCharacter* Character = GetStudyCharacter())
	{
		Character->CloseDialog();
	}
}

void UStudyHUDWidget::SetLoadingState(bool bLoading)
{
	if (LoadingBorder)
	{
		LoadingBorder->SetVisibility(bLoading ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	// 加载期间玩家既不能动、也不能挖/捡/交互
	APawn* Pawn = PC->GetPawn();

	if (bLoading)
	{
		PC->DisableInput(PC);
		if (Pawn)
		{
			Pawn->DisableInput(PC);
		}
	}
	else
	{
		PC->EnableInput(PC);
		if (Pawn)
		{
			Pawn->EnableInput(PC);
		}
	}
}

void UStudyHUDWidget::SetDialogInputMode(bool bDialogOpen)
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	if (bDialogOpen)
	{
		PC->SetShowMouseCursor(true);

		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);

		// 弹窗期间不许移动 / 转视角
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
	}
	else
	{
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetIgnoreMoveInput(false);
		PC->SetIgnoreLookInput(false);
	}
}
