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

	/** 行高（像素）。字越大这个也要跟着调大 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float LineHeight = 56.f;

	/** 左边距 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float ScreenX = 60.f;

	/** 上边距（从屏幕底部往上算） */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float ScreenYFromBottom = 140.f;

	/** 字体缩放 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float FontScale = 2.4f;
};
