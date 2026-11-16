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
 * 8 个工艺品品类（用现有 8 个模型命名）
 */
enum class EHandicraftCategory : uint8
{
	GoldLock = 0,	// 金锁
	GoldMask,		// 金面具
	GoldEgg,		// 金蛋
	GoldToad,		// 金蟾
	GoldGoblet,		// 金杯
	GoldShield,		// 金盾
	GoldLamp,		// 金灯
	GoldStatue,		// 金像
	Count
};

/** 一件工艺品 */
USTRUCT(BlueprintType)
struct FHandicraftItem
{
	GENERATED_BODY()

	/** 0~7，对应 EHandicraftCategory */
	UPROPERTY(BlueprintReadOnly, Category = "Handicraft")
	int32 Category = 0;

	/** 标称金重（g） */
	UPROPERTY(BlueprintReadOnly, Category = "Handicraft")
	float NominalGrams = 0.f;

	/** 真实含金量 0~1（玩家看不到，除非花 500 鉴定） */
	UPROPERTY(BlueprintReadOnly, Category = "Handicraft")
	float RealPurity = 0.f;

	/** 黑商报的含金量 0~1（可能吹高） */
	UPROPERTY(BlueprintReadOnly, Category = "Handicraft")
	float DeclaredPurity = 0.f;

	/** 买入价（币） */
	UPROPERTY(BlueprintReadOnly, Category = "Handicraft")
	int32 Price = 0;

	/** 是否已经花 500 鉴定过 */
	UPROPERTY(BlueprintReadOnly, Category = "Handicraft")
	bool bAppraised = false;

	/** 老板话术 */
	UPROPERTY(BlueprintReadOnly, Category = "Handicraft")
	FString SalesPitch;
};

/** 货架上的一格（含"被谁买走了"） */
USTRUCT(BlueprintType)
struct FShelfSlot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Handicraft")
	FHandicraftItem Item;

	UPROPERTY(BlueprintReadOnly, Category = "Handicraft")
	bool bSold = false;

	UPROPERTY(BlueprintReadOnly, Category = "Handicraft")
	FString BuyerName;
};

/**
 * 一个「模拟对手」的状态。
 *
 * 它不是一个真的 AI 角色 —— 没有寻路、没有模型，只是一套跟着游戏时钟跑的小状态机。
 * 这样排行榜上会有人跟你抢，但不用为寻路/AI 控制器再花两天。
 *
 * 放在 FHandicraftItem 之后：对手也会从黑商柜台买工艺品，得把买到的货存起来。
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

	/** 他手上囤着的工艺品（买了就压着，卖正规买家能拿真实价值） */
	UPROPERTY(BlueprintReadOnly, Category = "Rival")
	TArray<FHandicraftItem> Handicraft;

	/** 资产口径：现金 + 囤着的金按正规价折算（工艺品另算，见 UstudyGameInstance::SumCraftValue） */
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

	// ---------------- 黑社会的放数（黑社会） ----------------

	/**
	 * 当前欠款 = 本金 + 累计利息 − 已还。
	 * 每天醒来滚一次：LoanBalance ×= (1 + LoanDailyRate)
	 * —— 也就是"利滚利"，不还就是每天多 3%。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Loan")
	float LoanBalance = 0.f;

	/** 日息 10%（利滚利，借一天不还就多一成） */
	static constexpr float LoanDailyRate = 0.10f;

	/** 欠款上限（含已经滚出来的利息）。没有上限的话借钱=白送钱，经济会被刷爆 */
	static constexpr int32 LoanLimit = 20000;

	bool HasLoan() const { return LoanBalance >= 1.f; }

	/** 四舍五入后的欠款（界面显示 / 还款判定用） */
	int32 GetLoanOwed() const { return FMath::RoundToInt(LoanBalance); }

	/** 今天不还，明天会多欠这么多（界面上给玩家看） */
	int32 GetLoanDailyInterest() const { return FMath::RoundToInt(LoanBalance * LoanDailyRate); }

	/** 还能再借多少 */
	int32 GetLoanRoom() const { return FMath::Max(0, LoanLimit - FMath::RoundToInt(LoanBalance)); }

	/** 借钱（进现金）。返回 false = 金额不合法或超过额度 */
	bool TakeLoan(int32 Amount);

	/** 还钱（自动夹到"现金"和"欠款"之间）。返回 false = 一分钱都没还成 */
	bool RepayLoan(int32 Amount);

	/** 换天时滚利息（SleepToNextDay 里调一次） */
	void ApplyDailyInterest();

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

	// ---------------- 工艺品 ----------------

	/** 当天的 8 件货 */
	UPROPERTY(BlueprintReadOnly, Category = "Handicraft")
	TArray<FShelfSlot> Shelf;

	/** 货架属于第几天（!= DayIndex 就重刷） */
	UPROPERTY(BlueprintReadOnly, Category = "Handicraft")
	int32 ShelfDay = 0;

	/** 玩家买下的工艺品（按 B 看） */
	UPROPERTY(BlueprintReadOnly, Category = "Handicraft")
	TArray<FHandicraftItem> HandicraftBag;

	/**
	 * 黑市每天的基础收购价（0.55~0.75 每天刷新）。
	 * 实际单价 = 1000 x 这个值 x BlackMarketMultiplier（抛售砸价的那个乘数）。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Market")
	float BlackMarketDailyRate = 0.65f;

	/** 黑商好感度 0~100：每卖给他 5 g 黄金 +1，被识破 -15 */
	UPROPERTY(BlueprintReadOnly, Category = "Handicraft")
	int32 FenceFavor = 60;

	// ---------------- 掺假（PRD §2.9 / D22） ----------------

	/**
	 * 黑商今天的严格度 0~1。每天重掷，第 1 天恒为 0（新手保护）。
	 * 只用来算抽查概率，**永远不给玩家看数字**，只给一句口语提示（见 GetStrictnessHint）。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Market")
	float FenceStrictness = 0.f;

	/** 黄铜单价（币/g）：黑商从柜台底下摸出来卖给你的 */
	static constexpr int32 BrassPricePerGram = 40;

	/** 掺 BrassGrams 克黄铜的"被抓概率"（掂量 + 抽查合一，0~1） */
	float GetAdulterationCatchChance(float BrassGrams) const;

	/** 同上，但按"这一笔要卖多少克"算 —— 卖一部分时概率按这一包算，不是按全部库存 */
	float GetAdulterationCatchChanceEx(float SellGrams, float BrassGrams) const;

	/** 风险口语提示：很可能发现不了 / 可能被抓 / 这很容易被抓 / 这一掂量就能分出来 */
	FString GetAdulterationHint(float BrassGrams) const;

	/** 同上，按这一笔算 */
	FString GetAdulterationHintEx(float SellGrams, float BrassGrams) const;

	/** 严格度的口语提示（他今天称得松 / 就那样 / 手挺勤 / 盯着你看） */
	FString GetStrictnessHint() const;

	/** 好感度 → 收购价系数（PRD 四档 68/65/58/50 除以 65） */
	float GetFavorFactor() const;

	/** 好感度档位名（老主顾 / 熟人 / 生面孔 / 麻烦精） */
	FString GetFavorLabel() const;

	/** 该档位对应的收购价（68 / 65 / 58 / 50，正规价的百分比） */
	int32 GetFavorPercent() const;

	/** 今天黑市真正到手的单价 = 1000 × 当天基础价 × 砸价 × 好感 */
	float GetFencePricePerGram() const;

	/** 换天时重掷严格度（第 1 天恒 0） */
	void RollDailyStrictness();

	// ---------------- 每日播报 ----------------

	/** 今天发生过的事（一条一行，换天时清空） */
	UPROPERTY(BlueprintReadOnly, Category = "Report")
	TArray<FString> TodayFeed;

	/** 睡醒后要弹的播报；GameMode 读完就清空 */
	UPROPERTY(BlueprintReadOnly, Category = "Report")
	FString PendingReport;

	/** 往当天事件流里塞一行 */
	void PushFeed(const FString& Line);

	/** 换天时调：把今天的事 + 名次变化拼成播报，清空事件流。必须在 DayIndex++ 之前调 */
	void ComposeMorningReport();

	/** 我的总资产：现金 + （口袋/仓库/暂存）金重 × 正规价 + 工艺品真实价值 */
	int32 GetMyAssetTotal() const;

	/** 第 Index 个对手的总资产（现金 + 囤金 + 手上工艺品） */
	int32 GetRivalAssetTotal(int32 Index) const;

	/** 我现在第几名（1 起） */
	int32 GetMyRank() const;

	/** 上一次收工时我第几名（0 = 还没播过） */
	UPROPERTY(BlueprintReadOnly, Category = "Report")
	int32 RankYesterday = 0;

	/** 正规买家第一次交货不抽检（PRD 新手保护） */
	UPROPERTY(BlueprintReadOnly, Category = "Market")
	bool bBuyerFirstPass = true;

	/** 今天被正规买家抽到过：本次窗口不再收你的货 */
	UPROPERTY(BlueprintReadOnly, Category = "Market")
	bool bBuyerRejectedToday = false;

	/** 换天/首次进入时刷货架（ShelfDay == DayIndex 时直接返回） */
	void RefillShelfIfNeeded();

	/** 品类名（日志/UI 用） */
	static FString GetCategoryName(int32 Category);

	/**
	 * 生成一张货架（纯函数，不依赖世界；测试走同一个入口）。
	 * MarketRate = 当天黑市基础价 —— 货架的卖价是按它算的，所以必须先定金价、再刷货架。
	 */
	static void GenerateShelf(int32 InDay, int32 Seed, TArray<FShelfSlot>& OutShelf, float MarketRate = 0.65f);

	/** 一件货的真实可变现（按正规全价算）——真金白银卖出去用的就是这个 */
	static int32 GetRealValue(const FHandicraftItem& Item);

	/**
	 * 一件货的「账面资产」（Tab / 每日播报用）。
	 * 和 GetRealValue 的区别：**没鉴定过就按黑商报的含金量估**，鉴定过才按真实。
	 * 为什么：你手里只有黑商那个数字，用真实值会让播报替你"免费鉴宝"。
	 */
	static int32 GetCraftAssetValue(const FHandicraftItem& Item);

	/** 一堆工艺品的账面资产合计（对手资产、背包估值用） */
	static int32 SumCraftValue(const TArray<FHandicraftItem>& Items);

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

	/** 让某个对手做一次行动。返回 true = 这次真的做成了什么（调用方据此决定要不要重排时间） */
	bool StepRivalAction(FRivalState& R);

	/**
	 * 玩家提前睡觉、跳过今天剩下的白天时调用：把这段时间里对手本该做的事补上。
	 * 不补的话，"睡觉跳天"会把对手的时间冻结 —— 那是免费的时间机器。
	 */
	void CatchUpRivals(float ElapsedGameMinutes);

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

	/** 一局总共几天（PRD §2.3：一季 12 个游戏日） */
	static constexpr int32 TotalDays = 12;

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
