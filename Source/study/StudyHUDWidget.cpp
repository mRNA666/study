// Fill out your copyright notice in the Description page of Project Settings.


#include "StudyHUDWidget.h"
#include "studyCharacter.h"
#include "studyGameState.h"
#include "studyGameInstance.h"
#include "Merchant.h"
#include "MineGenerator.h"
#include "DropItem.h"
#include "BagHUD.h"
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
#include "Components/ScrollBox.h"
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
	// 准星提示：必须是纯白 + 黑色阴影。
	// 原来用淡黄 (1, 0.9, 0.5)：和头灯照亮的黄褐色岩石**亮度几乎一样**，
	// 两者都"很亮"，人眼只能靠色相区分，一晃眼就看不见了。
	// 白字靠的是"高亮 + 周围一圈黑边"，在任何背景上都能读。
	HintText = MakeText(WidgetTree, TEXT(""), 20, FLinearColor(1.f, 1.f, 1.f, 1.f), true);
	HintText->SetShadowOffset(FVector2D(2.f, 2.f));
	HintText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.95f));
	HintText->SetJustification(ETextJustify::Center);
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

		// 正文的可滚动版（每日播报那种长文本）：SizeBox 卡最高高度 → ScrollBox → 文本。
		// 只有 Character->IsDialogBodyScrollable() 为真时显示，平时隐藏。
		BodyScrollSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		BodyScrollSize->SetMaxDesiredHeight(520.f);

		DialogBodyScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
		DialogBodyScroll->SetScrollBarVisibility(ESlateVisibility::Visible);

		DialogBodyScrollText = MakeText(WidgetTree, TEXT(""), 18, FLinearColor(0.80f, 0.82f, 0.88f, 1.f));
		DialogBodyScroll->AddChild(DialogBodyScrollText);

		BodyScrollSize->SetContent(DialogBodyScroll);
		BodyScrollSize->SetVisibility(ESlateVisibility::Collapsed);
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->AddChildToVerticalBox(BodyScrollSize)))
		{
			S->SetPadding(FMargin(0.f, 8.f, 0.f, 14.f));
		}

		// 选项按钮（最多 6 个，多余的隐藏）
		// 为什么是 6：UButton::OnClicked 是动态多播委托，只能绑 UFUNCTION ——
		// 每个按钮必须配一个独立的 OnOptionN，没法在循环里绑带下标的 lambda。
		// 6 = 5 件货 + 1 个「算了/离开」；货再多就翻页（↑↓ / 滚轮）。
		for (int32 i = 0; i < 6; ++i)
		{
			UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
			UTextBlock* Label = MakeText(WidgetTree, TEXT(""), 20, FLinearColor(0.06f, 0.06f, 0.06f, 1.f), true);
			Btn->AddChild(Label);

			switch (i)
			{
			case 0: Btn->OnClicked.AddDynamic(this, &UStudyHUDWidget::OnOption1); break;
			case 1: Btn->OnClicked.AddDynamic(this, &UStudyHUDWidget::OnOption2); break;
			case 2: Btn->OnClicked.AddDynamic(this, &UStudyHUDWidget::OnOption3); break;
			case 3: Btn->OnClicked.AddDynamic(this, &UStudyHUDWidget::OnOption4); break;
			case 4: Btn->OnClicked.AddDynamic(this, &UStudyHUDWidget::OnOption5); break;
			default: Btn->OnClicked.AddDynamic(this, &UStudyHUDWidget::OnOption6); break;
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

		NumberLabelText = MakeText(WidgetTree, TEXT("卖"), 20, FLinearColor(0.85f, 0.87f, 0.92f, 1.f), true);
		NumberRow->AddChildToHorizontalBox(NumberLabelText);

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

		NumberUnitText = MakeText(WidgetTree, TEXT("g"), 20, FLinearColor(0.85f, 0.87f, 0.92f, 1.f), true);
		NumberRow->AddChildToHorizontalBox(NumberUnitText);

		NumberSubmitButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		NumberSubmitButton->AddChild(
			MakeText(WidgetTree, TEXT("确 认"), 18, FLinearColor(0.06f, 0.06f, 0.06f, 1.f), true));
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

		// 第二个行内输入：掺黄铜那页要同时填"卖多少 g"和"掺多少 g"
		NumberRow2 = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		NumberRow2->AddChildToHorizontalBox(
			MakeText(WidgetTree, TEXT("掺"), 20, FLinearColor(0.85f, 0.87f, 0.92f, 1.f), true));

		NumberBox2 = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		NumberBox2->SetHintText(FText::FromString(TEXT("黄铜克数")));
		{
			USizeBox* BrassBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			BrassBox->SetWidthOverride(130.f);
			BrassBox->AddChild(NumberBox2);

			if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(NumberRow2->AddChildToHorizontalBox(BrassBox)))
			{
				S->SetPadding(FMargin(8.f, 0.f, 8.f, 0.f));
			}
		}

		NumberRow2->AddChildToHorizontalBox(
			MakeText(WidgetTree, TEXT("g"), 20, FLinearColor(0.85f, 0.87f, 0.92f, 1.f), true));

		NumberSubmitButton2 = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		NumberSubmitButton2->AddChild(
			MakeText(WidgetTree, TEXT("确认掺入"), 18, FLinearColor(0.06f, 0.06f, 0.06f, 1.f), true));
		NumberSubmitButton2->OnClicked.AddDynamic(this, &UStudyHUDWidget::OnNumberSubmit2);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(NumberRow2->AddChildToHorizontalBox(NumberSubmitButton2)))
		{
			S->SetPadding(FMargin(14.f, 0.f, 0.f, 0.f));
		}

		NumberRow2->SetVisibility(ESlateVisibility::Collapsed);
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->AddChildToVerticalBox(NumberRow2)))
		{
			S->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		}

		// 输入框要一眼看得见：默认样式的灰底灰字在这个面板上几乎消失。
		// 改成金色底 + 近黑字 + 更大字号（背景色是 FEditableTextBoxStyle 里的 BackgroundColor）。
		auto StyleUpTextBox = [](UEditableTextBox* InputBox)
		{
			if (!InputBox)
			{
				return;
			}

			FEditableTextBoxStyle Style = InputBox->WidgetStyle;
			Style.BackgroundColor = FLinearColor(0.96f, 0.80f, 0.32f, 1.f);
			Style.ForegroundColor = FLinearColor(0.06f, 0.06f, 0.06f, 1.f);
			Style.FocusedForegroundColor = FLinearColor(0.02f, 0.02f, 0.02f, 1.f);
			Style.ReadOnlyForegroundColor = FLinearColor(0.12f, 0.12f, 0.12f, 1.f);
			Style.TextStyle.ColorAndOpacity = FSlateColor(FLinearColor(0.06f, 0.06f, 0.06f, 1.f));
			Style.TextStyle.Font = Font(22, true);
			Style.Padding = FMargin(12.f, 6.f);
			InputBox->WidgetStyle = Style;

			InputBox->SetForegroundColor(FLinearColor(0.06f, 0.06f, 0.06f, 1.f));
			InputBox->SetJustification(ETextJustify::Center);
		};

		StyleUpTextBox(NumberBox);
		StyleUpTextBox(NumberBox2);

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

	// 关键：每进一个新关卡都先把输入状态复位。
	// SetInputMode / SetIgnoreMoveInput / SetIgnoreLookInput 挂在 PlayerController 上，
	// 切关卡不会自动清掉 —— 上次对话/面板开着时切走，就会把 GameAndUI + 锁移动带过去。
	bLastInputModal = false;
	bWasDialogOpen = false;
	bWasLoading = false;
	SetLoadingState(false);		// 万一是从"加载中被切走"的场景回来，先把遮罩收掉
	SyncInputMode(false);		// 输入锁按当前状态重算一遍（绝对量，不会累积）
}

void UStudyHUDWidget::NativeDestruct()
{
	// Widget 被销毁（关卡切换 / 结束播放）时也要把输入还回去
	SetLoadingState(false);
	SyncInputMode(false);
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
			FString::Printf(TEXT("第 %d/%d 天    %02d:%02d"),
				GS->GetDayIndex(), UstudyGameInstance::TotalDays, Total / 60, Total % 60)));

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
			// 只报档位词，不报重量也不报精确百分比：
			// 报重量会让"捡不捡"退化成算数，报百分比会毁掉"挖出来才知道"的赌感。
			Hint = FString::Printf(TEXT("按  E  拾取      %s"), *ADropItem::GoldGradeWord(Drop->GoldContent));
		}
		if (AMerchant* M = Character->GetNearbyMerchant())
		{
			// 床 / 轮盘用专门的提示语
			if (M->Kind == EMerchantKind::Bed)
			{
				Hint = TEXT("按  E  睡觉");
			}
			else if (M->Kind == EMerchantKind::Roulette)
			{
				Hint = TEXT("按  E  玩一把轮盘");
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
			const int32 LoanDebt = GS->GetFinalLoanDebt();

			SettlementCashText->SetText(FText::FromString(FText::AsNumber(Total).ToString()));

			FString Info = FString::Printf(
				TEXT("卖金所得 %d  +  未卖黄金折算 %d\n卸货 %d 趟"),
				Sold, Unsold, GS->GetUnloadCount());

			if (LoanDebt > 0)
			{
				Info += FString::Printf(TEXT("\n欠黑社会 %d 币（已从最终金额里扣掉）"), LoanDebt);
			}

			SettlementInfoText->SetText(FText::FromString(Info));
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

	// 数字输入行的单位：卖金是 g，卖工艺品是 %。
	// 这里必须每帧比对字符串 —— 因为"选项页 → 数字页"是在同一个对话里切换的
	// （没有关闭再打开），只靠"对话框刚打开"那一下判断，单位会一直是上一次的 g。
	if (NumberUnitText)
	{
		const FString Unit = Character->GetDialogNumberUnit();
		if (!Unit.Equals(LastNumberUnit))
		{
			LastNumberUnit = Unit;
			NumberUnitText->SetText(FText::FromString(Unit));
		}

		// 行首字和输入框灰字也跟着换：卖金是「卖 / 克数」，工艺品报价是「卖 / 含量」。
		// 不换的话报含金量那一页会写成"卖 克数 %"，玩家会以为要填克数。
		const FString Label = Character->GetDialogNumberLabel();
		if (NumberLabelText && !Label.Equals(LastNumberLabel))
		{
			LastNumberLabel = Label;
			NumberLabelText->SetText(FText::FromString(Label));
		}

		const FString Hint = Character->GetDialogNumberHint();
		if (NumberBox && !Hint.Equals(LastNumberHint))
		{
			LastNumberHint = Hint;
			NumberBox->SetHintText(FText::FromString(Hint));
		}
	}

	bWasDialogOpen = bDialogOpen;

	// 输入模式/输入锁的切换统一由 SyncInputMode 每帧算一遍，这里不再单独处理

	if (DialogBorder)
	{
		DialogBorder->SetVisibility(bDialogOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

		if (bDialogOpen)
		{
			DialogTitleText->SetText(FText::FromString(Character->GetDialogTitle()));

			// 正文：短的直接摆；长的（每日播报）换成可滚动的那一版
			const bool bScrollBody = Character->IsDialogBodyScrollable();
			const FString& Body = Character->GetDialogBody();

			if (DialogBodyText)
			{
				DialogBodyText->SetVisibility(bScrollBody
					? ESlateVisibility::Collapsed
					: ESlateVisibility::SelfHitTestInvisible);

				if (!bScrollBody)
				{
					DialogBodyText->SetText(FText::FromString(Body));
				}
			}

			if (BodyScrollSize)
			{
				BodyScrollSize->SetVisibility(bScrollBody
					? ESlateVisibility::Visible
					: ESlateVisibility::Collapsed);
			}

			if (bScrollBody && DialogBodyScrollText)
			{
				// 换了一份新内容才重写 + 回到顶部（否则别打断玩家正在滚的位置）
				if (!Body.Equals(LastScrollBody))
				{
					LastScrollBody = Body;
					DialogBodyScrollText->SetText(FText::FromString(Body));

					if (DialogBodyScroll)
					{
						DialogBodyScroll->SetScrollOffset(0.f);
					}
				}
			}
			else
			{
				LastScrollBody.Reset();
			}

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

				// 由隐到显 = 换了一页：清空上一次的字，让商人重新预填
				if (bShowNumber && !bNumberRowVisible)
				{
					if (NumberBox)
					{
						NumberBox->SetText(FText::GetEmpty());
					}
					AppliedPrefill1 = -1;
				}
				bNumberRowVisible = bShowNumber;

				// 商人要求预填（例如填超了，改成最大值）
				if (NumberBox && bShowNumber)
				{
					const int32 Ver = Character->GetDialogNumberPrefillVersion(false);
					if (Ver != AppliedPrefill1)
					{
						AppliedPrefill1 = Ver;
						NumberBox->SetText(FText::FromString(Character->GetDialogNumberPrefill(false)));
					}
				}
			}

			// 第二行（掺黄铜）
			if (NumberRow2)
			{
				const bool bShowNumber2 = (Character->GetDialogGoldHeld2() > 0.f);
				NumberRow2->SetVisibility(bShowNumber2 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

				if (bShowNumber2 && !bNumberRow2Visible)
				{
					if (NumberBox2)
					{
						NumberBox2->SetText(FText::GetEmpty());
					}
					AppliedPrefill2 = -1;
				}
				bNumberRow2Visible = bShowNumber2;

				if (NumberBox2 && bShowNumber2)
				{
					const int32 Ver2 = Character->GetDialogNumberPrefillVersion(true);
					if (Ver2 != AppliedPrefill2)
					{
						AppliedPrefill2 = Ver2;
						NumberBox2->SetText(FText::FromString(Character->GetDialogNumberPrefill(true)));
					}
				}
			}
		}
	}

	// 最后一步：按"现在到底开着什么界面"把输入状态重算一遍（绝对量写回，永远自愈）
	SyncInputMode(bWasLoading);
}

// ---------------- 交互 ----------------

void UStudyHUDWidget::ScrollBodyBy(float Pixels)
{
	if (!DialogBodyScroll || FMath::IsNearlyZero(Pixels))
	{
		return;
	}

	const float End = FMath::Max(0.f, DialogBodyScroll->GetScrollOffsetOfEnd());
	DialogBodyScroll->SetScrollOffset(
		FMath::Clamp(DialogBodyScroll->GetScrollOffset() + Pixels, 0.f, End));
}

FReply UStudyHUDWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// 鼠标停在对话框上时滚轮会被 UMG 先吃掉（到不了视口的 MouseWheelAxis），
	// 所以这里再接一份：对话开着就把滚轮当翻页 / 滚正文。
	AstudyCharacter* Character = GetStudyCharacter();
	const float Delta = InMouseEvent.GetWheelDelta();
	if (Character && Character->IsDialogOpen() && FMath::Abs(Delta) > 0.01f)
	{
		// ① 长正文（每日播报）：滚正文本身
		if (Character->IsDialogBodyScrollable())
		{
			ScrollBodyBy(Delta > 0.f ? -48.f : 48.f);
			return FReply::Handled();
		}

		// ② 列表（卖工艺品 / 听泉鉴宝）：翻页。往上滚 = 看前面的
		Character->ScrollDialogList(Delta > 0.f ? -1 : 1);
		return FReply::Handled();
	}

	return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

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

void UStudyHUDWidget::OnNumberSubmit2()
{
	UGameplayStatics::PlaySound2D(this, StudySounds::Click(), 0.8f);

	AstudyCharacter* Character = GetStudyCharacter();
	if (!Character || !NumberBox2)
	{
		return;
	}

	const float Value = FCString::Atof(*NumberBox2->GetText().ToString());
	Character->DialogSubmitNumber2(Value);
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

	// 加载期间"不许动/不许挖不许捡"现在由 SyncInputMode 每帧算（Reset + SetIgnore* / 动作屏蔽），
	// 不再用 DisableInput / EnableInput —— 那两个是往输入组件"栈"里 Push/Pop，
	// 和其它地方交叉调用会把栈搞乱，典型症状就是界面关了却永远收不到输入。
}

void UStudyHUDWidget::SyncInputMode(bool bLoading)
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	AstudyCharacter* Character = GetStudyCharacter();
	AstudyGameState* GS = GetStudyState();

	// 会拦住玩家的六种情况：对话 / 货架 / 工艺品背包 / 暂停菜单 / 加载 / 对局结算
	bool bPanelOpen = false;
	bool bMenuOpen = false;
	if (ABagHUD* BagHud = Cast<ABagHUD>(PC->GetHUD()))
	{
		bPanelOpen = BagHud->IsHandicraftPanelOpen();
		bMenuOpen = BagHud->IsMenuOpen();
	}

	const bool bDialogOpen = (Character && Character->IsDialogOpen());
	const bool bMatchOver = (GS && GS->GetPhase() == EGamePhase::Finished);

	// 面板/对话/菜单：要鼠标、锁移动；加载/结算：只锁移动，不弹鼠标（黑屏和结算面板不需要点）
	const bool bBlockActions = bLoading || bMatchOver || bPanelOpen || bMenuOpen;
	const bool bLockMove = bBlockActions || bDialogOpen;
	const bool bShowCursor = bDialogOpen || bPanelOpen || bMenuOpen;

	// ---- ① 移动 / 视角：每帧先清零，再按当前状态加锁 ----
	// 绝对量写回，所以任何一方漏了配对都不会把玩家永久锁死。
	PC->ResetIgnoreMoveInput();
	PC->ResetIgnoreLookInput();
	if (bLockMove)
	{
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
	}

	// 只在"锁"翻转的那一帧打日志：以后谁再把这个锁搞住，日志里一眼能看到是哪个界面干的
	if (bLockMove != bLastLockMove)
	{
		bLastLockMove = bLockMove;
		UE_LOG(LogTemp, Log,
			TEXT("[Input] 移动锁 %s   （货架/背包=%d 菜单=%d 对话=%d 加载=%d 结算=%d）"),
			bLockMove ? TEXT("开") : TEXT("关"),
			bPanelOpen ? 1 : 0, bMenuOpen ? 1 : 0, bDialogOpen ? 1 : 0,
			bLoading ? 1 : 0, bMatchOver ? 1 : 0);
	}

	// ---- ② 鼠标 / 输入模式：只在状态翻转的那一帧改，避免每帧 SetInputMode 打掉控件焦点 ----
	if (bShowCursor != bLastInputModal)
	{
		PC->SetShowMouseCursor(bShowCursor);

		if (bShowCursor)
		{
			FInputModeGameAndUI Mode;
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Mode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(Mode);
		}
		else
		{
			PC->SetInputMode(FInputModeGameOnly());
		}

		bLastInputModal = bShowCursor;
	}

	// ---- ③ 动作级屏蔽：E / 跳跃 / 挖掘（这三条不吃 IgnoreMoveInput，只能在角色里挡）----
	if (Character)
	{
		Character->SetGameplayInputBlocked(bBlockActions);
	}
}
