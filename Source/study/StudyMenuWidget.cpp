// Fill out your copyright notice in the Description page of Project Settings.

#include "StudyMenuWidget.h"
#include "studyCharacter.h"
#include "studyGameInstance.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"
#include "InputCoreTypes.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/Engine.h"
#include "AudioDevice.h"
#include "AudioDeviceHandle.h"

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

	/** 固定宽高的 SizeBox 包一个按钮 */
	USizeBox* MakeButton(UWidgetTree* Tree, const FString& Label, float W, float H, UButton*& OutButton)
	{
		UButton* B = Tree->ConstructWidget<UButton>(UButton::StaticClass());
		UTextBlock* T = MakeText(Tree, Label, 26, FLinearColor(1.f, 0.97f, 0.9f, 1.f), true);
		T->SetJustification(ETextJustify::Center);
		B->AddChild(T);

		USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Box->SetWidthOverride(W);
		Box->SetHeightOverride(H);
		Box->AddChild(B);

		OutButton = B;
		return Box;
	}

	/** 给一个控件套上固定宽度（用来做"标签 | 滑块 | 数值"三栏） */
	USizeBox* WrapWidth(UWidgetTree* Tree, UWidget* W, float Width)
	{
		USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Box->SetWidthOverride(Width);
		Box->AddChild(W);
		return Box;
	}

	/** 往 VerticalBox 里加一项，上下留白 + 水平居中 */
	void AddRow(UVerticalBox* Box, UWidget* W, float PadY = 10.f)
	{
		if (UVerticalBoxSlot* Slot = Box->AddChildToVerticalBox(W))
		{
			Slot->SetPadding(FMargin(0.f, PadY));
			Slot->SetHorizontalAlignment(HAlign_Center);
		}
	}

	/** 把一个控件摆在画布正中间 */
	void PlaceCenter(UCanvasPanel* Canvas, UWidget* W)
	{
		if (UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(W))
		{
			Slot->SetAnchors(FAnchors(0.5f, 0.5f));
			Slot->SetAlignment(FVector2D(0.5f, 0.5f));
			Slot->SetAutoSize(true);
			Slot->SetPosition(FVector2D::ZeroVector);
		}
	}
}

// ---------------- 搭控件树 ----------------

TSharedRef<SWidget> UStudyMenuWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildLayout();
	}
	return Super::RebuildWidget();
}

void UStudyMenuWidget::BuildLayout()
{
	const FLinearColor Gold(0.95f, 0.78f, 0.28f, 1.f);
	const FLinearColor Dim(0.72f, 0.72f, 0.76f, 1.f);

	// 根：铺满全屏的黑底（设置页时调成半透明）
	RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	RootBorder->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.03f, 1.f));
	RootBorder->SetPadding(FMargin(0.f));
	WidgetTree->RootWidget = RootBorder;

	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	RootBorder->SetContent(Canvas);

	// ---------------- 主菜单 ----------------
	MainPage = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	PlaceCenter(Canvas, MainPage);

	AddRow(MainPage, MakeText(WidgetTree, TEXT("脏  金"), 88, Gold, true), 0.f);
	AddRow(MainPage, MakeText(WidgetTree, TEXT("D I R T Y   G O L D"), 20, Dim), 4.f);

	USpacer* GapMain = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
	GapMain->SetSize(FVector2D(0.f, 46.f));
	AddRow(MainPage, GapMain, 0.f);

	{
		UButton* Btn = nullptr;
		USizeBox* Box = MakeButton(WidgetTree, TEXT("开始游戏"), 340.f, 66.f, Btn);
		if (Btn)
		{
			Btn->OnClicked.AddDynamic(this, &UStudyMenuWidget::OnStartClicked);
		}
		AddRow(MainPage, Box);
	}
	{
		UButton* Btn = nullptr;
		USizeBox* Box = MakeButton(WidgetTree, TEXT("退    出"), 340.f, 66.f, Btn);
		if (Btn)
		{
			Btn->OnClicked.AddDynamic(this, &UStudyMenuWidget::OnQuitClicked);
		}
		AddRow(MainPage, Box);
	}

	// ---------------- ESC 设置页 ----------------
	PausePage = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	PlaceCenter(Canvas, PausePage);

	AddRow(PausePage, MakeText(WidgetTree, TEXT("设  置"), 56, Gold, true), 0.f);

	USpacer* GapPause = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
	GapPause->SetSize(FVector2D(0.f, 30.f));
	AddRow(PausePage, GapPause, 0.f);

	// 主音量：标签 | 滑块 | 数值
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		VolumeSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
		VolumeSlider->SetMinValue(0.f);
		VolumeSlider->SetMaxValue(1.f);
		VolumeSlider->SetStepSize(0.01f);
		VolumeSlider->SetValue(0.7f);
		VolumeSlider->OnValueChanged.AddDynamic(this, &UStudyMenuWidget::OnVolumeChanged);

		VolumeValueText = MakeText(WidgetTree, TEXT("70%"), 24, Dim);

		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(WrapWidth(WidgetTree, MakeText(WidgetTree, TEXT("主音量"), 24, Dim), 150.f)))
		{
			S->SetVerticalAlignment(VAlign_Center);
		}
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(WrapWidth(WidgetTree, VolumeSlider, 320.f)))
		{
			S->SetVerticalAlignment(VAlign_Center);
		}
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(VolumeValueText))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(16.f, 0.f, 0.f, 0.f));
		}
		AddRow(PausePage, Row, 12.f);
	}

	// 头灯亮度
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

		HeadLightSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
		HeadLightSlider->SetMinValue(0.f);
		HeadLightSlider->SetMaxValue(3.f);
		HeadLightSlider->SetStepSize(0.05f);
		HeadLightSlider->SetValue(1.f);
		HeadLightSlider->OnValueChanged.AddDynamic(this, &UStudyMenuWidget::OnHeadLightChanged);

		HeadLightValueText = MakeText(WidgetTree, TEXT("100%"), 24, Dim);

		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(WrapWidth(WidgetTree, MakeText(WidgetTree, TEXT("头灯亮度"), 24, Dim), 150.f)))
		{
			S->SetVerticalAlignment(VAlign_Center);
		}
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(WrapWidth(WidgetTree, HeadLightSlider, 320.f)))
		{
			S->SetVerticalAlignment(VAlign_Center);
		}
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(HeadLightValueText))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(16.f, 0.f, 0.f, 0.f));
		}
		AddRow(PausePage, Row, 12.f);
	}

	USpacer* GapPause2 = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
	GapPause2->SetSize(FVector2D(0.f, 26.f));
	AddRow(PausePage, GapPause2, 0.f);

	{
		UButton* Btn = nullptr;
		USizeBox* Box = MakeButton(WidgetTree, TEXT("继续游戏"), 340.f, 62.f, Btn);
		if (Btn)
		{
			Btn->OnClicked.AddDynamic(this, &UStudyMenuWidget::OnResumeClicked);
		}
		AddRow(PausePage, Box);
	}
	{
		UButton* Btn = nullptr;
		USizeBox* Box = MakeButton(WidgetTree, TEXT("退出游戏"), 340.f, 62.f, Btn);
		if (Btn)
		{
			Btn->OnClicked.AddDynamic(this, &UStudyMenuWidget::OnQuitClicked);
		}
		AddRow(PausePage, Box);
	}

	MainPage->SetVisibility(ESlateVisibility::Collapsed);
	PausePage->SetVisibility(ESlateVisibility::Collapsed);
}

// ---------------- 生命周期 ----------------

void UStudyMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 需要键盘焦点才能收到 ESC（NativeOnKeyDown）
	SetIsFocusable(true);

	// 初始值从 GameInstance 读（跨关卡保留）
	if (UstudyGameInstance* GI = GetGameInstance<UstudyGameInstance>())
	{
		if (VolumeSlider)
		{
			VolumeSlider->SetValue(GI->MasterVolume);
		}
		if (HeadLightSlider)
		{
			HeadLightSlider->SetValue(GI->HeadLightScale);
		}
	}

	UpdateLabels();

	// 没被要求打开时先藏起来，避免开局闪一下
	if (!bMenuOpen)
	{
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

FReply UStudyMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && bMenuOpen && !bMainMenuMode)
	{
		CloseMenu();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// ---------------- 显示 / 隐藏 ----------------

void UStudyMenuWidget::ShowMainMenu()
{
	ShowMenu(/*bMain=*/true);
}

void UStudyMenuWidget::ShowPauseMenu()
{
	ShowMenu(/*bMain=*/false);
}

void UStudyMenuWidget::TogglePauseMenu()
{
	if (bMenuOpen)
	{
		// 开场主菜单不能用 ESC 关掉（必须先点「开始游戏」）
		if (!bMainMenuMode)
		{
			CloseMenu();
		}
		return;
	}
	ShowPauseMenu();
}

void UStudyMenuWidget::ShowMenu(bool bMain)
{
	bMenuOpen = true;
	bMainMenuMode = bMain;

	SetVisibility(ESlateVisibility::Visible);

	if (MainPage)
	{
		MainPage->SetVisibility(bMain ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (PausePage)
	{
		PausePage->SetVisibility(bMain ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}

	// 主菜单：全黑；设置页：半透明，能看见后面的矿
	if (RootBorder)
	{
		RootBorder->SetBrushColor(bMain
			? FLinearColor(0.02f, 0.02f, 0.03f, 1.f)
			: FLinearColor(0.02f, 0.02f, 0.03f, 0.62f));
	}

	SetInputLocked(true);
	UpdateLabels();

	// 暂停世界：菜单里流逝的时间不算开采时间
	UGameplayStatics::SetGamePaused(this, true);

	// ESC 靠键盘焦点进来
	SetKeyboardFocus();
}

void UStudyMenuWidget::CloseMenu()
{
	bMenuOpen = false;

	SetVisibility(ESlateVisibility::Collapsed);
	SetInputLocked(false);
	ApplySettings();

	UGameplayStatics::SetGamePaused(this, false);
}

void UStudyMenuWidget::SetInputLocked(bool bLocked)
{
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
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetIgnoreMoveInput(false);
		PC->SetIgnoreLookInput(false);
	}
}

// ---------------- 设置 ----------------

void UStudyMenuWidget::UpdateLabels()
{
	if (VolumeValueText && VolumeSlider)
	{
		VolumeValueText->SetText(FText::FromString(
			FString::Printf(TEXT("%.0f%%"), VolumeSlider->GetValue() * 100.f)));
	}
	if (HeadLightValueText && HeadLightSlider)
	{
		HeadLightValueText->SetText(FText::FromString(
			FString::Printf(TEXT("%.0f%%"), HeadLightSlider->GetValue() * 100.f)));
	}
}

void UStudyMenuWidget::ApplySettings()
{
	UstudyGameInstance* GI = GetGameInstance<UstudyGameInstance>();
	if (!GI)
	{
		return;
	}

	// 主音量：SetTransientPrimaryVolume 是 5.1 之后的新名字
	//（老名字 SetTransientMasterVolume 已废弃）。它内部会把命令转发到音频线程，游戏线程可调。
	FAudioDeviceHandle Handle = FAudioDevice::GetMainAudioDevice();
	if (FAudioDevice* Device = Handle.GetAudioDevice())
	{
		Device->SetTransientPrimaryVolume(FMath::Clamp(GI->MasterVolume, 0.f, 1.f));
	}

	// 头灯亮度
	if (AstudyCharacter* Char = Cast<AstudyCharacter>(GetOwningPlayerPawn()))
	{
		Char->SetHeadLightScale(GI->HeadLightScale);
	}
}

void UStudyMenuWidget::OnVolumeChanged(float Value)
{
	if (UstudyGameInstance* GI = GetGameInstance<UstudyGameInstance>())
	{
		GI->MasterVolume = FMath::Clamp(Value, 0.f, 1.f);
	}
	UpdateLabels();
	ApplySettings();
}

void UStudyMenuWidget::OnHeadLightChanged(float Value)
{
	if (UstudyGameInstance* GI = GetGameInstance<UstudyGameInstance>())
	{
		GI->HeadLightScale = FMath::Clamp(Value, 0.f, 3.f);
	}
	UpdateLabels();
	ApplySettings();
}

// ---------------- 按钮 ----------------

void UStudyMenuWidget::OnStartClicked()
{
	// 记一笔：以后回营地不再弹主菜单（按 R 重开会重置这个标记）
	if (UstudyGameInstance* GI = GetGameInstance<UstudyGameInstance>())
	{
		GI->bMainMenuDone = true;
	}
	CloseMenu();
}

void UStudyMenuWidget::OnResumeClicked()
{
	CloseMenu();
}

void UStudyMenuWidget::OnQuitClicked()
{
	ApplySettings();
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
