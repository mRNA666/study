// Fill out your copyright notice in the Description page of Project Settings.

#include "StudyBagWidget.h"
#include "studyGameInstance.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

namespace
{
	FSlateFontInfo BagFont(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	UTextBlock* BagText(UWidgetTree* Tree, const FString& Str, int32 Size,
		const FLinearColor& Color, bool bBold = false)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		T->SetText(FText::FromString(Str));
		T->SetFont(BagFont(Size, bBold));
		T->SetColorAndOpacity(FSlateColor(Color));
		return T;
	}
}

TSharedRef<SWidget> UStudyBagWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildLayout();
	}
	return Super::RebuildWidget();
}

void UStudyBagWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (!bOpen)
	{
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UStudyBagWidget::BuildLayout()
{
	const FLinearColor Gold(0.95f, 0.78f, 0.28f, 1.f);
	const FLinearColor White(0.92f, 0.92f, 0.95f, 1.f);
	const FLinearColor Dim(0.62f, 0.62f, 0.68f, 1.f);

	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	WidgetTree->RootWidget = Canvas;

	PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	PanelBorder->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.86f));
	PanelBorder->SetPadding(FMargin(30.f, 22.f));

	if (UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(PanelBorder))
	{
		CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CanvasSlot->SetAutoSize(true);
		CanvasSlot->SetPosition(FVector2D::ZeroVector);
	}

	ListBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	PanelBorder->SetContent(ListBox);

	if (UVerticalBoxSlot* S = ListBox->AddChildToVerticalBox(BagText(WidgetTree, TEXT("工艺品背包"), 32, Gold, true)))
	{
		S->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		S->SetHorizontalAlignment(HAlign_Left);
	}
	if (UVerticalBoxSlot* S = ListBox->AddChildToVerticalBox(BagText(WidgetTree, TEXT("按 B 关闭"), 18, Dim)))
	{
		S->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
		S->SetHorizontalAlignment(HAlign_Left);
	}

	for (int32 i = 0; i < 14; ++i)
	{
		UTextBlock* Line = BagText(WidgetTree, TEXT(""), 22, White);
		Lines.Add(Line);
		if (UVerticalBoxSlot* S = ListBox->AddChildToVerticalBox(Line))
		{
			S->SetPadding(FMargin(0.f, 3.f));
			S->SetHorizontalAlignment(HAlign_Left);
		}
	}

	SumText = BagText(WidgetTree, TEXT(""), 22, Dim, true);
	if (UVerticalBoxSlot* S = ListBox->AddChildToVerticalBox(SumText))
	{
		S->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));
		S->SetHorizontalAlignment(HAlign_Left);
	}

	SetVisibility(ESlateVisibility::Collapsed);
}

void UStudyBagWidget::Toggle()
{
	if (bOpen)
	{
		CloseBag();
	}
	else
	{
		OpenBag();
	}
}

void UStudyBagWidget::OpenBag()
{
	bOpen = true;
	SetVisibility(ESlateVisibility::Visible);
	Refresh();
	SetWidgetInputLocked(true);

	SetIsFocusable(true);
	SetKeyboardFocus();
}

void UStudyBagWidget::CloseBag()
{
	bOpen = false;
	SetVisibility(ESlateVisibility::Collapsed);
	SetWidgetInputLocked(false);
}

void UStudyBagWidget::SetWidgetInputLocked(bool bLocked)
{
	// 即时反馈用；权威是 UStudyHUDWidget::SyncInputMode（每帧 Reset + 按需 Set，绝对量）
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	if (bLocked)
	{
		PC->SetShowMouseCursor(true);
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
	}
	else
	{
		PC->SetIgnoreMoveInput(false);
		PC->SetIgnoreLookInput(false);
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
	}
}

FReply UStudyBagWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// 角色输入被禁了，所以 B 和 Esc 都得面板自己接
	if (bOpen && (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::B))
	{
		CloseBag();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UStudyBagWidget::Refresh()
{
	UstudyGameInstance* GI = GetGameInstance<UstudyGameInstance>();
	if (!GI)
	{
		return;
	}

	const int32 Count = GI->HandicraftBag.Num();
	int32 AppraisedCount = 0;
	double RealSum = 0.0;

	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		if (!Lines[i])
		{
			continue;
		}

		if (!GI->HandicraftBag.IsValidIndex(i))
		{
			Lines[i]->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}

		const FHandicraftItem& It = GI->HandicraftBag[i];
		Lines[i]->SetVisibility(ESlateVisibility::Visible);

		if (It.bAppraised)
		{
			++AppraisedCount;
			RealSum += It.NominalGrams * It.RealPurity * 1000.0;

			Lines[i]->SetText(FText::FromString(FString::Printf(
				TEXT("· %-6s  标称 %.0f g   真实 %.0f%%   ≈ %d 币"),
				*UstudyGameInstance::GetCategoryName(It.Category), It.NominalGrams,
				It.RealPurity * 100.f,
				FMath::RoundToInt(It.NominalGrams * It.RealPurity * 1000.f))));
			Lines[i]->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.92f, 0.95f, 1.f)));
		}
		else
		{
			Lines[i]->SetText(FText::FromString(FString::Printf(
				TEXT("· %-6s  标称 %.0f g   黑商报 %.0f%%   鉴定：—"),
				*UstudyGameInstance::GetCategoryName(It.Category), It.NominalGrams,
				It.DeclaredPurity * 100.f)));
			Lines[i]->SetColorAndOpacity(FSlateColor(FLinearColor(0.70f, 0.70f, 0.74f, 1.f)));
		}
	}

	if (SumText)
	{
		if (Count == 0)
		{
			SumText->SetText(FText::FromString(TEXT("空的。去黑市柜台上看看？")));
		}
		else
		{
			SumText->SetText(FText::FromString(FString::Printf(
				TEXT("共 %d 件（已鉴定 %d 件）   已鉴定部分估值：%d 币"),
				Count, AppraisedCount, FMath::RoundToInt(RealSum))));
		}
	}
}
