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
