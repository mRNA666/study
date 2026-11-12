// Fill out your copyright notice in the Description page of Project Settings.


#include "Merchant.h"
#include "studyCharacter.h"
#include "studyGameInstance.h"
#include "studyGameState.h"
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
	if (Body && !Body->GetSkeletalMeshAsset())
	{
		if (USkeletalMesh* Manny = LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple")))
		{
			Body->SetSkeletalMeshAsset(Manny);
		}
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

	// ---- 收黄金 ----
	if (Kind == EMerchantKind::SellGold)
	{
		const int32 Estimate = FMath::RoundToInt(
			GI->GoldGrams * AstudyGameState::BlackMarketRate * static_cast<float>(AstudyGameState::BasePricePerGram));

		TArray<FString> Options;
		Options.Add(FString::Printf(TEXT("全部卖掉（%.1f g → 约 %d 币）"), GI->GoldGrams, Estimate));
		Options.Add(TEXT("只卖一部分（自己输入克数）"));
		Options.Add(TEXT("算了"));

		Character->OpenOptionDialog(
			DisplayName,
			FString::Printf(TEXT("手上的金重：%.1f g\n黑市价：正规价的 65%%\n现金：%d 币"),
				GI->GoldGrams, GI->Cash),
			Options);
		return;
	}

	// ---- 卖开采权 ----
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

	Character->OpenOptionDialog(
		DisplayName,
		FString::Printf(TEXT("现金：%d 币\n今天已买 %d / %d 次\n一次能挖 3 分钟，到点自动送回营地"),
			GI->Cash, GI->RightsBoughtToday, UstudyGameInstance::MaxMineRightsPerDay),
		Options);
}

void AMerchant::OnDialogOption(AstudyCharacter* Character, int32 Index)
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI || !Character)
	{
		return;
	}

	// ---- 收黄金 ----
	if (Kind == EMerchantKind::SellGold)
	{
		if (Index == 0)
		{
			SellGold(GI->GoldGrams);
		}
		else if (Index == 1)
		{
			if (GI->GoldGrams <= 0.f)
			{
				UE_LOG(LogTemp, Log, TEXT("[Merchant] %s：你手上没有金重"), *DisplayName);
				return;
			}

			Character->SetDialogMerchant(this);
			Character->OpenNumberDialog(
				DisplayName,
				FString::Printf(TEXT("手上的金重：%.1f g"), GI->GoldGrams),
				TEXT("卖出多少克？"),
				GI->GoldGrams);
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

	BuyMineRight();
}

void AMerchant::OnDialogNumber(AstudyCharacter* Character, float Value)
{
	if (Kind == EMerchantKind::SellGold)
	{
		SellGold(Value);
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

void AMerchant::BuyMineRight()
{
	UstudyGameInstance* GI = GetRun(this);
	if (!GI)
	{
		return;
	}

	if (GI->Cash < MineRightPrice)
	{
		UE_LOG(LogTemp, Log, TEXT("[Merchant] %s：开采权要 %d 币，你只有 %d 币"), *DisplayName, MineRightPrice, GI->Cash);
		return;
	}

	GI->Cash -= MineRightPrice;
	GI->RightsBoughtToday++;

	// 一次开采 3 分钟。
	// 时间换算：1 真实秒 = 1 游戏分钟 → 180 秒 = 180 游戏分钟 = 3 小时。
	// 所以这里直接加 180 分钟，不要再除以 60。
	GI->MineTimeLeft = UstudyGameInstance::MineDurationSeconds;
	GI->MineStopClockMinutes = GI->ClockMinutes + UstudyGameInstance::MineDurationSeconds;

	const int32 StartH = FMath::FloorToInt(GI->ClockMinutes / 60.f);
	const int32 StartM = FMath::FloorToInt(GI->ClockMinutes) % 60;
	const int32 StopH = FMath::FloorToInt(GI->MineStopClockMinutes / 60.f);
	const int32 StopM = FMath::FloorToInt(GI->MineStopClockMinutes) % 60;

	UE_LOG(LogTemp, Log, TEXT("[Merchant] %s：开采权买到（今天第 %d/%d 次，花了 %d 币），%02d:%02d 采到 %02d:%02d"),
		*DisplayName, GI->RightsBoughtToday, UstudyGameInstance::MaxMineRightsPerDay, MineRightPrice,
		StartH, StartM, StopH, StopM);

	UGameplayStatics::OpenLevel(this, FName(TEXT("Mine1")));
}
