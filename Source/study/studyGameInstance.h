// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "studyGameInstance.generated.h"

/**
 * 跨关卡持久的数据。
 *
 * 为什么需要它：营地（Camp）和矿洞（Mine1）是两个关卡，用 OpenLevel 切过去时
 * GameMode / GameState / Pawn 全部会被销毁重建 —— 「第几天、几点、多少钱」如果
 * 存在 GameState 里，一传送就归零了。GameInstance 是整个进程唯一的，所以放这里。
 */
UCLASS()
class STUDY_API UstudyGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	// ---------------- 时间 ----------------
	// 约定：1 游戏小时 = RealSecondsPerGameHour 真实秒（默认 60 秒）
	// 于是 1 真实秒 = 1 游戏分钟，8:00 → 18:00 正好 10 分钟。

	/** 第几天，从 1 开始 */
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	int32 DayIndex = 1;

	/** 游戏内时间，单位：分钟。480 = 8:00，1080 = 18:00 */
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	float ClockMinutes = 480.f;

	/** 今天已经买了几次开采权（每天上限 MaxMineRightsPerDay） */
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	int32 RightsBoughtToday = 0;

	/** 本次下矿还剩多少【真实秒】。<= 0 表示不在矿里 */
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	float MineTimeLeft = 0.f;

	/** 这次开采按游戏时钟几点收工（给 HUD 显示"开采至 HH:MM"） */
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	float MineStopClockMinutes = 0.f;

	// ---------------- 资产 ----------------

	/** 现金（币）。只有卖金才有 */
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	int32 Cash = 0;

	/** 还没卖掉的金重（g） */
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	float GoldGrams = 0.f;

	/**
	 * 矿洞里"暂存点"收下的金重（g）。
	 * 玩家回到营地时才一次性加进 GoldGrams 并弹提示。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	float StashedGoldGrams = 0.f;

	/** 一局是否已经结束（第二天 18:00 结算之后置 true，防止重复弹） */
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	bool bRunFinished = false;

	/** 这一局是否已经初始化过（BeginPlay 用它判断要不要 ResetRun） */
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	bool bRunStarted = false;

	// ---------------- 常量 ----------------

	/** 一天从几点开始（8:00） */
	static constexpr float DayStartMinutes = 8.f * 60.f;

	/** 白天结束（20:00）。到点后时间停住，玩家必须去床上睡。
	 *  8:00 → 20:00 = 12 小时；配合 RealSecondsPerGameHour = 60，正好 12 真实分钟。 */
	static constexpr float DayEndMinutes = 20.f * 60.f;

	/** 每天最多买几次开采权 */
	static constexpr int32 MaxMineRightsPerDay = 3;

	/** 1 游戏小时 = 多少真实秒（60 → 1 真实秒 = 1 游戏分钟） */
	static constexpr float RealSecondsPerGameHour = 60.f;

	/** 一局总共几天（现在是两天） */
	static constexpr int32 TotalDays = 2;

	/** 一次开采能挖多久（真实秒） */
	static constexpr float MineDurationSeconds = 180.f;

	/** 开局启动资金（币）。够买一次开采权，之后要靠卖金赚 */
	static constexpr int32 StartingCash = 4000;

	// ---------------- 操作 ----------------

	/** 重开一局：天数、时间、钱、金重全部重置 */
	void ResetRun();

	/** 睡觉：进入下一天。时间回到 8:00，今天购买次数清零 */
	void SleepToNextDay();

	/** 把时间钳制在 [8:00, 18:00] */
	void ClampClock();

	/** 当前时间是不是已经到 18:00 */
	bool IsDayOver() const { return ClockMinutes >= DayEndMinutes - 0.01f; }

	/** 现在是第几天（1 起） */
	int32 GetDayIndex() const { return DayIndex; }
};
