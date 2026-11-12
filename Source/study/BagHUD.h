// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BagHUD.generated.h"

/**
 * 极简 HUD：只在屏幕左下角画一行"背包 12.5 / 50.0 kg（25%）"。
 * 用 DrawText 直接画，不依赖任何 UMG 资产，改完立刻能看见。
 * 以后要美化再换成 Widget。
 */
UCLASS()
class STUDY_API ABagHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** 左边距 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float ScreenX = 60.f;

	/** 面板底边离屏幕底部的距离（像素）。0 = 贴底 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float ScreenYFromBottom = 50.f;

	/** 字体缩放 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float FontScale = 2.4f;

	/** 卸货区标记：框线颜色 */
	UPROPERTY(EditAnywhere, Category = "HUD|Zone")
	FLinearColor ZoneEdgeColor = FLinearColor(1.f, 0.18f, 0.12f, 0.95f);

	/** 卸货区标记：文字颜色 */
	UPROPERTY(EditAnywhere, Category = "HUD|Zone")
	FLinearColor ZoneTextColor = FLinearColor(1.f, 0.35f, 0.2f, 1.f);

private:
	/** 画所有卸货区的红框和"卸货区"标签（屏幕空间投影，不需要任何资产） */
	void DrawUnloadZones();

	/** 结算面板：对局结束之后盖在屏幕中央 */
	void DrawSettlement();

	/** 商人的对话框（选项列表 / 数字输入） */
	void DrawDialog();

	// ---------------- UMG ----------------

	/** UMG 版的 HUD。创建成功时，Canvas 那套就整个不画了 */
	UPROPERTY(Transient)
	TObjectPtr<class UStudyHUDWidget> HUDWidget;

	virtual void BeginPlay() override;

	/** 卸货区缓存。每 2 秒重扫一次，避免每帧 GetAllActorsOfClass */
	TArray<TWeakObjectPtr<class AActor>> CachedZones;

	/** 下一次重扫的时间（世界时间秒） */
	double NextZoneScanTime = 0.0;
};
