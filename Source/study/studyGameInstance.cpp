// Fill out your copyright notice in the Description page of Project Settings.


#include "studyGameInstance.h"
#include "studyCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

void UstudyGameInstance::ResetRun()
{
	DayIndex = 1;
	ClockMinutes = DayStartMinutes;
	RightsBoughtToday = 0;
	MineTimeLeft = 0.f;
	MineStopClockMinutes = 0.f;
	Cash = StartingCash;
	GoldGrams = 0.f;
	StashedGoldGrams = 0.f;
	bRunFinished = false;

	UE_LOG(LogTemp, Log, TEXT("[Run] 新的一局开始（%d 天）"), TotalDays);
}

void UstudyGameInstance::SleepToNextDay()
{
	DayIndex++;
	ClockMinutes = DayStartMinutes;
	RightsBoughtToday = 0;

	// 第 2 天睡过去之后就没有第 3 天了，交给 GameMode 去结算
	UE_LOG(LogTemp, Log, TEXT("[Run] 睡到第 %d 天 08:00"), DayIndex);
}

void UstudyGameInstance::ClampClock()
{
	ClockMinutes = FMath::Clamp(ClockMinutes, DayStartMinutes, DayEndMinutes);
}

void UstudyGameInstance::CollectCarriedGoldFromPlayers(UWorld* World)
{
	if (!World)
	{
		return;
	}

	UstudyGameInstance* GI = Cast<UstudyGameInstance>(World->GetGameInstance());
	if (!GI)
	{
		return;
	}

	// 单机：只处理 0 号玩家
	AstudyCharacter* Character = Cast<AstudyCharacter>(UGameplayStatics::GetPlayerCharacter(World, 0));
	if (!Character)
	{
		return;
	}

	const float Carried = Character->GetCarriedGoldGrams();
	if (Carried <= 0.f)
	{
		return;
	}

	GI->StashedGoldGrams += Carried;
	Character->UnloadAll();		// 清空背包（不然数值会重复计）

	UE_LOG(LogTemp, Log, TEXT("[Stash] 离开矿洞：背包里的 %.1f g 一并带走，累计 %.1f g"),
		Carried, GI->StashedGoldGrams);
}
