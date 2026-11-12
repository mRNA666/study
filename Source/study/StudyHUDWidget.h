// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StudyHUDWidget.generated.h"

class UTextBlock;
class UButton;
class UImage;
class UBorder;
class UProgressBar;
class UCanvasPanel;
class UVerticalBox;
class UHorizontalBox;
class UEditableTextBox;
class AstudyCharacter;
class AstudyGameState;

/**
 * 整个游戏的 HUD，全部用 UMG（Slate）渲染 —— 所以文字是清晰的，
 * 不像 AHUD::DrawText 那样把小字图放大糊成一团。
 *
 * 控件树全部在 C++ 里搭，不需要你画控件蓝图。
 * 想改字大/位置，改本文件顶部的几个常量即可。
 */
UCLASS()
class STUDY_API UStudyHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ---------------- 对话按钮回调 ----------------

	UFUNCTION() void OnOption1() { ChooseOption(0); }
	UFUNCTION() void OnOption2() { ChooseOption(1); }
	UFUNCTION() void OnOption3() { ChooseOption(2); }
	UFUNCTION() void OnOption4() { ChooseOption(3); }
	UFUNCTION() void OnNumberSubmit();
	UFUNCTION() void OnDialogClose();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 搭建整个控件树（只跑一次） */
	void BuildLayout();

	/** 每帧把数据同步到控件 */
	void Refresh();

	void ChooseOption(int32 Index);

	/** 打开/关闭对话时切换输入模式（鼠标 + 锁移动） */
	void SetDialogInputMode(bool bDialogOpen);

	AstudyCharacter* GetStudyCharacter() const;
	AstudyGameState* GetStudyState() const;

	// ---------------- 控件引用 ----------------

	UPROPERTY() TObjectPtr<UTextBlock> ClockText;
	UPROPERTY() TObjectPtr<UTextBlock> MineTimeText;
	UPROPERTY() TObjectPtr<UBorder> ClockBorder;

	UPROPERTY() TObjectPtr<UTextBlock> WeightText;
	UPROPERTY() TObjectPtr<UTextBlock> PctText;
	UPROPERTY() TObjectPtr<UProgressBar> LoadBar;
	UPROPERTY() TObjectPtr<UTextBlock> GoldText;
	UPROPERTY() TObjectPtr<UTextBlock> CashText;

	UPROPERTY() TObjectPtr<UTextBlock> HintText;
	UPROPERTY() TObjectPtr<UTextBlock> NoticeText;

	UPROPERTY() TObjectPtr<UBorder> SettlementBorder;
	UPROPERTY() TObjectPtr<UTextBlock> SettlementCashText;
	UPROPERTY() TObjectPtr<UTextBlock> SettlementInfoText;

	UPROPERTY() TObjectPtr<UBorder> DialogBorder;
	UPROPERTY() TObjectPtr<UTextBlock> DialogTitleText;
	UPROPERTY() TObjectPtr<UTextBlock> DialogBodyText;
	UPROPERTY() TObjectPtr<UEditableTextBox> NumberBox;
	UPROPERTY() TObjectPtr<UButton> NumberSubmitButton;
	UPROPERTY() TObjectPtr<class UHorizontalBox> NumberRow;
	UPROPERTY() TObjectPtr<UButton> CloseButton;

	UPROPERTY() TArray<TObjectPtr<UButton>> OptionButtons;

	/** 上一帧对话是不是开着（用来检测状态变化） */
	bool bWasDialogOpen = false;
};
