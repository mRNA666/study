// Fill out your copyright notice in the Description page of Project Settings.

#include "StudyShelfWidget.h"
#include "studyGameInstance.h"
#include "Merchant.h"
#include "StudySounds.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "InputCoreTypes.h"

namespace
{
	FSlateFontInfo ShelfFont(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	UTextBlock* ShelfText(UWidgetTree* Tree, const FString& Str, int32 Size,
		const FLinearColor& Color, bool bBold = false)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		T->SetText(FText::FromString(Str));
		T->SetFont(ShelfFont(Size, bBold));
		T->SetColorAndOpacity(FSlateColor(Color));
		return T;
	}
}

TSharedRef<SWidget> UStudyShelfWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildLayout();
	}
	return Super::RebuildWidget();
}

void UStudyShelfWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (!bOpen)
	{
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UStudyShelfWidget::BuildLayout()
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

	// 左：8 行货架 / 详情；右：抢货信息
	UHorizontalBox* Root = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	PanelBorder->SetContent(Root);

	// ---------------- 左列 ----------------
	ListBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	UTextBlock* Title = ShelfText(WidgetTree, TEXT("柜台上的货"), 32, Gold, true);
	if (UVerticalBoxSlot* S = ListBox->AddChildToVerticalBox(Title))
	{
		S->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		S->SetHorizontalAlignment(HAlign_Left);
	}

	for (int32 i = 0; i < 8; ++i)
	{
		UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		UTextBlock* Txt = ShelfText(WidgetTree, TEXT(""), 22, White);
		Txt->SetJustification(ETextJustify::Left);
		Btn->AddChild(Txt);

		switch (i)
		{
		case 0: Btn->OnClicked.AddDynamic(this, &UStudyShelfWidget::Row0); break;
		case 1: Btn->OnClicked.AddDynamic(this, &UStudyShelfWidget::Row1); break;
		case 2: Btn->OnClicked.AddDynamic(this, &UStudyShelfWidget::Row2); break;
		case 3: Btn->OnClicked.AddDynamic(this, &UStudyShelfWidget::Row3); break;
		case 4: Btn->OnClicked.AddDynamic(this, &UStudyShelfWidget::Row4); break;
		case 5: Btn->OnClicked.AddDynamic(this, &UStudyShelfWidget::Row5); break;
		case 6: Btn->OnClicked.AddDynamic(this, &UStudyShelfWidget::Row6); break;
		default: Btn->OnClicked.AddDynamic(this, &UStudyShelfWidget::Row7); break;
		}

		RowButtons.Add(Btn);
		RowTexts.Add(Txt);

		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Box->SetWidthOverride(520.f);
		Box->SetHeightOverride(40.f);
		Box->AddChild(Btn);

		if (UVerticalBoxSlot* S = ListBox->AddChildToVerticalBox(Box))
		{
			S->SetPadding(FMargin(0.f, 2.f));
		}
	}

	{
		UButton* CloseBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		UTextBlock* T = ShelfText(WidgetTree, TEXT("关  上  （Esc）"), 22, White, true);
		T->SetJustification(ETextJustify::Center);
		CloseBtn->AddChild(T);
		CloseBtn->OnClicked.AddDynamic(this, &UStudyShelfWidget::OnCloseClicked);

		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Box->SetWidthOverride(520.f);
		Box->SetHeightOverride(44.f);
		Box->AddChild(CloseBtn);
		if (UVerticalBoxSlot* S = ListBox->AddChildToVerticalBox(Box))
		{
			S->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));
		}
	}

	if (UHorizontalBoxSlot* S = Root->AddChildToHorizontalBox(ListBox))
	{
		S->SetVerticalAlignment(VAlign_Top);
		S->SetPadding(FMargin(0.f, 0.f, 36.f, 0.f));
	}

	// ---------------- 详情列 ----------------
	DetailBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	DetailText = ShelfText(WidgetTree, TEXT(""), 22, White);
	DetailText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* S = DetailBox->AddChildToVerticalBox(DetailText))
	{
		S->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));
	}

	{
		UButton* BuyBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		UTextBlock* T = ShelfText(WidgetTree, TEXT("买   下"), 24, FLinearColor(1.f, 0.97f, 0.9f, 1.f), true);
		T->SetJustification(ETextJustify::Center);
		BuyBtn->AddChild(T);
		BuyBtn->OnClicked.AddDynamic(this, &UStudyShelfWidget::OnBuyClicked);

		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Box->SetWidthOverride(240.f);
		Box->SetHeightOverride(48.f);
		Box->AddChild(BuyBtn);
		if (UVerticalBoxSlot* S = DetailBox->AddChildToVerticalBox(Box))
		{
			S->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		}
	}
	{
		UButton* BackBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		UTextBlock* T = ShelfText(WidgetTree, TEXT("返   回"), 24, White, true);
		T->SetJustification(ETextJustify::Center);
		BackBtn->AddChild(T);
		BackBtn->OnClicked.AddDynamic(this, &UStudyShelfWidget::OnBackClicked);

		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Box->SetWidthOverride(240.f);
		Box->SetHeightOverride(48.f);
		Box->AddChild(BackBtn);
		if (UVerticalBoxSlot* S = DetailBox->AddChildToVerticalBox(Box))
		{
			S->SetPadding(FMargin(0.f));
		}
	}

	if (UHorizontalBoxSlot* S = Root->AddChildToHorizontalBox(DetailBox))
	{
		S->SetVerticalAlignment(VAlign_Top);
	}

	// ---------------- 右列：抢货信息 ----------------
	BuyerText = ShelfText(WidgetTree, TEXT(""), 20, Dim);
	BuyerText->SetAutoWrapText(true);

	USizeBox* RightBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	RightBox->SetWidthOverride(280.f);
	RightBox->AddChild(BuyerText);

	if (UHorizontalBoxSlot* S = Root->AddChildToHorizontalBox(RightBox))
	{
		S->SetVerticalAlignment(VAlign_Top);
		S->SetPadding(FMargin(36.f, 40.f, 0.f, 0.f));
	}

	DetailBox->SetVisibility(ESlateVisibility::Collapsed);
	SetVisibility(ESlateVisibility::Collapsed);
}

// ---------------- 开关 ----------------

void UStudyShelfWidget::Open(AMerchant* InOwner)
{
	OwnerMerchant = InOwner;
	bOpen = true;
	PendingSlot = -1;
	SetVisibility(ESlateVisibility::Visible);
	ShowList();
	Refresh();
	SetWidgetInputLocked(true);

	// 拿键盘焦点，这样 Esc 能关面板
	SetIsFocusable(true);
	SetKeyboardFocus();
}

void UStudyShelfWidget::ClosePanel()
{
	bOpen = false;
	PendingSlot = -1;
	SetVisibility(ESlateVisibility::Collapsed);
	SetWidgetInputLocked(false);
}

void UStudyShelfWidget::SetWidgetInputLocked(bool bLocked)
{
	// 注意：这里只是"立刻生效"的即时反馈（开面板的同一帧就能点到按钮）。
	// 真正的权威是 UStudyHUDWidget::SyncInputMode —— 它每帧把 IgnoreMove/Look 用
	// Reset + Set 的绝对量重写一遍。因为 SetIgnoreMoveInput(false) 只是"计数减 1"，
	// 不是清零；只靠加/减配对，中间任何一条路径漏掉就会永久卡住玩家。
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	if (bLocked)
	{
		// 面板开着的时候：显示鼠标、锁住移动和视角，否则玩家还能到处走
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

void UStudyShelfWidget::ShowList()
{
	if (ListBox) { ListBox->SetVisibility(ESlateVisibility::Visible); }
	if (DetailBox) { DetailBox->SetVisibility(ESlateVisibility::Collapsed); }
}

void UStudyShelfWidget::ShowDetail(int32 SlotIndex)
{
	UstudyGameInstance* GI = GetGameInstance<UstudyGameInstance>();
	if (!GI || !GI->Shelf.IsValidIndex(SlotIndex))
	{
		return;
	}

	const FShelfSlot& RowRef = GI->Shelf[SlotIndex];
	const FHandicraftItem& It = RowRef.Item;

	PendingSlot = SlotIndex;

	if (DetailText)
	{
		DetailText->SetText(FText::FromString(FString::Printf(
			TEXT("%s\n\n标称金重：%.0f g\n价    格：%d 币\n黑商说含金量：%.0f%%\n\n「%s」"),
			*UstudyGameInstance::GetCategoryName(It.Category),
			It.NominalGrams, It.Price, It.DeclaredPurity * 100.f, *It.SalesPitch)));
	}

	if (ListBox) { ListBox->SetVisibility(ESlateVisibility::Collapsed); }
	if (DetailBox) { DetailBox->SetVisibility(ESlateVisibility::Visible); }
}

// ---------------- 刷新 ----------------

void UStudyShelfWidget::Refresh()
{
	UstudyGameInstance* GI = GetGameInstance<UstudyGameInstance>();
	if (!GI)
	{
		return;
	}

	RowSlotIndex.Reset();

	FString BuyerLog;
	int32 SoldCount = 0;

	for (int32 i = 0; i < RowButtons.Num(); ++i)
	{
		const bool bHas = GI->Shelf.IsValidIndex(i);
		if (!bHas)
		{
			if (RowTexts[i]) { RowTexts[i]->SetText(FText::GetEmpty()); }
			if (RowButtons[i]) { RowButtons[i]->SetIsEnabled(false); }
			RowSlotIndex.Add(INDEX_NONE);
			continue;
		}

		const FShelfSlot& RowRef = GI->Shelf[i];
		RowSlotIndex.Add(i);

		if (RowTexts[i])
		{
			if (RowRef.bSold)
			{
				RowTexts[i]->SetText(FText::FromString(FString::Printf(
					TEXT("%d.  %s   —— 已被人买走"),
					i + 1, *UstudyGameInstance::GetCategoryName(RowRef.Item.Category))));
				RowTexts[i]->SetColorAndOpacity(FSlateColor(FLinearColor(0.35f, 0.35f, 0.38f, 1.f)));
				RowTexts[i]->SetOpacity(0.55f);
			}
			else
			{
				RowTexts[i]->SetText(FText::FromString(FString::Printf(
					TEXT("%d.  %s    %.0f g    报 %.0f%%    %d 币"),
					i + 1, *UstudyGameInstance::GetCategoryName(RowRef.Item.Category),
					RowRef.Item.NominalGrams, RowRef.Item.DeclaredPurity * 100.f, RowRef.Item.Price)));
				RowTexts[i]->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.92f, 0.95f, 1.f)));
				RowTexts[i]->SetOpacity(1.f);
			}
		}

		if (RowButtons[i])
		{
			RowButtons[i]->SetIsEnabled(!RowRef.bSold);
		}

		if (RowRef.bSold)
		{
			++SoldCount;
			BuyerLog += FString::Printf(TEXT("· %s 买走了 %s\n"),
				RowRef.BuyerName.IsEmpty() ? TEXT("有人") : *RowRef.BuyerName,
				*UstudyGameInstance::GetCategoryName(RowRef.Item.Category));
		}
	}

	if (BuyerText)
	{
		BuyerText->SetText(FText::FromString(SoldCount > 0
			? FString::Printf(TEXT("今天被买走的：\n\n%s"), *BuyerLog)
			: TEXT("今天还没人动过这柜子。\n\n（手快有，手慢无）")));
	}
}

// ---------------- 按钮 ----------------

void UStudyShelfWidget::PickRow(int32 Index)
{
	if (!RowSlotIndex.IsValidIndex(Index))
	{
		return;
	}
	const int32 SlotIndex = RowSlotIndex[Index];
	if (SlotIndex == INDEX_NONE)
	{
		return;
	}
	ShowDetail(SlotIndex);
}

void UStudyShelfWidget::DoBuy()
{
	UstudyGameInstance* GI = GetGameInstance<UstudyGameInstance>();
	if (!GI || !GI->Shelf.IsValidIndex(PendingSlot))
	{
		return;
	}

	FShelfSlot& RowRef = GI->Shelf[PendingSlot];
	if (RowRef.bSold)
	{
		ShowList();
		Refresh();
		return;
	}

	if (GI->Cash < RowRef.Item.Price)
	{
		// 钱不够：留在详情页，刷新说明
		if (DetailText)
		{
			DetailText->SetText(FText::FromString(FString::Printf(
				TEXT("钱不够。\n\n要 %d 币，你只有 %d 币。"), RowRef.Item.Price, GI->Cash)));
		}
		return;
	}

	GI->Cash -= RowRef.Item.Price;

	FHandicraftItem Bought = RowRef.Item;
	Bought.bAppraised = false;
	GI->HandicraftBag.Add(Bought);

	RowRef.bSold = true;
	RowRef.BuyerName = TEXT("你");

	UGameplayStatics::PlaySound2D(this, StudySounds::Cash(), 0.8f);

	UE_LOG(LogTemp, Log, TEXT("[Shelf] 买下 %s（%.0f g，%d 币，报 %.0f%%，真实 %.0f%%）"),
		*UstudyGameInstance::GetCategoryName(Bought.Category), Bought.NominalGrams, Bought.Price,
		Bought.DeclaredPurity * 100.f, Bought.RealPurity * 100.f);

	ShowList();
	Refresh();
}

void UStudyShelfWidget::OnBuyClicked()
{
	DoBuy();
}

void UStudyShelfWidget::OnBackClicked()
{
	PendingSlot = -1;
	ShowList();
	Refresh();
}

FReply UStudyShelfWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bOpen && InKeyEvent.GetKey() == EKeys::Escape)
	{
		ClosePanel();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UStudyShelfWidget::OnCloseClicked()
{
	ClosePanel();
}
