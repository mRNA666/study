// Fill out your copyright notice in the Description page of Project Settings.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StudyShelfWidget.generated.h"

class UBorder;
class UButton;
class UTextBlock;
class UVerticalBox;
class AMerchant;

/**
 * 黑市"柜台上的货"面板。
 *
 * 左边 8 行（可点，被买走的变暗不可点），右边是"今天谁买走了什么"。
 * 点一行 → 切到详情（标称 / 价格 / 黑商报的含金量 / 话术）→ [买下][返回]。
 */
UCLASS()
class STUDY_API UStudyShelfWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 打开面板（由黑商调用） */
	void Open(AMerchant* InOwner);

	void ClosePanel();

	bool IsOpen() const { return bOpen; }

	void Refresh();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	void BuildLayout();
	void ShowList();
	void ShowDetail(int32 SlotIndex);
	void DoBuy();
	void SetWidgetInputLocked(bool bLocked);

	UFUNCTION() void OnCloseClicked();
	UFUNCTION() void OnBuyClicked();
	UFUNCTION() void OnBackClicked();

	// 8 行：因为 UMG 的按钮回调必须是 UFUNCTION，没法带参数
	UFUNCTION() void Row0() { PickRow(0); }
	UFUNCTION() void Row1() { PickRow(1); }
	UFUNCTION() void Row2() { PickRow(2); }
	UFUNCTION() void Row3() { PickRow(3); }
	UFUNCTION() void Row4() { PickRow(4); }
	UFUNCTION() void Row5() { PickRow(5); }
	UFUNCTION() void Row6() { PickRow(6); }
	UFUNCTION() void Row7() { PickRow(7); }

	void PickRow(int32 Index);

	UPROPERTY()
	TObjectPtr<UBorder> PanelBorder;

	UPROPERTY()
	TObjectPtr<UVerticalBox> ListBox;

	UPROPERTY()
	TObjectPtr<UVerticalBox> DetailBox;

	UPROPERTY()
	TObjectPtr<UTextBlock> BuyerText;

	UPROPERTY()
	TObjectPtr<UTextBlock> DetailText;

	UPROPERTY()
	TArray<TObjectPtr<UButton>> RowButtons;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> RowTexts;

	UPROPERTY()
	TObjectPtr<AMerchant> OwnerMerchant;

	bool bOpen = false;
	int32 PendingSlot = -1;

	/** 行内容的缓存（避免点击时还要去 GameInstance 里对一遍索引） */
	TArray<int32> RowSlotIndex;
};
