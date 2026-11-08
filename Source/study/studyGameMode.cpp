// Copyright Epic Games, Inc. All Rights Reserved.

#include "studyGameMode.h"
#include "BagHUD.h"

AstudyGameMode::AstudyGameMode()
{
	// 用最简 HUD 显示背包重量（不用建 UMG 资产）
	HUDClass = ABagHUD::StaticClass();
}
