// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "studyGameState.generated.h"

/**
 * 一局游戏的阶段。
 *
 * 为什么放在 GameState 而不是 GameMode：
 *   GameMode 只存在于服务器（客户端根本没有这个对象），
 *   GameState 会被复制到每个客户端。
 * 现在单机看不出区别，但 HUD 要读这个值 —— 将来联机时 HUD 跑在客户端，
 * 读 GameMode 永远是空，读 GameState 才拿得到。
 */
UENUM(BlueprintType)
enum class EGamePhase : uint8
{
	WaitingToStart	UMETA(DisplayName = "准备中"),
	InProgress		UMETA(DisplayName = "进行中"),
	Finished		UMETA(DisplayName = "已结束")
};

/**
 * 一局比赛的共享状态：现在什么阶段、还剩多少秒。
 * 只存"所有人都该知道的数据"，规则和流程放在 GameMode。
 */
UCLASS()
class STUDY_API AstudyGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	/** 当前阶段 */
	UPROPERTY(ReplicatedUsing = OnRep_Phase, BlueprintReadOnly, Category = "Match")
	EGamePhase Phase = EGamePhase::WaitingToStart;

	/** 距离结束还剩多少秒（只在 InProgress 阶段有意义） */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	float TimeRemaining = 0.f;

	/** 第几天（从 1 开始）。跨关卡的真值在 GameInstance 里，这里只是给 HUD 看的副本 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	int32 DayIndex = 1;

	/** 游戏内时间，单位分钟。480 = 8:00，1080 = 18:00 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	float ClockMinutes = 480.f;

	/** 结算时的最终现金（含未卖金重的折算） */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	int32 FinalCash = 0;

	UFUNCTION(BlueprintPure, Category = "Match")
	int32 GetDayIndex() const { return DayIndex; }

	UFUNCTION(BlueprintPure, Category = "Match")
	float GetClockMinutes() const { return ClockMinutes; }

	UFUNCTION(BlueprintPure, Category = "Match")
	int32 GetFinalCash() const { return FinalCash; }

	/** 服务器调用：更新天数与时钟 */
	void SetClock(float InMinutes, int32 InDay);

	/** 服务器调用：结算分数 */
	void SetFinalCash(int32 InCash) { FinalCash = InCash; }

	/** 黑市收购价系数（好感度"熟人"档 65%，见 PRD §2.9） */
	static constexpr float BlackMarketRate = 0.65f;

	// ---------------- 回营地提示 ----------------

	/** 这一趟从矿洞带回来多少克黄金（0 = 不显示） */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	float NoticeGoldGrams = 0.f;

	/** 提示还剩几秒 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	float NoticeTimeLeft = 0.f;

	float GetNoticeGoldGrams() const { return NoticeGoldGrams; }
	float GetNoticeTimeLeft() const { return NoticeTimeLeft; }

	/** 服务器调用：弹一条"本趟带回 xx g"的提示 */
	void ShowGoldNotice(float Grams, float Seconds);

	/** 服务器每帧调用：把提示的剩余时间往下扣 */
	void TickNotice(float DeltaSeconds);

	/**
	 * 每个玩家已交回的黄金克数。索引 = 玩家序号。
	 * 现在单机只有索引 0；联机时改成按 PlayerState 映射。
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	TArray<float> PlayerGoldGrams;

	/** 一共卸了几趟货 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	int32 UnloadCount = 0;

	/** 读某个玩家已交回的黄金（索引越界返回 0） */
	UFUNCTION(BlueprintPure, Category = "Match")
	float GetGoldGrams(int32 PlayerIndex) const;

	/** 一共卸了几趟货（结算面板显示用） */
	int32 GetUnloadCount() const { return UnloadCount; }

	/**
	 * 每个玩家的现金（币）。索引 = 玩家序号；现在单机只有 0。
	 * 注意：现金**不会**在卸货时自动产生 —— 必须把金重卖给商人（见 SellAllGold）。
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	TArray<int32> PlayerCash;

	/** 读某个玩家的现金（索引越界返回 0） */
	UFUNCTION(BlueprintPure, Category = "Match")
	int32 GetCash(int32 PlayerIndex) const;

	/** 花现金：够就扣掉并返回 true，不够返回 false 不扣 */
	bool SpendCash(int32 PlayerIndex, int32 Amount);

	/**
	 * 把某个玩家的全部金重按 PricePerGram 卖掉（PRD §2.9 的成交公式）。
	 * @param PricePerGram  实际成交单价 = 正规基准价 × 好感度系数 × 市场乘数
	 * @return 这次得到的现金（没有金可卖返回 0）
	 */
	int32 SellAllGold(int32 PlayerIndex, int32 PricePerGram);

	/** 只在服务器调用：卸货时**只加金重**，不产生现金 */
	void AddGoldGrams(int32 PlayerIndex, float Grams);

	/** 正规基准价：1 g 黄金 = 1000 币（PRD §2.9 成交公式里的"正规基准价"） */
	static constexpr int32 BasePricePerGram = 1000;

	/** 读阶段。HUD / Character 用这个，不要直接碰成员变量 */
	UFUNCTION(BlueprintPure, Category = "Match")
	EGamePhase GetPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "Match")
	float GetTimeRemaining() const { return TimeRemaining; }

	/** 只在服务器调用：切阶段 */
	void SetPhase(EGamePhase NewPhase);

	/** 只在服务器调用：更新剩余时间 */
	void SetTimeRemaining(float NewRemaining);

protected:
	/** 阶段变化时的回调。服务器手动调一次，客户端由复制触发 —— 两边行为一致 */
	UFUNCTION()
	void OnRep_Phase();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
