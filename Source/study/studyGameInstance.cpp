// Fill out your copyright notice in the Description page of Project Settings.


#include "studyGameInstance.h"
#include "studyCharacter.h"
#include "studyGameState.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

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
	BuyerQuotaGrams = BuyerQuotaPerSeason;
	CampNoticeText.Reset();
	bRunFinished = false;

	// 重开一局 → 开场主菜单重新弹一次
	bMainMenuDone = false;

	// 掺假 / 播报相关状态
	FenceFavor = 60;
	FenceStrictness = 0.f;
	TodayFeed.Reset();
	PendingReport.Reset();
	RankYesterday = 0;
	bBuyerFirstPass = true;
	bBuyerRejectedToday = false;

	// 贷款的欠款也要清零（不然重开一局还背着上一局的债）
	LoanBalance = 0.f;

	// 赛季统计
	MyTrips = 0;
	RouletteProfit = 0;

	// 对手也要重新开局
	InitRivals();

	// 工艺品货架也重刷
	ShelfDay = 0;
	RefillShelfIfNeeded();

	UE_LOG(LogTemp, Log, TEXT("[Run] 新的一局开始（%d 天）"), TotalDays);
}

void UstudyGameInstance::SleepToNextDay()
{
	// 先补跑对手：你提前睡觉 = 放弃了今天剩下的白天，对手在这段时间里照样干活。
	// （放在播报之前，这样补出来的事件会出现在今天的简报里）
	const float SkippedDayMinutes = FMath::Max(DayEndMinutes - ClockMinutes, 0.f);
	CatchUpRivals(SkippedDayMinutes);

	// 黑社会的利息也在播报之前结算：这样早上那份简报里的"资产"已经扣掉新利息，
	// 简报的事件列表里也会有"利息 +X"这一条
	ApplyDailyInterest();

	// 先播报：这时候 DayIndex 还是"刚结束的那天"
	ComposeMorningReport();

	DayIndex++;
	ClockMinutes = DayStartMinutes;
	RightsBoughtToday = 0;

	// 正规买家的额度是"一季一个池子"，不是每天重刷：只减不增。
	// 唯一例外是池子见底（不足 300 g）时，窗口开的这天补到 300 ——
	// 否则季末会剩个几十克，窗口等于关着，玩家白等两天。
	if (DayIndex % 2 == 0 && BuyerQuotaGrams < BuyerQuotaMinPerWindow)
	{
		BuyerQuotaGrams = BuyerQuotaMinPerWindow;
	}
	bBuyerRejectedToday = false;
	RollDailyStrictness();

	UE_LOG(LogTemp, Log, TEXT("[Run] 睡到第 %d 天 08:00（黑商严格度 %.2f，补跑 %.0f 游戏分钟）"),
		DayIndex, FenceStrictness, SkippedDayMinutes);
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

void UstudyGameInstance::InitRivals()
{
	Rivals.Reset();
	RivalClock = 0.f;
	RivalLastDay = DayIndex;
	BlackMarketMultiplier = 1.f;
	BlackMarketDailyRate = FMath::FRandRange(0.55f, 0.75f);

	// 三个对手：名字 / 性格 / 起始现金
	struct FRivalSeed { const TCHAR* Name; int32 Style; int32 Cash; };
	const FRivalSeed Seeds[] =
	{
		{ TEXT("铁头"), 0, 5000 },	// 稳：卖正规买家，产出平稳
		{ TEXT("独眼"), 1, 4000 },	// 赌：卖黑市，产出波动最大
		{ TEXT("疤脸"), 2, 6000 },	// 囤：攒够一堆再卖
	};

	for (int32 i = 0; i < UE_ARRAY_COUNT(Seeds); ++i)
	{
		FRivalState R;
		R.Name = Seeds[i].Name;
		R.Style = Seeds[i].Style;
		R.Cash = Seeds[i].Cash;
		// 第一次行动时间故意错开：不会三个一起动
		R.NextActionAt = FMath::FRandRange(8.f, 40.f) + i * 15.f;
		Rivals.Add(R);
	}

	UE_LOG(LogTemp, Log, TEXT("[Rival] 三个对手已就位"));
}

void UstudyGameInstance::ApplyBlackMarketDump(float Grams)
{
	if (Grams <= 0.f)
	{
		return;
	}

	// 卖 15g → 价格 -6%；地板 60%，防止几个人一起砸到底
	BlackMarketMultiplier = FMath::Clamp(BlackMarketMultiplier - Grams * 0.004f, 0.60f, 1.f);
}

void UstudyGameInstance::TickRivals(float DeltaSeconds)
{
	if (!bRunStarted || bRunFinished || Rivals.Num() == 0)
	{
		return;
	}

	// 换天：开采次数清零
	if (DayIndex != RivalLastDay)
	{
		RivalLastDay = DayIndex;
		for (FRivalState& R : Rivals)
		{
			R.RightsToday = 0;
		}
	}

	// 关键：对手的时钟跑【游戏分钟】，不是真实秒。
	// 以前是 RivalClock += DeltaSeconds（真实秒），于是"玩家睡觉跳天"时真实时间只过去 1.5 秒，
	// 对手一整天一次也动不了 —— 睡觉变成了白嫖一天的时间机器。
	const float GameMinutesPerSecond =
		60.f / FMath::Max(RealSecondsPerGameHour, 1.f);
	RivalClock += DeltaSeconds * GameMinutesPerSecond;

	// 黑市价随时间恢复（每秒 0.0012 → 跌 6% 大约 50 秒回满）
	BlackMarketMultiplier = FMath::Min(1.f, BlackMarketMultiplier + DeltaSeconds * 0.0012f);

	for (int32 i = 0; i < Rivals.Num(); ++i)
	{
		FRivalState& R = Rivals[i];
		if (RivalClock < R.NextActionAt)
		{
			continue;
		}

		if (!StepRivalAction(R))
		{
			// 什么都没做成（没钱/没货），过一会儿再看
			R.NextActionAt = RivalClock + FMath::FRandRange(30.f, 60.f);
		}
	}
}

bool UstudyGameInstance::StepRivalAction(FRivalState& R)
{
	if (!Shelf.Num() && R.Handicraft.Num() == 0 && R.Cash < 1600)
	{
		// 完全没有可做的事：留给调用方去设置等待时间
		return false;
	}

	// ---- 路径零：25% 概率先去柜台看看（抢货） ----
	// 关键：他真的掏钱买，而且只花得起自己现金的 40%。
	// 开局现金 4~6 千，柜台上动辄几万的东西他根本不会碰 —— 不会出现
	// "刚开局就有人把 500 g 的金像抱走"这种乱套的事。
	if (FMath::FRand() < 0.25f && Shelf.Num() > 0)
	{
		const int32 Budget = FMath::RoundToInt(R.Cash * 0.4f);

		TArray<int32> Pickable;
		for (int32 s = 0; s < Shelf.Num(); ++s)
		{
			if (Shelf[s].bSold)
			{
				continue;
			}
			if (Shelf[s].Item.Price > Budget)
			{
				continue;			// 买不起，看看就走
			}

			const float Grams = Shelf[s].Item.NominalGrams;
			if (R.Style == 0 && Grams > 60.f) { continue; }		// 铁头：只买便宜货
			// 疤脸：只买大件。阈值原来是 250 g，但 v3.21 把工艺品标称重量整体 ÷4 之后，
			// 最重的金像才 62~150 g —— 250 这个数让他一辈子买不到东西，等于这条支线死了。
			if (R.Style == 2 && Grams < 60.f) { continue; }
			Pickable.Add(s);
		}

		if (Pickable.Num() > 0)
		{
			const int32 Choice = Pickable[FMath::RandRange(0, Pickable.Num() - 1)];
			FShelfSlot& Bought = Shelf[Choice];

			R.Cash -= Bought.Item.Price;
			Bought.bSold = true;
			Bought.BuyerName = R.Name;

			// 买到的货压在他手上，算进资产（卖出去才变成现金，见下面"手头紧"那条）
			R.Handicraft.Add(Bought.Item);

			UE_LOG(LogTemp, Log, TEXT("[Shelf] %s 花了 %d 币买走 %s（现金剩 %d，手上工艺品 %d 件）"),
				*R.Name, Bought.Item.Price,
				*UstudyGameInstance::GetCategoryName(Bought.Item.Category),
				R.Cash, R.Handicraft.Num());

			PushFeed(FString::Printf(TEXT("%s 花 %d 币把%s抱走了"),
				*R.Name, Bought.Item.Price,
				*UstudyGameInstance::GetCategoryName(Bought.Item.Category)));

			R.NextActionAt = RivalClock + FMath::FRandRange(60.f, 150.f);
			return true;
		}
	}

	// ---- 路径零半：手头紧了就把压着的工艺品卖给正规买家（必验，按真实价值给钱） ----
	if (R.Cash < 3000 && R.Handicraft.Num() > 0)
	{
		const FHandicraftItem SoldIt = R.Handicraft[0];
		const int32 Earned = UstudyGameInstance::GetRealValue(SoldIt);

		R.Cash += Earned;
		R.Handicraft.RemoveAt(0);

		UE_LOG(LogTemp, Log, TEXT("[Rival] %s 把 %s 卖给正规买家 → %d 币（现金 %d）"),
			*R.Name, *UstudyGameInstance::GetCategoryName(SoldIt.Category), Earned, R.Cash);

		PushFeed(FString::Printf(TEXT("%s 把%s出手了"), *R.Name,
			*UstudyGameInstance::GetCategoryName(SoldIt.Category)));

		R.NextActionAt = RivalClock + FMath::FRandRange(40.f, 90.f);
		return true;
	}

	// ---- 路径一：赌工艺品（小概率，现金够才赌；PRD 2.9 第一天就开门） ----
	if (R.Cash >= 1600 && FMath::FRand() < 0.12f)
	{
		const float Stake = 1500.f;
		R.Cash -= static_cast<int32>(Stake);
		// 55% 是真金：赚 80%；否则本金打水漂
		if (FMath::FRand() < 0.55f)
		{
			R.Cash += static_cast<int32>(Stake * 1.8f);
		}
		R.NextActionAt = RivalClock + FMath::FRandRange(90.f, 200.f);
		return true;
	}

	// ---- 路径一·五：去轮盘赌一把（负 EV，但能制造波动，也给播报添戏） ----
	if (R.Cash >= 2000 && FMath::FRand() < 0.12f)
	{
		// 下注比例按性格：铁头 10%（保守）/ 独眼 30%（赌徒）/ 疤脸 15%
		const float Frac = (R.Style == 1) ? 0.30f : (R.Style == 2 ? 0.15f : 0.10f);
		const int32 Bet = FMath::Clamp(FMath::RoundToInt(R.Cash * Frac), 200, 5000);

		// 押什么：独眼偶尔押绿（博 20 倍），另外两个只押红黑
		const int32 Color = (R.Style == 1 && FMath::FRand() < 0.25f) ? 2 : FMath::RandRange(0, 1);

		const int32 Pocket = RollRoulettePocket();
		const int32 PocketColor = RouletteColorOf(Pocket);

		R.Cash -= Bet;
		R.RoulettePlays++;

		int32 Won = 0;
		if (PocketColor == Color)
		{
			Won = Bet * (Color == 2 ? RouletteGreenMultiplier : 2);
			R.Cash += Won;
		}

		R.RouletteProfit += (Won - Bet);

		PushFeed(Won > 0
			? FString::Printf(TEXT("%s 在轮盘押%s %d 币，中了 %d 币"), *R.Name, *RouletteColorText(Color), Bet, Won)
			: FString::Printf(TEXT("%s 在轮盘押%s %d 币，输了"), *R.Name, *RouletteColorText(Color), Bet));

		UE_LOG(LogTemp, Log, TEXT("[Rival] %s 轮盘押%s %d → 摇出 %d（%s）%s %d 币"),
			*R.Name, *RouletteColorText(Color), Bet, Pocket, *RouletteColorText(PocketColor),
			Won > 0 ? TEXT("中") : TEXT("没中"), Won > 0 ? Won : Bet);

		R.NextActionAt = RivalClock + FMath::FRandRange(40.f, 120.f);
		return true;
	}

	// ---- 路径二：买开采权 → 下矿 → 卖 ----
	const int32 RightPrice = 4000;
	if (R.RightsToday < MaxMineRightsPerDay && R.Cash >= RightPrice)
	{
		R.Cash -= RightPrice;
		R.RightsToday++;
		R.TripsDone++;

		// 一次下矿的产量（按性格给不同区间）
		// v3.24：整体上调到"一季 500~700 g"，玩家一季约 1500~2200 g，
		// 这样他们靠 100% 的正规价才追得上玩家的量，排行榜才有压力。
		float Gained = 0.f;
		switch (R.Style)
		{
		case 1:  Gained = FMath::FRandRange(22.f, 35.f);  break;	// 独眼：波动最大
		case 2:  Gained = FMath::FRandRange(26.f, 30.f);  break;	// 疤脸：偏高但慢
		default: Gained = FMath::FRandRange(25.f, 29.f);  break;	// 铁头：平稳
		}
		R.StashGrams += Gained;

		// 卖不卖、卖给谁、能不能抢到窗口，全在 SellRivalStash 里
		SellRivalStash(R);
		RivalRepayIfAble(R);

		// 下一次行动的间隔（性格不同）
		const float Gap = (R.Style == 1)
			? FMath::FRandRange(120.f, 200.f)
			: (R.Style == 2 ? FMath::FRandRange(220.f, 340.f) : FMath::FRandRange(200.f, 320.f));
		R.NextActionAt = RivalClock + Gap;
		return true;
	}

	// ---- 路径三：连开采权都买不起了，就看手上囤的货能不能变现 ----
	if (R.StashGrams > 0.01f)
	{
		const float Before = R.StashGrams;
		SellRivalStash(R);
		RivalRepayIfAble(R);
		if (R.StashGrams < Before - 0.01f)
		{
			R.NextActionAt = RivalClock + FMath::FRandRange(40.f, 120.f);
			return true;
		}
	}

	// ---- 路径四：货也卖不掉、钱又不够 —— 去找黑社会借 ----
	// 和玩家同一个黑社会、同一个日息、同一个额度上限。
	// 没有这一条，"抢不到窗口的人"就会卡死在原地（疤脸之前就是这么僵住的）。
	if (R.Cash < RightPrice && R.RightsToday < MaxMineRightsPerDay)
	{
		const int32 Room = LoanLimit - R.GetLoanOwed();
		const int32 Amount = FMath::Min(6000, Room);		// 够买一两趟开采权 + 一点周转
		if (Amount >= 1000)
		{
			R.Cash += Amount;
			R.LoanBalance += static_cast<float>(Amount);

			UE_LOG(LogTemp, Log, TEXT("[Rival] %s 找黑社会借了 %d 币（现在欠 %d）→ 现金 %d"),
				*R.Name, Amount, R.GetLoanOwed(), R.Cash);
			PushFeed(FString::Printf(TEXT("%s 找黑社会借了 %d 币"), *R.Name, Amount));

			R.NextActionAt = RivalClock + FMath::FRandRange(60.f, 140.f);
			return true;
		}
	}

	// 什么都不成（没钱、没货，或者窗口还没开），等一会儿再说
	return false;
}

void UstudyGameInstance::RivalRepayIfAble(FRivalState& R)
{
	// 日息 10% 拖着不还，比亏本还狠 —— 所以手头一宽裕，他也会先还债。
	// 留 4000 现金当开采权的本钱，别把自己还成穷光蛋。
	if (R.LoanBalance <= 0.f || R.Cash <= 4000)
	{
		return;
	}

	const int32 Pay = FMath::Min(R.Cash - 4000, R.GetLoanOwed());
	if (Pay < 500)
	{
		return;
	}

	R.Cash -= Pay;
	R.LoanBalance = FMath::Max(0.f, R.LoanBalance - static_cast<float>(Pay));

	UE_LOG(LogTemp, Log, TEXT("[Rival] %s 还了 %d 币 → 还欠 %d，现金 %d"),
		*R.Name, Pay, R.GetLoanOwed(), R.Cash);
	PushFeed(FString::Printf(TEXT("%s 还了黑社会 %d 币"), *R.Name, Pay));
}

void UstudyGameInstance::SellRivalStash(FRivalState& R)
{
	if (R.StashGrams <= 0.01f)
	{
		return;
	}

	// ---- 独眼：只认黑市。不问窗口、不等价钱，卖了就砸 ----
	if (R.Style == 1)
	{
		const float Sold = R.StashGrams;
		// 他没有好感度系统，固定 0.65 折；同样受黑市硬地板约束（MinFencePrice）
		const float Unit = FMath::Max(650.f * BlackMarketMultiplier, MinFencePrice);
		R.Cash += FMath::RoundToInt(Sold * Unit);
		R.StashGrams = 0.f;
		ApplyBlackMarketDump(Sold);

		UE_LOG(LogTemp, Log, TEXT("[Rival] %s 卖了 %.1f g（黑市 %.0f 币/g）→ 现金 %d"),
			*R.Name, Sold, Unit, R.Cash);
		PushFeed(FString::Printf(TEXT("%s 卖了 %.1f g"), *R.Name, Sold));
		return;
	}

	// ---- 疤脸：非攒够一堆不出手 ----
	// 但"囤"有个前提：他还买得起开采权。否则会死锁 ——
	// stash 不到 40 g 不肯卖、又没钱下矿，就永远卡在原地，资产再也不动。
	// （实测：第 10 天起他的钱卡在 3 万上下，播报里一条他的消息都没有。）
	if (R.Style == 2 && R.StashGrams < 40.f && R.Cash >= 4000)
	{
		return;
	}

	// ---- 铁头/疤脸：等正规买家的窗口（双数天），而且和玩家抢同一个池子 ----
	// 谁先行动谁先舀 —— 所以他完全可能在你到柜台之前就把货交了。
	if ((DayIndex % 2 == 0) && BuyerQuotaGrams > 0.5f)
	{
		const float Sold = FMath::Min(R.StashGrams, BuyerQuotaGrams);
		BuyerQuotaGrams = FMath::Max(BuyerQuotaGrams - Sold, 0.f);
		R.StashGrams -= Sold;
		R.Cash += FMath::RoundToInt(Sold * 1000.f);

		// 卖给正规买家也一样砸黑市价 —— 这条对四个人是同一条规则。
		// 四条线一起往市场里倒金子，黑市价才会被真正压下去。
		ApplyBlackMarketDump(Sold);

		UE_LOG(LogTemp, Log, TEXT("[Rival] %s 抢在窗口交了 %.1f g（全额）→ 现金 %d，池子剩 %.1f g"),
			*R.Name, Sold, R.Cash, BuyerQuotaGrams);
		PushFeed(FString::Printf(TEXT("%s 抢先交给正规买家 %.0f g（窗口还剩 %.0f g）"),
			*R.Name, Sold, BuyerQuotaGrams));
		return;
	}

	// 窗口没开、或者池子被别人舀空了。
	// 手上还有钱就继续囤着等下一个窗口；连开采权都买不起，才去黑市贱卖。
	if (R.Cash < 4000)		// 4000 = 路径二里的开采权价格
	{
		const float Sold = R.StashGrams;
		const float Unit = FMath::Max(650.f * BlackMarketMultiplier, MinFencePrice);
		R.Cash += FMath::RoundToInt(Sold * Unit);
		R.StashGrams = 0.f;
		ApplyBlackMarketDump(Sold);

		UE_LOG(LogTemp, Log, TEXT("[Rival] %s 等不到窗口，只能按黑市价卖 %.1f g（%.0f 币/g）→ 现金 %d"),
			*R.Name, Sold, Unit, R.Cash);
		PushFeed(FString::Printf(TEXT("%s 等不到窗口，把 %.0f g 贱卖了"), *R.Name, Sold));
	}
}

void UstudyGameInstance::CatchUpRivals(float ElapsedGameMinutes)
{
	if (ElapsedGameMinutes <= 1.f || Rivals.Num() == 0)
	{
		return;
	}

	// 玩家提前睡觉 = 放弃了今天剩下的白天。这段时间对手照样在干活，
	// 否则"睡觉跳天"就能把对手的时间冻结（还能白嫖货架和日价的刷新）。
	// 上限 8 次，和正常玩一天的行动次数同量级，防止睡觉刷出天文数字。
	constexpr int32 MaxActionsPerRival = 8;

	int32 TotalActions = 0;
	for (FRivalState& R : Rivals)
	{
		for (int32 n = 0; n < MaxActionsPerRival; ++n)
		{
			if (!StepRivalAction(R))
			{
				break;
			}
			++TotalActions;
		}
	}

	// 跳过的时间也要记进对手时钟，否则他们的节奏会一直落后
	RivalClock += ElapsedGameMinutes;

	// 新一天的第一次行动错开，别所有人同一帧一起动
	for (FRivalState& R : Rivals)
	{
		R.NextActionAt = FMath::Max(R.NextActionAt, RivalClock + FMath::FRandRange(10.f, 60.f));
	}

	UE_LOG(LogTemp, Log, TEXT("[Rival] 玩家跳过了 %.0f 游戏分钟，补跑 %d 次对手行动"),
		ElapsedGameMinutes, TotalActions);
}


// ==================== 工艺品 ====================

namespace
{
	/** 每个品类的标称金重区间 */
	struct FCategoryRange
	{
		const TCHAR* Name;
		float MinGrams;
		float MaxGrams;
	};

	const FCategoryRange GCategoryRanges[] =
	{
		// 【v3.21】整体 ÷4：原来金像是 250~600 g，一件卖 9 万多币，
		// 而玩家一天净赚 1~2 万 —— 8 件里 5 件整局都买不起，柜台等于只有 3 件。
		// 缩到 1/4 后：小件（金锁 3~10 g）第 1 天就买得起，大件（金像 62~150 g）天然
		// 留到第 2 天 —— 不需要再按天数硬锁，现金自己就是门槛。
		{ TEXT("金锁"),    3.f,  10.f },
		{ TEXT("金面具"),  5.f,  15.f },
		{ TEXT("金蛋"),    5.f,  15.f },
		{ TEXT("金蟾"),   12.f,  30.f },
		{ TEXT("金杯"),   15.f,  38.f },
		{ TEXT("金盾"),   20.f,  50.f },
		{ TEXT("金灯"),   20.f,  50.f },
		{ TEXT("金像"),   62.f, 150.f },
	};

	const TCHAR* GPitches[] =
	{
		TEXT("船上掉的货，不图钱"),
		TEXT("主家急用钱，抵给我的"),
		TEXT("上一任买家退货，我看着不像假的"),
		TEXT("拆了个旧教堂，顺手收的"),
		TEXT("别人拿来换酒钱的，我压了半个月"),
		TEXT("来路你别问，东西是真的"),
	};
}

FString UstudyGameInstance::GetCategoryName(int32 Category)
{
	if (Category < 0 || Category >= UE_ARRAY_COUNT(GCategoryRanges))
	{
		return TEXT("?");
	}
	return GCategoryRanges[Category].Name;
}

int32 UstudyGameInstance::GetRealValue(const FHandicraftItem& Item)
{
	// 正规渠道全价：标称金重 × 真实含金量 × 1000
	return FMath::RoundToInt(Item.NominalGrams * Item.RealPurity * 1000.f);
}

int32 UstudyGameInstance::GetCraftAssetValue(const FHandicraftItem& Item)
{
	// 账面口径：没鉴定过就只有黑商那句报价，按它估；鉴定过才用真实含金量
	const float Purity = Item.bAppraised ? Item.RealPurity : Item.DeclaredPurity;
	return FMath::RoundToInt(Item.NominalGrams * Purity * 1000.f);
}

int32 UstudyGameInstance::SumCraftValue(const TArray<FHandicraftItem>& Items)
{
	int32 Total = 0;
	for (const FHandicraftItem& It : Items)
	{
		Total += GetCraftAssetValue(It);
	}
	return Total;
}

void UstudyGameInstance::GenerateShelf(int32 InDay, int32 Seed, TArray<FShelfSlot>& OutShelf, float MarketRate)
{
	OutShelf.Reset();

	// 固定种子（同一局同一天刷出同一张货架）
	FRandomStream Rng(Seed * 7919 + InDay * 104729 + 1);

	const int32 Count = UE_ARRAY_COUNT(GCategoryRanges);

	// ---- 1) 抽出 8 件的基础数据 ----
	float SumNominal = 0.f;
	float SumRealGrams = 0.f;

	OutShelf.SetNum(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		FHandicraftItem& It = OutShelf[i].Item;
		It.Category = i;
		It.NominalGrams = FMath::RoundToFloat(
			Rng.FRandRange(GCategoryRanges[i].MinGrams, GCategoryRanges[i].MaxGrams));

		// 真实含金量：55% 真金 / 28% 半真 / 17% 掺铜
		const float Roll = Rng.FRand();
		if (Roll < 0.55f)
		{
			It.RealPurity = Rng.FRandRange(0.88f, 1.00f);
		}
		else if (Roll < 0.83f)
		{
			It.RealPurity = Rng.FRandRange(0.45f, 0.80f);
		}
		else
		{
			It.RealPurity = Rng.FRandRange(0.02f, 0.35f);
		}

		It.SalesPitch = GPitches[Rng.RandRange(0, UE_ARRAY_COUNT(GPitches) - 1)];

		SumNominal += It.NominalGrams;
		SumRealGrams += It.RealPurity * It.NominalGrams;
	}

	// ---- 2) 真金配额：Σ(真实×标称) ≤ 0.52 × Σ标称 ----
	const float Quota = 0.52f * SumNominal;
	int32 Guard = 0;
	while (SumRealGrams > Quota && Guard++ < 200)
	{
		// 找当前"真实金重最大"的那件，往掺铜压
		int32 Worst = 0;
		float WorstGrams = -1.f;
		for (int32 i = 0; i < OutShelf.Num(); ++i)
		{
			const float G = OutShelf[i].Item.RealPurity * OutShelf[i].Item.NominalGrams;
			if (G > WorstGrams)
			{
				WorstGrams = G;
				Worst = i;
			}
		}

		FHandicraftItem& It = OutShelf[Worst].Item;
		const float OldGrams = It.RealPurity * It.NominalGrams;
		It.RealPurity = Rng.FRandRange(0.02f, 0.35f);
		SumRealGrams += (It.RealPurity * It.NominalGrams) - OldGrams;
	}

	// ---- 3) 黑商报价：主要往高了吹（他就是靠这个赚差价）----
	//    30% 照实说 / 10% 故意低报（捡漏位）/ 60% 吹 1.2~2.6 倍，上限 100%。
	//    为什么以前是 ±15% 双向：报价和"该卖多少钱"没关系，吹过头也没意义。
	//    现在报价直接决定卖价（见第 4 步），所以报价又必须"像那么回事"。
	for (FShelfSlot& Slot : OutShelf)
	{
		FHandicraftItem& It = Slot.Item;

		const float Roll = Rng.FRand();
		float Ratio = 1.f;
		if (Roll < 0.30f)
		{
			Ratio = 1.f;								// 老实报价
		}
		else if (Roll < 0.40f)
		{
			Ratio = Rng.FRandRange(0.75f, 1.0f);		// 低报：这件是漏
		}
		else
		{
			Ratio = Rng.FRandRange(1.20f, 2.60f);		// 吹
		}

		It.DeclaredPurity = FMath::Clamp(It.RealPurity * Ratio, 0.02f, 1.f);
	}

	// ---- 4) 买入价：报价 x 标称 x 金价 x 加价 ----
	//    玩家看到的价 = "他说的含金量"值多少钱，再加他一笔 5%~30% 的利润。
	//    这样价格和报价是自洽的（价 / 报价金价 ≈ 0.94），不会出现"报 60% 却卖得比 30% 还便宜"。
	for (FShelfSlot& Slot : OutShelf)
	{
		FHandicraftItem& It = Slot.Item;
		const float ClaimValue = It.NominalGrams * It.DeclaredPurity * 1000.f;
		It.Price = FMath::RoundToInt(ClaimValue * MarketRate * Rng.FRandRange(1.05f, 1.30f));
	}

	// ---- 5) 兜底：整架成本 ≥ 1.12 x 整架真实价值 ----
	//    只靠上面的随机加价，遇上"低报的漏特别多"的那天，全买下来转手卖给正规买家就净赚
	//    —— 那就成了无风险套利。这里统一放大，保证全买必亏（PRD D36）。
	//    放大的倍率通常是 0.9~1.3，对单件"看着贵不贵"影响很小，也不会泄露真实含金量。
	float SumRealValue = 0.f;
	float SumPrice = 0.f;
	for (const FShelfSlot& Slot : OutShelf)
	{
		SumRealValue += Slot.Item.NominalGrams * Slot.Item.RealPurity * 1000.f;
		SumPrice += static_cast<float>(Slot.Item.Price);
	}

	const float CostFloor = 1.12f * SumRealValue;
	if (SumPrice > 0.f && SumPrice < CostFloor)
	{
		const float Scale = CostFloor / SumPrice;
		for (FShelfSlot& Slot : OutShelf)
		{
			Slot.Item.Price = FMath::RoundToInt(Slot.Item.Price * Scale);
		}
	}

	// ---- 6) 清空买走状态 ----
	for (FShelfSlot& Slot : OutShelf)
	{
		Slot.bSold = false;
		Slot.BuyerName.Reset();
	}
}

void UstudyGameInstance::RefillShelfIfNeeded()
{
	if (Shelf.Num() > 0 && ShelfDay == DayIndex)
	{
		return;
	}

	// 先定今天的黑市基础价 —— 货架的卖价要按它算，顺序不能反
	BlackMarketDailyRate = FMath::FRandRange(0.55f, 0.75f);

	// 种子：同一天永远同一张货架（存档读档后也一致）
	const int32 Seed = DayIndex * 977 + 12345;
	GenerateShelf(DayIndex, Seed, Shelf, BlackMarketDailyRate);
	ShelfDay = DayIndex;

	UE_LOG(LogTemp, Log, TEXT("[Shelf] 第 %d 天的货架已生成（%d 件），黑市基础价 %.0f%%"),
		DayIndex, Shelf.Num(), BlackMarketDailyRate * 100.f);
}

// ==================== 掺假 / 好感度 / 播报 ====================

float UstudyGameInstance::GetFavorFactor() const
{
	// PRD §2.9 四档收购价 68 / 65 / 58 / 50，除以基准 65 得到乘数
	if (FenceFavor >= 80) { return 1.046f; }	// 老主顾
	if (FenceFavor >= 55) { return 1.000f; }	// 熟人
	if (FenceFavor >= 30) { return 0.892f; }	// 生面孔
	return 0.769f;								// 麻烦精
}

int32 UstudyGameInstance::GetFavorPercent() const
{
	if (FenceFavor >= 80) { return 68; }
	if (FenceFavor >= 55) { return 65; }
	if (FenceFavor >= 30) { return 58; }
	return 50;
}

FString UstudyGameInstance::GetFavorLabel() const
{
	if (FenceFavor >= 80) { return TEXT("老主顾"); }
	if (FenceFavor >= 55) { return TEXT("熟人"); }
	if (FenceFavor >= 30) { return TEXT("生面孔"); }
	return TEXT("麻烦精");
}

float UstudyGameInstance::GetFencePricePerGram() const
{
	// 顺序固定：正规基准价 × 当天基础价 × 抛售砸价 × 好感系数（PRD §2.9 硬约束）
	const float Raw = static_cast<float>(AstudyGameState::BasePricePerGram)
		* BlackMarketDailyRate
		* BlackMarketMultiplier
		* GetFavorFactor();

	// v3.24：硬地板，而且地板本身跟着好感档走 ——
	// 四个人一季往市场里倒两千多克，砸价会长期钉在 0.60，
	// 要是四档拿同一个地板，好感度在后期就等于不存在了。
	return FMath::Max(Raw, GetFenceFloorPrice());
}

float UstudyGameInstance::GetFenceFloorPrice() const
{
	//   老主顾 68% → 460 × 68/65 = 481
	//   熟人   65% → 460（基准）
	//   生面孔 58% → 440（内插，玩家没指定）
	//   麻烦精 50% → 420（玩家指定：按比例本来是 354，和熟人的差得太狠）
	if (FenceFavor >= 80) { return 481.f; }
	if (FenceFavor >= 55) { return 460.f; }
	if (FenceFavor >= 30) { return 440.f; }
	return 420.f;
}

void UstudyGameInstance::RollDailyStrictness()
{
	// 第 1 天永不抽查（PRD 新手保护）
	FenceStrictness = (DayIndex <= 1) ? 0.f : FMath::FRandRange(0.05f, 1.f);
}

FString UstudyGameInstance::GetStrictnessHint() const
{
	if (FenceStrictness < 0.10f) { return TEXT("他今天在打瞌睡，称得松"); }
	if (FenceStrictness < 0.40f) { return TEXT("跟平时一样，称得不算细"); }
	if (FenceStrictness < 0.75f) { return TEXT("他今天手挺勤，翻来覆去地看"); }
	return TEXT("他今天一直盯着秤，眼睛没离开过");
}

FString UstudyGameInstance::GetFenceMoodLine() const
{
	// 分段用的是"实际到手单价"（已经过 460 的硬地板），
	// 所以最后一档就是地板价本身 —— 玩家听到这句话，就知道再等也不会更好了。
	//   ≥720  极热（早期/刚回血）
	//   640~  好价
	//   560~  平常
	//   500~  偏低
	//   <500  地板（460 上下）
	const float Unit = GetFencePricePerGram();

	if (Unit >= 720.f)
	{
		return TEXT("「高价回收，急需黄金。」他把铜盘往前一推，「有多少拿多少来。」");
	}
	if (Unit >= 640.f)
	{
		return TEXT("「今天行情好。」他敲了两下秤沿，「要出手就趁现在。」");
	}
	if (Unit >= 560.f)
	{
		return TEXT("「老价钱。」他连头都没抬，「卖就快点，后面还排着人。」");
	}
	if (Unit >= 500.f)
	{
		return TEXT("「这两天送货的人多。」他掂了掂你的袋子，「价我压一点，你也别嫌。」");
	}
	return TEXT("「就这个价，你爱卖不卖。」他把秤往柜台上一搁，不再看你。");
}

float UstudyGameInstance::GetAdulterationCatchChance(float BrassGrams) const
{
	return GetAdulterationCatchChanceEx(GoldGrams, BrassGrams);
}

float UstudyGameInstance::GetAdulterationCatchChanceEx(float SellGrams, float BrassGrams) const
{
	const float G = FMath::Max(SellGrams, 0.f);
	if (BrassGrams <= 0.f || G <= 0.f)
	{
		return 0.f;
	}

	// 掺入比例 r = 黄铜 / (真实金重 + 黄铜)。他掂的是"这一包"的手感
	const float R = BrassGrams / (G + BrassGrams);

	// ---- 第一层：掂量（必做，免费）----
	// PRD 的锚点：≤15% 掂不出、30% 约四成、>40% 必定被掂出。
	// 用一条连续曲线穿过这三个点，而不是硬切四档 ——
	// 这样"掺 18% 和 22%"的体感是连续的，玩家会自己摸出那条线在哪。
	//   t = (r - 0.15) / 0.25  ，P掂 = t²
	//   r=0.15 → 0     r=0.30 → 0.36     r=0.40 → 1.0
	const float T = FMath::Clamp((R - 0.15f) / 0.25f, 0.f, 1.f);
	const float PWeigh = T * T;

	// ---- 第二层：抽查（按当天严格度）----
	// PRD 原公式：基础概率 = 严格度 × 掺假比例
	const float PSpot = FMath::Clamp(FenceStrictness * R, 0.f, 1.f);

	// 两层是"或"的关系，不能直接相加（否则会超过 1）
	return 1.f - (1.f - PWeigh) * (1.f - PSpot);
}

FString UstudyGameInstance::GetAdulterationHint(float BrassGrams) const
{
	return GetAdulterationHintEx(GoldGrams, BrassGrams);
}

FString UstudyGameInstance::GetAdulterationHintEx(float SellGrams, float BrassGrams) const
{
	const float P = GetAdulterationCatchChanceEx(SellGrams, BrassGrams);

	if (P < 0.10f) { return TEXT("很可能发现不了"); }
	if (P < 0.30f) { return TEXT("可能被抓"); }
	if (P < 0.60f) { return TEXT("这很容易被抓"); }
	return TEXT("这一掂量就能分出来");
}

void UstudyGameInstance::PushFeed(const FString& Line)
{
	if (Line.IsEmpty())
	{
		return;
	}

	// 一天最多留 12 条，免得刷屏
	if (TodayFeed.Num() < 12)
	{
		TodayFeed.Add(Line);
	}
}

int32 UstudyGameInstance::GetMyAssetTotal() const
{
	int32 Total = Cash + FMath::RoundToInt(
		(GoldGrams + StashedGoldGrams) * static_cast<float>(AstudyGameState::BasePricePerGram));
	// 身上背着的金（挖出来还没交回营地）也算资产 ——
	// 不算的话 Tab 和每日播报会差一截，人在矿洞里的时候尤其明显
	if (const UWorld* W = GetWorld())
	{
		if (const APlayerController* PC = W->GetFirstPlayerController())
		{
			if (const AstudyCharacter* C = Cast<AstudyCharacter>(PC->GetPawn()))
			{
				Total += FMath::RoundToInt(
					C->GetCarriedGoldGrams() * static_cast<float>(AstudyGameState::BasePricePerGram));
			}
		}
	}

	// 工艺品按账面价（没鉴定过 = 黑商报的含金量）
	Total += SumCraftValue(HandicraftBag);

	// 欠黑社会的钱是**负债**，必须从资产里扣掉。
	// 不扣的话"借钱"就等于凭空变资产，Tab 排名和赛季结算都会被刷。
	Total -= GetLoanOwed();

	return Total;
}

// ==================== 轮盘（玩家和 AI 共用同一套规则） ====================

int32 UstudyGameInstance::RouletteColorOf(int32 Pocket)
{
	if (Pocket == 0)
	{
		return 2;	// 0 = 绿
	}

	static const int32 Reds[] =
	{
		1, 3, 5, 7, 9, 12, 14, 16, 18,
		19, 21, 23, 25, 27, 30, 32, 34, 36
	};

	for (int32 N : Reds)
	{
		if (N == Pocket)
		{
			return 0;
		}
	}

	return 1;
}

int32 UstudyGameInstance::RollRoulettePocket()
{
	return FMath::RandRange(0, 36);
}

FString UstudyGameInstance::RouletteColorText(int32 Color)
{
	switch (Color)
	{
	case 0:  return TEXT("红");
	case 1:  return TEXT("黑");
	case 2:  return TEXT("绿");
	default: return TEXT("？");
	}
}

// ==================== 黑社会的放数 ====================

bool UstudyGameInstance::TakeLoan(int32 Amount)
{
	if (Amount <= 0)
	{
		return false;
	}

	// 额度按"欠款总额"算（含已经滚出来的利息）
	if (Amount > GetLoanRoom())
	{
		return false;
	}

	Cash += Amount;
	LoanBalance += static_cast<float>(Amount);

	UE_LOG(LogTemp, Log, TEXT("[Loan] 借了 %d 币 → 欠款 %d（现金 %d）"),
		Amount, GetLoanOwed(), Cash);

	PushFeed(FString::Printf(TEXT("你向黑社会借了 %d 币"), Amount));
	return true;
}

bool UstudyGameInstance::RepayLoan(int32 Amount)
{
	if (Amount <= 0 || !HasLoan())
	{
		return false;
	}

	// 三个上限取最小：想还的 / 手上的现金 / 实际欠款
	const int32 Pay = FMath::Min3(Amount, Cash, FMath::CeilToInt(LoanBalance));
	if (Pay <= 0)
	{
		return false;
	}

	Cash -= Pay;
	LoanBalance = FMath::Max(0.f, LoanBalance - static_cast<float>(Pay));

	// 尾差（0.4 币这种）直接抹掉，免得永远差一点点还清
	if (LoanBalance < 1.f)
	{
		LoanBalance = 0.f;
	}

	UE_LOG(LogTemp, Log, TEXT("[Loan] 还了 %d 币 → 欠款 %d（现金 %d）"),
		Pay, GetLoanOwed(), Cash);

	PushFeed(FString::Printf(TEXT("你还了黑社会 %d 币"), Pay));
	return true;
}

void UstudyGameInstance::ApplyDailyInterest()
{
	// 对手的债先滚（他们不看播报，所以只记日志）
	for (FRivalState& R : Rivals)
	{
		if (R.LoanBalance <= 0.f)
		{
			continue;
		}
		const int32 Before = R.GetLoanOwed();
		R.LoanBalance *= (1.f + LoanDailyRate);
		UE_LOG(LogTemp, Log, TEXT("[Rival] %s 的利息 +%d → 欠款 %d"),
			*R.Name, R.GetLoanOwed() - Before, R.GetLoanOwed());
	}

	if (!HasLoan())
	{
		return;
	}

	const float Before = LoanBalance;

	// 利滚利：欠款 ×= 1.03
	LoanBalance *= (1.f + LoanDailyRate);

	const int32 Interest = FMath::RoundToInt(LoanBalance - Before);

	UE_LOG(LogTemp, Log, TEXT("[Loan] 第 %d 天利息 +%d → 欠款 %d"),
		DayIndex, Interest, GetLoanOwed());

	PushFeed(FString::Printf(TEXT("利滚利 +%d 币，现在欠 %d 币"), Interest, GetLoanOwed()));
}

int32 UstudyGameInstance::GetRivalAssetTotal(int32 Index) const
{
	if (!Rivals.IsValidIndex(Index))
	{
		return 0;
	}

	const FRivalState& R = Rivals[Index];
	return R.GetAssetTotal() + SumCraftValue(R.Handicraft);
}

int32 UstudyGameInstance::GetMyRank() const
{
	const int32 Me = GetMyAssetTotal();

	int32 Rank = 1;
	for (int32 i = 0; i < Rivals.Num(); ++i)
	{
		if (GetRivalAssetTotal(i) > Me)
		{
			++Rank;
		}
	}
	return Rank;
}

void UstudyGameInstance::ComposeMorningReport()
{
	const int32 FinishedDay = DayIndex;
	const int32 MyRank = GetMyRank();

	FString Body = FString::Printf(TEXT("第 %d 天 · 收工\n\n"), FinishedDay);

	if (TodayFeed.Num() == 0)
	{
		Body += TEXT("· 今天没什么动静，柜台冷冷清清。\n");
	}
	else
	{
		// 播报面板可以滚，所以不用再砍条数：一天最多留 12 条全给玩家看
		const int32 MaxLines = 12;
		for (int32 i = 0; i < TodayFeed.Num() && i < MaxLines; ++i)
		{
			Body += FString::Printf(TEXT("· %s\n"), *TodayFeed[i]);
		}

		if (TodayFeed.Num() > MaxLines)
		{
			Body += FString::Printf(TEXT("· …还有 %d 条\n"), TodayFeed.Num() - MaxLines);
		}
	}

	// 名次（金额精确是为了自己看着有底，别人的金额在 Tab 里是区间）
	Body += FString::Printf(TEXT("\n资产 %d 币 · 第 %d 名"), GetMyAssetTotal(), MyRank);

	if (RankYesterday > 0)
	{
		if (MyRank < RankYesterday)
		{
			Body += FString::Printf(TEXT("（↑%d）"), RankYesterday - MyRank);
		}
		else if (MyRank > RankYesterday)
		{
			Body += FString::Printf(TEXT("（↓%d）"), MyRank - RankYesterday);
		}
		else
		{
			Body += TEXT("（没变）");
		}
	}

	// 欠黑社会的钱单独提一句：早上看一眼就知道今天有多少压力
	if (HasLoan())
	{
		Body += FString::Printf(TEXT("\n欠黑社会 %d 币（日息 %d%%，明天还会 +%d）"),
			GetLoanOwed(), FMath::RoundToInt(LoanDailyRate * 100.f), GetLoanDailyInterest());
	}

	// 播报是"睡醒之后"看的，所以这里说的是【今天】会发生什么
	const int32 Today = FinishedDay + 1;
	if (Today <= TotalDays)
	{
		Body += FString::Printf(TEXT("\n今天（第 %d 天）："), Today);
		Body += (Today % 2 == 0)
			? TEXT("正规买家开窗口，全场 3000 g 配额；黑市换新货")
			: TEXT("黑市换新货；正规买家明天才来");
	}
	else
	{
		Body += TEXT("\n今天是最后一天了。");
	}

	PendingReport = Body;
	TodayFeed.Reset();
	RankYesterday = MyRank;

	UE_LOG(LogTemp, Log, TEXT("[Report] 第 %d 天收工，我的名次 %d，播报 %d 字"),
		FinishedDay, MyRank, Body.Len());
}
