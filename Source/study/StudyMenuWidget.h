// Fill out your copyright notice in the Description page of Project Settings.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StudyMenuWidget.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UTextBlock;
class UVerticalBox;
class USlider;

/**
 * 主菜单 + ESC 设置菜单。
 *
 * 两个页共用一个控件：主菜单是开场那张黑屏，设置页是 ESC 弹的半透明页。
 * 全部用 C++ 建树，不依赖任何 UMG 资产 —— 和 StudyHUDWidget 一个路子。
 *
 * 打开时会 SetGamePaused(true)，所以游戏时钟会停住，不会在菜单里流失开采时间。
 * 关闭时把音量 / 头灯亮度应用到游戏。
 */
UCLASS()
class STUDY_API UStudyMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 开场主菜单（不透明的黑底） */
	void ShowMainMenu();

	/** ESC 设置菜单（半透明，能看见后面的游戏画面） */
	void ShowPauseMenu();

	/** 全部隐藏 + 解除暂停 + 应用设置 */
	void CloseMenu();

	/** ESC：菜单没开就开（设置页），开了就关 */
	void TogglePauseMenu();

	bool IsMenuOpen() const { return bMenuOpen; }

	/** 把当前的音量 / 头灯亮度应用到游戏 */
	void ApplySettings();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UFUNCTION() void OnStartClicked();
	UFUNCTION() void OnResumeClicked();
	UFUNCTION() void OnQuitClicked();
	UFUNCTION() void OnVolumeChanged(float Value);
	UFUNCTION() void OnHeadLightChanged(float Value);

	void BuildLayout();
	void ShowMenu(bool bMain);
	void SetInputLocked(bool bLocked);
	void UpdateLabels();

	/** 整块黑底（主菜单不透明 / 设置页半透明） */
	UPROPERTY()
	TObjectPtr<UBorder> RootBorder;

	UPROPERTY()
	TObjectPtr<UVerticalBox> MainPage;

	UPROPERTY()
	TObjectPtr<UVerticalBox> PausePage;

	UPROPERTY()
	TObjectPtr<USlider> VolumeSlider;

	UPROPERTY()
	TObjectPtr<USlider> HeadLightSlider;

	UPROPERTY()
	TObjectPtr<UTextBlock> VolumeValueText;

	UPROPERTY()
	TObjectPtr<UTextBlock> HeadLightValueText;

	bool bMenuOpen = false;

	/** true = 现在显示的是开场主菜单（没有「继续游戏」按钮） */
	bool bMainMenuMode = true;
};
