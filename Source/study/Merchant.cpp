// Fill out your copyright notice in the Description page of Project Settings.


#include "Merchant.h"
#include "studyCharacter.h"
#include "studyGameInstance.h"
#include "studyGameState.h"
#include "studyGameMode.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	UstudyGameInstance* GetRun(class UObject* Context)
	{
		return Context ? Cast<UstudyGameInstance>(UGameplayStatics::GetGameInstance(Context)) : nullptr;
	}
}

AMerchant::AMerchant()
{
	PrimaryActorTick.bCanEverTick = false;

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);

	// 挡路用：玩家不能从商人身体里穿过去
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
}

void AMerchant::BeginPlay()
{
	Super::BeginPlay();

	// 白色机器人模型（UE 自带）
	if (Body && !Body->GetSkeletalMeshAsset() && !bHideBody)
	{
		if (USkeletalMesh* Manny = LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple")))
		{
			Body->SetSkeletalMeshAsset(Manny);
		}
	}

	// 床这种自己另有外观的，把机器人藏掉
	if (Body && bHideBody)
	{
		Body->SetVisibility(false);
	}
}

bool AMerchant::IsInRange(const AActor* Other) const
{
	if (!Other)
	{
		return false;
	}

	return FVector::DistSquared(Other->GetActorLocation(), GetActorLocation())
		<= FMath::Square(InteractRadiusCm);
}

bool AMerchant::Interact(AstudyCharacter* Character)
{
	if (!Character || !IsInRange(Character))
	{
		return false;
	}

	Character->SetDialogMerchant(this);
	bBuyFailed = false;
	BuildDialog(Character);
	return true;
}

// ---------------- 对话内容 ----------------

void AMerchant::BuildDialog(AstudyCharacter* Character)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	// ---- 床：睡到下一天（晚上才用，所以不参与"太晚了"封锁） ----
	if (Kind == EMerchantKind::Bed)
	{
		Character->SetDialogNumberEnabled(0.f);

		const bool bLastDay = (GI->DayIndex >= UstudyGameInstance::TotalDays);

		TArray<FString> Options;
		Options.Add(bLastDay
			? TEXT("睡觉 —— 收工结算")
			: FString::Printf(TEXT("睡觉，进入第 %d 天"), GI->DayIndex + 1));
		Options.Add(TEXT("再等等"));

		Character->OpenOptionDialog(
			DisplayName,
			bLastDay
				? TEXT("今天是最后一天了，睡下去这一局就结束。")
				: FString::Printf(TEXT("第 %d 天要过去了，睡一觉天就亮了。"), GI->DayIndex),
			Options);
		return;
	}

	// ---- 20:00 之后，所有商人都收摊 ----
	if (GI->IsDayOver())
	{
		Character->SetDialogNumberEnabled(0.f);

		TArray<FString> Options;
		Options.Add(TEXT("知道了"));

		Character->OpenOptionDialog(DisplayName, TEXT("今天太晚了，明天再来吧。"), Options);
		return;
	}

	// ---- 正规买家：全额收，每天有配额 ----
	if (Kind == EMerchantKind::LegitBuyer)
	{
		const float Sellable = FMath::Min(GI->GoldGrams, GI->BuyerQuotaGrams);
		const int32 Estimate = FMath::RoundToInt(Sellable * static_cast<float>(AstudyGameState::BasePricePerGram));

		Character->SetDialogNumberEnabled(Sellable);

		TArray<FString> Options;
		Options.Add(FString::Printf(TEXT("全部卖掉（%.1f g → %d 币，全额）"), Sellable, Estimate));
		Options.Add(TEXT("算了"));

		Character->OpenOptionDialog(
			DisplayName,
			FString::Printf(TEXT("正规价：100%%（不压价）\n今天还能收：%.1f g\n你手上有：%.1f g\n现金：%d 币"),
				GI->BuyerQuotaGrams, GI->GoldGrams, GI->Cash),
			Options);
		return;
	}

	// ---- 矿洞向导 ----
	if (Kind == EMerchantKind::MineGuide)
	{
		Character->SetDialogNumberEnabled(0.f);

		FString GuideBody = TEXT("要提前上去吗？还是再挖一会儿。");
		if (bStashRevealed)
		{
			GuideBody += FString::Printf(TEXT("\n\n这里已经存放：%.1f g 黄金"), GI->StashedGoldGrams);
			GuideBody += TEXT("\n回到营地会自动结算。");
		}

		TArray<FString> Options;
		Options.Add(TEXT("我要提前返回营地"));
		Options.Add(TEXT("这里已经放了多少黄金？"));

		Character->OpenOptionDialog(DisplayName, GuideBody, Options);
		return;
	}

	// ---- 收黄金 ----
	if (Kind == EMerchantKind::SellGold)
	{
		const int32 Estimate = FMath::RoundToInt(
			GI->GoldGrams * AstudyGameState::BlackMarketRate * static_cast<float>(AstudyGameState::BasePricePerGram));

		// 打开行内数字输入（框里填"卖多少克"）
		Character->SetDialogNumberEnabled(GI->GoldGrams);

		TArray<FString> Options;
		Options.Add(FString::Printf(TEXT("全部卖掉（%.1f g → 约 %d 币）"), GI->GoldGrams, Estimate));
		Options.Add(TEXT("算了"));

		Character->OpenOptionDialog(
			DisplayName,
			FString::Printf(TEXT("手上的金重：%.1f g\n黑市价：正规价的 65%%\n现金：%d 币"),
				GI->GoldGrams, GI->Cash),
			Options);
		return;
	}

	// ---- 卖开采权（没有数字输入） ----
	Character->SetDialogNumberEnabled(0.f);
	const bool bSoldOut = (GI->RightsBoughtToday >= UstudyGameInstance::MaxMineRightsPerDay);

	TArray<FString> Options;
	if (bSoldOut)
	{
		Options.Add(TEXT("今天的开采权卖完了，明天再来"));
		Options.Add(TEXT("知道了"));
	}
	else
	{
		Options.Add(FString::Printf(TEXT("买开采权（%d 币）"), MineRightPrice));
		Options.Add(TEXT("不买了"));
	}

	// 钱不够的提示写在正文最前面，一眼能看到
	FString BuyBody;
	if (bBuyFailed)
	{
		const int32 Short_ = FMath::Max(MineRightPrice - GI->Cash, 0);
		BuyBody = FString::Printf(TEXT("⚠ 金钱不够！要 %d 币，你只有 %d 币（还差 %d）\n\n"),
			MineRightPrice, GI->Cash, Short_);
	}

	BuyBody += FString::Printf(TEXT("现金：%d 币\n今天已买 %d / %d 次\n一次能挖 3 分钟，到点自动送回营地"),
		GI->Cash, GI->RightsBoughtToday, UstudyGameInstance::MaxMineRightsPerDay);

	Character->OpenOptionDialog(DisplayName, BuyBody, Options);
}

void AMerchant::OnDialogOption(AstudyCharacter* Character, int32 Index)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	// ---- 床 ----
	if (Kind == EMerchantKind::Bed)
	{
		if (Index == 0)
		{
			// 最后一天：睡觉 = 直接收工结算，不进第 3 天
			if (GI->DayIndex >= UstudyGameInstance::TotalDays)
			{
				if (AstudyGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AstudyGameMode>() : nullptr)
				{
					UE_LOG(LogTemp, Log, TEXT("[Merchant] 最后一天睡觉 → 结算"));
					GM->ForceSettle();
				}
				return;
			}

			GI->SleepToNextDay();
			UE_LOG(LogTemp, Log, TEXT("[Merchant] 睡到第 %d 天"), GI->DayIndex);
			UGameplayStatics::OpenLevel(this, FName(TEXT("Camp")));
		}
		return;
	}

	// ---- 正规买家 ----
	if (Kind == EMerchantKind::LegitBuyer)
	{
		if (Index == 0)
		{
			SellGoldLegit(FMath::Min(GI->GoldGrams, GI->BuyerQuotaGrams));
			BuildDialog(Character);
		}
		return;
	}

	// 20:00 之后所有对话只有"知道了"，点了不做任何事
	if (GI->IsDayOver())
	{
		return;
	}

	// ---- 矿洞向导 ----
	if (Kind == EMerchantKind::MineGuide)
	{
		if (Index == 0)
		{
			// 提前返回营地：清掉开采倒计时，免得刚回去又被强制传送
			GI->MineTimeLeft = 0.f;
			GI->MineStopClockMinutes = 0.f;

			UE_LOG(LogTemp, Log, TEXT("[Merchant] %s：提前返回营地"), *DisplayName);

			// 走之前把背包里没交的黄金一起带走
			UstudyGameInstance::CollectCarriedGoldFromPlayers(GetWorld());

			UGameplayStatics::OpenLevel(this, FName(TEXT("Camp")));
		}
		else if (Index == 1)
		{
			bStashRevealed = true;
			BuildDialog(Character);
		}
		return;
	}

	// ---- 收黄金 ----
	if (Kind == EMerchantKind::SellGold)
	{
		if (Index == 0)
		{
			SellGold(GI->GoldGrams);
			// 卖完重刷对话框，让"手上的金重"立刻反映新数值
			BuildDialog(Character);
		}
		return;
	}

	// ---- 卖开采权 ----
	if (Index != 0)
	{
		return;
	}

	if (GI->RightsBoughtToday >= UstudyGameInstance::MaxMineRightsPerDay)
	{
		UE_LOG(LogTemp, Log, TEXT("[Merchant] %s：今天的开采权已经卖完了"), *DisplayName);
		return;
	}

	// 钱不够：标一下，让对话框弹提示
	if (!BuyMineRight())
	{
		bBuyFailed = true;
		BuildDialog(Character);
	}
}

void AMerchant::OnDialogNumber(AstudyCharacter* Character, float Value)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI)
	{
		return;
	}

	if (Kind == EMerchantKind::SellGold)
	{
		// 输入比存量还大 → 直接按存量算（不用报错，玩家想卖完就该卖完）
		SellGold(FMath::Clamp(Value, 0.f, GI->GoldGrams));
	}
	else if (Kind == EMerchantKind::LegitBuyer)
	{
		SellGoldLegit(FMath::Clamp(Value, 0.f, FMath::Min(GI->GoldGrams, GI->BuyerQuotaGrams)));
	}
	else
	{
		return;
	}

	// 重刷对话框（金重 / 现金 / 可卖数量都变了）
	if (Character && Character->IsDialogOpen())
	{
		BuildDialog(Character);
	}
}

// ---------------- 实际逻辑 ----------------

void AMerchant::SellGold(float Grams)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || Grams <= 0.f)
	{
		return;
	}

	const float Sold = FMath::Min(Grams, GI->GoldGrams);
	if (Sold <= 0.f)
	{
		return;
	}

	const int32 Earned = FMath::RoundToInt(
		Sold * AstudyGameState::BlackMarketRate * static_cast<float>(AstudyGameState::BasePricePerGram));

	GI->GoldGrams -= Sold;
	GI->Cash += Earned;

	UE_LOG(LogTemp, Log, TEXT("[Merchant] %s 收走 %.1f g（黑市价 65%%）→ +%d 币；剩 %.1f g，现金 %d"),
		*DisplayName, Sold, Earned, GI->GoldGrams, GI->Cash);
}

void AMerchant::SellGoldLegit(float Grams)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || Grams <= 0.f)
	{
		return;
	}

	// 三个上限取最小：想卖的、手上的、今天剩下的配额
	const float Sold = FMath::Min(Grams, FMath::Min(GI->GoldGrams, GI->BuyerQuotaGrams));
	if (Sold <= 0.f)
	{
		UE_LOG(LogTemp, Log, TEXT("[Merchant] %s：今天收不了更多了（配额剩 %.1f g）"), *DisplayName, GI->BuyerQuotaGrams);
		return;
	}

	const int32 Earned = FMath::RoundToInt(Sold * static_cast<float>(AstudyGameState::BasePricePerGram));

	GI->GoldGrams -= Sold;
	GI->BuyerQuotaGrams -= Sold;
	GI->Cash += Earned;

	UE_LOG(LogTemp, Log, TEXT("[Merchant] %s 全额收走 %.1f g → +%d 币；配额剩 %.1f g，现金 %d"),
		*DisplayName, Sold, Earned, GI->BuyerQuotaGrams, GI->Cash);
}

bool AMerchant::BuyMineRight()
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI)
	{
		return false;
	}

	if (GI->Cash < MineRightPrice)
	{
		UE_LOG(LogTemp, Log, TEXT("[Merchant] %s：金钱不够（要 %d，只有 %d）"), *DisplayName, MineRightPrice, GI->Cash);
		return false;
	}

	GI->Cash -= MineRightPrice;
	GI->RightsBoughtToday++;

	// 一次开采 3 分钟（真实时间）。
	// 换算成游戏分钟必须跟着流速走：游戏分钟 = 真实秒 × (60 / RealSecondsPerGameHour)。
	// 写死 +180 只在"1 真实秒 = 1 游戏分钟"时才对，改流速就全错了。
	const float GameMinutesPerSecond = 60.f / FMath::Max(UstudyGameInstance::RealSecondsPerGameHour, 1.f);

	GI->MineTimeLeft = UstudyGameInstance::MineDurationSeconds;

	// 收工时间不能超过当天收工点（20:00）—— 否则会显示成"26:16"这种不存在的钟点
	GI->MineStopClockMinutes = FMath::Min(
		GI->ClockMinutes + UstudyGameInstance::MineDurationSeconds * GameMinutesPerSecond,
		UstudyGameInstance::DayEndMinutes);

	const int32 StartH = FMath::FloorToInt(GI->ClockMinutes / 60.f);
	const int32 StartM = FMath::FloorToInt(GI->ClockMinutes) % 60;
	const int32 StopH = FMath::FloorToInt(GI->MineStopClockMinutes / 60.f);
	const int32 StopM = FMath::FloorToInt(GI->MineStopClockMinutes) % 60;

	UE_LOG(LogTemp, Log, TEXT("[Merchant] %s：开采权买到（今天第 %d/%d 次，花了 %d 币），%02d:%02d 采到 %02d:%02d"),
		*DisplayName, GI->RightsBoughtToday, UstudyGameInstance::MaxMineRightsPerDay, MineRightPrice,
		StartH, StartM, StopH, StopM);

	UGameplayStatics::OpenLevel(this, FName(TEXT("Mine1")));
	return true;
}
