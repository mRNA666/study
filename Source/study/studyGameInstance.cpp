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
	BuyerQuotaGrams = BuyerQuotaPerDay;
	CampNoticeText.Reset();
	bRunFinished = false;

	// 重开一局 → 开场主菜单重新弹一次
	bMainMenuDone = false;

	// 对手也要重新开局
	InitRivals();

	// 工艺品货架也重刷
	ShelfDay = 0;
	RefillShelfIfNeeded();

	UE_LOG(LogTemp, Log, TEXT("[Run] 新的一局开始（%d 天）"), TotalDays);
}

void UstudyGameInstance::SleepToNextDay()
{
	DayIndex++;
	ClockMinutes = DayStartMinutes;
	RightsBoughtToday = 0;
	BuyerQuotaGrams = BuyerQuotaPerDay;		// 正规买家第二天又有配额

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

	RivalClock += DeltaSeconds;

	// 黑市价随时间恢复（每秒 0.0008 → 跌 6% 大约 75 秒回满）
	BlackMarketMultiplier = FMath::Min(1.f, BlackMarketMultiplier + DeltaSeconds * 0.0008f);

	for (int32 i = 0; i < Rivals.Num(); ++i)
	{
		FRivalState& R = Rivals[i];
		if (RivalClock < R.NextActionAt)
		{
			continue;
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
				if (R.Style == 2 && Grams < 250.f) { continue; }	// 疤脸：只买大件
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

				R.NextActionAt = RivalClock + FMath::FRandRange(60.f, 150.f);
				continue;
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

			R.NextActionAt = RivalClock + FMath::FRandRange(40.f, 90.f);
			continue;
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
			continue;
		}

		// ---- 路径二：买开采权 → 下矿 → 卖 ----
		const int32 RightPrice = 4000;
		if (R.RightsToday < MaxMineRightsPerDay && R.Cash >= RightPrice)
		{
			R.Cash -= RightPrice;
			R.RightsToday++;
			R.TripsDone++;

			// 一次下矿的产量（按性格给不同区间）
			float Gained = 0.f;
			switch (R.Style)
			{
			case 1:  Gained = FMath::FRandRange(8.f, 18.f);  break;	// 独眼：波动最大
			case 2:  Gained = FMath::FRandRange(7.f, 12.f);  break;	// 疤脸：偏高但慢
			default: Gained = FMath::FRandRange(6.f, 10.f);  break;	// 铁头：平稳
			}
			R.StashGrams += Gained;

			// 疤脸先囤货，攒到 20g 才出手
			const bool bHold = (R.Style == 2 && R.StashGrams < 20.f);
			if (!bHold)
			{
				const float Sold = R.StashGrams;
				// 独眼走黑市（65%，还会砸价）；另外两个按正规价（100%）
				const float Rate = (R.Style == 1)
					? (0.65f * BlackMarketMultiplier)
					: 1.0f;
				R.Cash += FMath::RoundToInt(Sold * 1000.f * Rate);
				R.StashGrams = 0.f;

				if (R.Style == 1)
				{
					ApplyBlackMarketDump(Sold);
				}
				UE_LOG(LogTemp, Log, TEXT("[Rival] %s 卖了 %.1f g（x%.2f）→ 现金 %d"),
					*R.Name, Sold, Rate, R.Cash);
			}

			// 下一次行动的间隔（性格不同）
			const float Gap = (R.Style == 1)
				? FMath::FRandRange(120.f, 200.f)
				: (R.Style == 2 ? FMath::FRandRange(220.f, 340.f) : FMath::FRandRange(200.f, 320.f));
			R.NextActionAt = RivalClock + Gap;
			continue;
		}

		// ---- 路径三：没钱，等一会儿再说 ----
		R.NextActionAt = RivalClock + FMath::FRandRange(30.f, 60.f);
	}
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
		{ TEXT("金锁"),   10.f,  40.f },
		{ TEXT("金面具"), 20.f,  60.f },
		{ TEXT("金蛋"),   20.f,  60.f },
		{ TEXT("金蟾"),   50.f, 120.f },
		{ TEXT("金杯"),   60.f, 150.f },
		{ TEXT("金盾"),   80.f, 200.f },
		{ TEXT("金灯"),   80.f, 200.f },
		{ TEXT("金像"),  250.f, 600.f },
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

int32 UstudyGameInstance::SumCraftValue(const TArray<FHandicraftItem>& Items)
{
	int32 Total = 0;
	for (const FHandicraftItem& It : Items)
	{
		Total += GetRealValue(It);
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
