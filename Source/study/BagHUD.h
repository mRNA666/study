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

	/** ESC：开关设置菜单（角色按 ESC 时调这里） */
	void TogglePauseMenu();

	/** Tab：开关资产排行面板 */
	void ToggleRank();

	/** 黑商点「购买工艺品」：打开货架面板 */
	void OpenShelf(class AMerchant* InOwner);

	/** B：开关工艺品背包 */
	void ToggleBag();

	/** 关掉货架/背包面板（ESC 先关面板）；返回 true 表示确实关掉了什么 */
	bool CloseHandicraftPanels();

	/** 货架或背包面板是不是开着（HUD 用它决定要不要抢输入模式） */
	bool IsHandicraftPanelOpen() const;

	/** 暂停/主菜单是不是开着 */
	bool IsMenuOpen() const;

	/** 开局如果还没点过「开始游戏」，就弹主菜单 */
	void ShowMainMenuIfNeeded();

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

	/** 主菜单 / ESC 设置菜单（开场和暂停都用它） */
	UPROPERTY()
	TObjectPtr<class UStudyMenuWidget> MenuWidget;

	/** Tab 资产排行（按 Tab 才显示） */
	UPROPERTY()
	TObjectPtr<class UStudyRankWidget> RankWidget;

	/** 黑市货架面板 */
	UPROPERTY()
	TObjectPtr<class UStudyShelfWidget> ShelfWidget;

	/** 工艺品背包 */
	UPROPERTY()
	TObjectPtr<class UStudyBagWidget> BagPanel;

	virtual void BeginPlay() override;

	/** 卸货区缓存。每 2 秒重扫一次，避免每帧 GetAllActorsOfClass */
	TArray<TWeakObjectPtr<class AActor>> CachedZones;

	/** 下一次重扫的时间（世界时间秒） */
	double NextZoneScanTime = 0.0;
};
