// Fill out your copyright notice in the Description page of Project Settings.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StudyRankWidget.generated.h"

class UBorder;
class UTextBlock;
class UVerticalBox;

/**
 * Tab 资产排行面板。
 *
 * 口径和对手完全一致：现金 + 足金重 × 正规价。
 * 你自己的那一行会把「仓库黄金 + 暂存黄金 + 背包里背着的」都算进去。
 */
UCLASS()
class STUDY_API UStudyRankWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Tab：显示/隐藏 */
	void Toggle();

	void Show();
	void Hide();

	bool IsShown() const { return bShown; }

	/** 重新算名次、刷新文字 */
	void Refresh();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	void BuildLayout();

	UPROPERTY()
	TObjectPtr<UBorder> PanelBorder;

	UPROPERTY()
	TObjectPtr<UVerticalBox> RowBox;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> RowTexts;

	UPROPERTY()
	TObjectPtr<UTextBlock> MarketText;

	bool bShown = false;

	/** 显示时每 0.3 秒刷一次就够了，不用每帧排序 */
	float RefreshAccum = 0.f;
};
