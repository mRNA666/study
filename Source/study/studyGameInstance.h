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
/**
 * 一个「模拟对手」的状态。
 *
 * 它不是一个真的 AI 角色 —— 没有寻路、没有模型，只是一套跟着游戏时钟跑的小状态机。
 * 这样排行榜上会有人跟你抢，但不用为寻路/AI 控制器再花两天。
 */
USTRUCT(BlueprintType)
struct FRivalState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Rival")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Rival")
	int32 Cash = 0;

	/** 挖出来还没卖的黄金（囤货） */
	UPROPERTY(BlueprintReadOnly, Category = "Rival")
	float StashGrams = 0.f;

	/** 今天已经用掉几次开采权（和玩家一样，上限 3 次） */
	UPROPERTY(BlueprintReadOnly, Category = "Rival")
	int32 RightsToday = 0;

	/** 下一次行动的时钟（秒，RivalClock 的绝对值）。到点才动 */
	UPROPERTY(BlueprintReadOnly, Category = "Rival")
	float NextActionAt = 0.f;

	/** 性格：0 = 稳（等正规买家 100%），1 = 赌（立刻卖黑市 65%），2 = 囤（攒够再卖） */
	UPROPERTY(BlueprintReadOnly, Category = "Rival")
	int32 Style = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Rival")
	int32 TripsDone = 0;

	/** 资产口径：现金 + 囤着的金按正规价折算（和玩家同一把尺子） */
	int32 GetAssetTotal() const
	{
		return Cash + FMath::RoundToInt(StashGrams * 1000.f);
	}
};

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

	/** 正规买家今天还能收多少克（每天刷新） */
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	float BuyerQuotaGrams = 3000.f;

	/** 回到营地时要在屏幕上弹的一句话（空 = 不弹） */
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	FString CampNoticeText;

	/** 一局是否已经结束（第二天 18:00 结算之后置 true，防止重复弹） */
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	bool bRunFinished = false;

	/** 这一局是否已经初始化过（BeginPlay 用它判断要不要 ResetRun） */
	UPROPERTY(BlueprintReadOnly, Category = "Run")
	bool bRunStarted = false;

	// ---------------- UI 设置（跨关卡保留） ----------------

	/** 主菜单是否已经点过「开始游戏」。按 R 重开会重置成 false */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	bool bMainMenuDone = false;

	/** 主音量 0~1（ESC 菜单里拖滑块改） */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	float MasterVolume = 0.7f;

	/** 头灯亮度倍数：1 = 默认亮度（HeadLightIntensity） */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	float HeadLightScale = 1.f;

	// ---------------- 模拟对手（AI 矿工） ----------------

	UPROPERTY(BlueprintReadOnly, Category = "Rivals")
	TArray<FRivalState> Rivals;

	/** 对手自己的时钟（秒）。只在这局开始后走 */
	UPROPERTY(BlueprintReadOnly, Category = "Rivals")
	float RivalClock = 0.f;

	/** 上一次同步的开采次数是哪一天（换天时把 RightsToday 清零） */
	UPROPERTY(BlueprintReadOnly, Category = "Rivals")
	int32 RivalLastDay = 1;

	/**
	 * 黑市价乘数。任何人（玩家或对手）向黑市大量抛售都会把它砸下来，
	 * 然后随时间慢慢恢复。1.0 = 原价。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Market")
	float BlackMarketMultiplier = 1.f;

	/** 新建三个对手，并给它们不同的出发时间（不会同时动） */
	void InitRivals();

	/** 每帧推进：换天重置、黑市价恢复、到点让对手行动。由 GameMode 调 */
	void TickRivals(float DeltaSeconds);

	/** 抛售 Grams 克：把黑市价砸下去一点（有地板） */
	void ApplyBlackMarketDump(float Grams);

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

	/** 正规买家每场能收多少克足金（PRD §2.9） */
	static constexpr float BuyerQuotaPerDay = 3000.f;

	/** 20:00 还在矿洞里，向导来接你的路费 */
	static constexpr int32 LatePickupFee = 2000;

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

	/**
	 * 切关卡之前调用：把玩家【背包里还没交的】黄金收进 StashedGoldGrams。
	 * 不这么做的话，没去暂存点就跑的人会一点都带不回来。
	 */
	static void CollectCarriedGoldFromPlayers(UWorld* World);
};
