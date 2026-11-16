// Fill out your copyright notice in the Description page of Project Settings.

#include "StudyRankWidget.h"
#include "studyCharacter.h"
#include "studyGameInstance.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

namespace
{
	FSlateFontInfo RankUIFont(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	UTextBlock* RankUIText(UWidgetTree* Tree, const FString& Str, int32 Size,
		const FLinearColor& Color, bool bBold = false)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		T->SetText(FText::FromString(Str));
		T->SetFont(RankUIFont(Size, bBold));
		T->SetColorAndOpacity(FSlateColor(Color));
		return T;
	}

	void RankUIRow(UVerticalBox* Box, UWidget* W, float PadY = 6.f)
	{
		if (UVerticalBoxSlot* Slot = Box->AddChildToVerticalBox(W))
		{
			Slot->SetPadding(FMargin(0.f, PadY));
			Slot->SetHorizontalAlignment(HAlign_Left);
		}
	}
}

TSharedRef<SWidget> UStudyRankWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildLayout();
	}
	return Super::RebuildWidget();
}

void UStudyRankWidget::BuildLayout()
{
	const FLinearColor Gold(0.95f, 0.78f, 0.28f, 1.f);
	const FLinearColor White(0.92f, 0.92f, 0.95f, 1.f);
	const FLinearColor Dim(0.65f, 0.65f, 0.70f, 1.f);

	// 根：画布（全透明），中间放一块半透明面板
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	WidgetTree->RootWidget = Canvas;

	PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	PanelBorder->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.78f));
	PanelBorder->SetPadding(FMargin(36.f, 26.f));

	RowBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	PanelBorder->SetContent(RowBox);

	if (UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(PanelBorder))
	{
		CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CanvasSlot->SetAutoSize(true);
		CanvasSlot->SetPosition(FVector2D::ZeroVector);
	}

	RankUIRow(RowBox, RankUIText(WidgetTree, TEXT("资 产 排 行"), 34, Gold, true), 2.f);
	RankUIRow(RowBox, RankUIText(WidgetTree, TEXT("按 Tab 关闭"), 18, Dim), 0.f);

	// 四行名次（文字在 Refresh 里填）
	for (int32 i = 0; i < 4; ++i)
	{
		UTextBlock* Line = RankUIText(WidgetTree, TEXT(""), 28, White);
		RowTexts.Add(Line);
		RankUIRow(RowBox, Line, 4.f);
	}

	// 底部：黑市行情
	MarketText = RankUIText(WidgetTree, TEXT(""), 20, Dim);
	RankUIRow(RowBox, MarketText, 10.f);

	SetVisibility(ESlateVisibility::Collapsed);
}

void UStudyRankWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bShown)
	{
		return;
	}

	RefreshAccum += InDeltaTime;
	if (RefreshAccum >= 0.3f)
	{
		RefreshAccum = 0.f;
		Refresh();
	}
}

void UStudyRankWidget::Toggle()
{
	if (bShown)
	{
		Hide();
	}
	else
	{
		Show();
	}
}

void UStudyRankWidget::Show()
{
	bShown = true;
	RefreshAccum = 0.f;
	SetVisibility(ESlateVisibility::HitTestInvisible);
	Refresh();
}

void UStudyRankWidget::Hide()
{
	bShown = false;
	SetVisibility(ESlateVisibility::Collapsed);
}

void UStudyRankWidget::Refresh()
{
	UstudyGameInstance* GI = GetGameInstance<UstudyGameInstance>();
	if (!GI)
	{
		return;
	}

	struct FEntry
	{
		FString Name;
		int32 Asset = 0;
		bool bMe = false;
	};

	TArray<FEntry> List;

	// 自己和对手走同一个口径（GetMyAssetTotal / GetRivalAssetTotal）：
	//   现金 + 仓库/暂存/身上背着的金 + 工艺品账面价（没鉴定过按黑商报的含金量）
	// 这样 Tab 和每日播报永远是同一个数，不会再对不上。
	FEntry Me;
	Me.Name = TEXT("你");
	Me.Asset = GI->GetMyAssetTotal();
	Me.bMe = true;
	List.Add(Me);

	for (int32 i = 0; i < GI->Rivals.Num(); ++i)
	{
		FEntry E;
		E.Name = GI->Rivals[i].Name;
		E.Asset = GI->GetRivalAssetTotal(i);
		List.Add(E);
	}

	// 资产从高到低
	List.Sort([](const FEntry& A, const FEntry& B) { return A.Asset > B.Asset; });

	const FLinearColor Gold(0.95f, 0.78f, 0.28f, 1.f);
	const FLinearColor White(0.92f, 0.92f, 0.95f, 1.f);

	for (int32 i = 0; i < RowTexts.Num(); ++i)
	{
		if (!RowTexts[i])
		{
			continue;
		}

		if (List.IsValidIndex(i))
		{
			const FEntry& E = List[i];

			if (E.bMe)
			{
				// 自己那一栏给精确值
				RowTexts[i]->SetText(FText::FromString(
					FString::Printf(TEXT("%d.  %-6s  %d"), i + 1, *E.Name, E.Asset)));
			}
			else
			{
				// 别人的资产只给一个模糊数（PRD §2.2：名次精确、金额模糊）
				if (E.Asset >= 10000)
				{
					RowTexts[i]->SetText(FText::FromString(FString::Printf(
						TEXT("%d.  %-6s  约 %.1f 万"),
						i + 1, *E.Name, E.Asset / 10000.f)));
				}
				else
				{
					RowTexts[i]->SetText(FText::FromString(FString::Printf(
						TEXT("%d.  %-6s  约 %d"), i + 1, *E.Name, E.Asset)));
				}
			}

			RowTexts[i]->SetColorAndOpacity(FSlateColor(E.bMe ? Gold : White));
		}
		else
		{
			RowTexts[i]->SetText(FText::GetEmpty());
		}
	}

	if (MarketText)
	{
		// 和黑商对话框里那行必须一模一样：显示的必须是"实际到手单价"（含硬地板）。
		// 以前显示的是"正规价的百分之多少"，加了地板之后会和真实报价对不上。
		MarketText->SetText(FText::FromString(FString::Printf(
			TEXT("黑市价：%d 币/g（今日基础 %.0f%%，有人抛售还会被砸，地板 %d）"),
			FMath::RoundToInt(GI->GetFencePricePerGram()),
			GI->BlackMarketDailyRate * 100.f,
			FMath::RoundToInt(GI->GetFenceFloorPrice()))));
	}
}
