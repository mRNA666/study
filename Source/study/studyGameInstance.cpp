// Fill out your copyright notice in the Description page of Project Settings.


#include "studyGameInstance.h"

void UstudyGameInstance::ResetRun()
{
	DayIndex = 1;
	ClockMinutes = DayStartMinutes;
	RightsBoughtToday = 0;
	Cash = 0;
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
