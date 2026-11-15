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
	UFUNCTION() void OnOption5() { ChooseOption(4); }
	UFUNCTION() void OnOption6() { ChooseOption(5); }
	UFUNCTION() void OnNumberSubmit();
	UFUNCTION() void OnNumberSubmit2();
	UFUNCTION() void OnDialogClose();

	/** 滚一下对话框正文（长正文时用；↑↓ / PageUp/Down 也走这里） */
	void ScrollBodyBy(float Pixels);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/**
	 * 滚轮：鼠标停在对话框上时，滚轮事件会被 UMG 先吃掉（到不了视口的 MouseWheelAxis），
	 * 所以这里再接一份 —— 对话开着就翻页。
	 */
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** 搭建整个控件树（只跑一次） */
	void BuildLayout();

	/** 每帧把数据同步到控件 */
	void Refresh();

	void ChooseOption(int32 Index);

	/**
	 * 【唯一的输入总闸】每帧按当前 UI 状态，把 PlayerController 上的输入锁按"绝对量"写一遍。
	 *
	 * 为什么必须绝对量：引擎里 SetIgnoreMoveInput(false) 不是清零，是"把锁计数减 1"
	 * （AController::SetIgnoreMoveInput：IgnoreMoveInput = Max(IgnoreMoveInput ± 1, 0)，
	 * 见 Controller.cpp:171）。只要有一路"加了没减"——比如对话里直接开了货架面板，
	 * 对话那次 −1 被跳过——计数就永久停在 1，界面全关了人还是走不动、转不了视角。
	 * 所以这里每帧先 Reset 成 0，再按当前状态决定要不要重新加 1，任何人都算错不了。
	 */
	void SyncInputMode(bool bLoading);

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

	/** 数字输入行开头的那个字（默认「卖」） */
	UPROPERTY() TObjectPtr<UTextBlock> NumberLabelText;

	/** 数字输入行后面的单位标签（g / %） */
	UPROPERTY() TObjectPtr<UTextBlock> NumberUnitText;

	UPROPERTY() TObjectPtr<UBorder> SettlementBorder;
	UPROPERTY() TObjectPtr<UTextBlock> SettlementCashText;
	UPROPERTY() TObjectPtr<UTextBlock> SettlementInfoText;

	UPROPERTY() TObjectPtr<UBorder> DialogBorder;
	UPROPERTY() TObjectPtr<UTextBlock> DialogTitleText;
	UPROPERTY() TObjectPtr<UTextBlock> DialogBodyText;

	/** 正文的可滚动版（每日播报这种长文本用；SizeBox 卡最高高度，ScrollBox 里面滚） */
	UPROPERTY() TObjectPtr<class USizeBox> BodyScrollSize;
	UPROPERTY() TObjectPtr<class UScrollBox> DialogBodyScroll;
	UPROPERTY() TObjectPtr<UTextBlock> DialogBodyScrollText;
	UPROPERTY() TObjectPtr<UEditableTextBox> NumberBox;
	UPROPERTY() TObjectPtr<UButton> NumberSubmitButton;
	UPROPERTY() TObjectPtr<class UHorizontalBox> NumberRow;
	UPROPERTY() TObjectPtr<UEditableTextBox> NumberBox2;
	UPROPERTY() TObjectPtr<UButton> NumberSubmitButton2;
	UPROPERTY() TObjectPtr<class UHorizontalBox> NumberRow2;
	UPROPERTY() TObjectPtr<UButton> CloseButton;

	UPROPERTY() TArray<TObjectPtr<UButton>> OptionButtons;

	// ---------------- 矿洞加载遮罩 ----------------

	UPROPERTY() TObjectPtr<UBorder> LoadingBorder;
	UPROPERTY() TObjectPtr<UTextBlock> LoadingText;
	UPROPERTY() TObjectPtr<UProgressBar> LoadingBar;

	/** 上一帧是不是在加载（用来检测状态变化） */
	bool bWasLoading = false;

	/** 缓存的矿洞生成器（每帧遍历关卡太浪费） */
	TWeakObjectPtr<class AMineGenerator> CachedMineGen;

	/** 进入/离开加载状态：显示遮罩 + 锁住玩家操作 */
	void SetLoadingState(bool bLoading);

	/** 上一帧对话是不是开着（用来检测状态变化） */
	bool bWasDialogOpen = false;

	/** 上一帧是不是"该显示鼠标"（只在翻转的那一帧才 SetInputMode，免得每帧打断控件焦点） */
	bool bLastInputModal = false;

	/** 上一帧移动锁是不是关着（只在翻转的那一帧打日志） */
	bool bLastLockMove = false;

	/** 上一帧输入框的单位（g / %），变了才改文字 */
	FString LastNumberUnit;

	/** 上一帧输入框的行首字 / 灰字，变了才改 */
	FString LastNumberLabel;
	FString LastNumberHint;

	/** 上一帧滚动态正文的内容（变了才重写 + 回到顶部） */
	FString LastScrollBody;

	/** 输入框预填：记住已经应用过的版本号，避免每帧覆盖玩家正在敲的字 */
	int32 AppliedPrefill1 = -1;
	int32 AppliedPrefill2 = -1;

	/** 上一帧这两行输入框是不是可见（由隐到显时清空，开新页面不带上次的字） */
	bool bNumberRowVisible = false;
	bool bNumberRow2Visible = false;
};
