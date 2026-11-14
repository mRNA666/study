// Fill out your copyright notice in the Description page of Project Settings.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StudyBagWidget.generated.h"

class UBorder;
class UTextBlock;
class UVerticalBox;

/** 工艺品背包（按 B 打开）：没鉴定过显示黑商报价，鉴定过显示真实含金量 */
UCLASS()
class STUDY_API UStudyBagWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Toggle();
	void OpenBag();
	void CloseBag();
	bool IsOpen() const { return bOpen; }
	void Refresh();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	void BuildLayout();
	void SetWidgetInputLocked(bool bLocked);

	UPROPERTY()
	TObjectPtr<UBorder> PanelBorder;

	UPROPERTY()
	TObjectPtr<UVerticalBox> ListBox;

	UPROPERTY()
	TObjectPtr<UTextBlock> SumText;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> Lines;

	bool bOpen = false;
};
