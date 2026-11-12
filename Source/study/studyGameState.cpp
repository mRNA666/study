// Fill out your copyright notice in the Description page of Project Settings.


#include "studyGameState.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const TCHAR* PhaseName(EGamePhase Phase)
	{
		switch (Phase)
		{
		case EGamePhase::WaitingToStart:	return TEXT("准备中");
		case EGamePhase::InProgress:		return TEXT("进行中");
		case EGamePhase::Finished:			return TEXT("已结束");
		default:							return TEXT("未知");
		}
	}
}

void AstudyGameState::SetPhase(EGamePhase NewPhase)
{
	// 只有服务器能改状态。客户端改会被下一次复制覆盖，所以干脆拒绝
	if (!HasAuthority())
	{
		return;
	}

	if (Phase == NewPhase)
	{
		return;
	}

	Phase = NewPhase;

	// 服务器上不会自动触发 OnRep（那只在复制时触发），手动调一次，
	// 保证"服务器"和"客户端"走同一段逻辑
	OnRep_Phase();
}

void AstudyGameState::SetClock(float InMinutes, int32 InDay)
{
	if (!HasAuthority())
	{
		return;
	}

	ClockMinutes = InMinutes;
	DayIndex = InDay;
}

void AstudyGameState::ShowGoldNotice(float Grams, float Seconds)
{
	if (!HasAuthority())
	{
		return;
	}

	NoticeGoldGrams = Grams;
	NoticeText.Reset();
	NoticeTimeLeft = Seconds;
}

void AstudyGameState::ShowTextNotice(const FString& Text, float Seconds)
{
	if (!HasAuthority())
	{
		return;
	}

	NoticeText = Text;
	NoticeGoldGrams = 0.f;
	NoticeTimeLeft = Seconds;
}

void AstudyGameState::TickNotice(float DeltaSeconds)
{
	if (!HasAuthority() || NoticeTimeLeft <= 0.f)
	{
		return;
	}

	NoticeTimeLeft = FMath::Max(NoticeTimeLeft - DeltaSeconds, 0.f);
}

void AstudyGameState::SetTimeRemaining(float NewRemaining)
{
	if (!HasAuthority())
	{
		return;
	}

	TimeRemaining = FMath::Max(NewRemaining, 0.f);
}

void AstudyGameState::AddGoldGrams(int32 PlayerIndex, float Grams)
{
	if (!HasAuthority() || PlayerIndex < 0 || Grams <= 0.f)
	{
		return;
	}

	if (!PlayerGoldGrams.IsValidIndex(PlayerIndex))
	{
		PlayerGoldGrams.SetNum(PlayerIndex + 1);
	}

	PlayerGoldGrams[PlayerIndex] += Grams;
	UnloadCount++;

	// 注意：这里**只加金重**。现金要等玩家把金卖给商人（SellAllGold）才会产生。
	UE_LOG(LogTemp, Log, TEXT("[Match] 玩家 %d 卸货 +%.1f g，金重累计 %.1f g（第 %d 趟）"),
		PlayerIndex, Grams, PlayerGoldGrams[PlayerIndex], UnloadCount);
}

int32 AstudyGameState::GetCash(int32 PlayerIndex) const
{
	return PlayerCash.IsValidIndex(PlayerIndex) ? PlayerCash[PlayerIndex] : 0;
}

bool AstudyGameState::SpendCash(int32 PlayerIndex, int32 Amount)
{
	if (!HasAuthority() || PlayerIndex < 0 || Amount <= 0)
	{
		return false;
	}

	if (GetCash(PlayerIndex) < Amount)
	{
		return false;
	}

	PlayerCash[PlayerIndex] -= Amount;
	UE_LOG(LogTemp, Log, TEXT("[Match] 玩家 %d 花了 %d 币，剩 %d 币"),
		PlayerIndex, Amount, PlayerCash[PlayerIndex]);
	return true;
}

int32 AstudyGameState::SellAllGold(int32 PlayerIndex, int32 PricePerGram)
{
	if (!HasAuthority() || PlayerIndex < 0 || PricePerGram <= 0)
	{
		return 0;
	}

	if (!PlayerGoldGrams.IsValidIndex(PlayerIndex))
	{
		return 0;
	}

	const float Grams = PlayerGoldGrams[PlayerIndex];
	if (Grams <= 0.f)
	{
		return 0;
	}

	const int32 Earned = FMath::RoundToInt(Grams * static_cast<float>(PricePerGram));

	if (!PlayerCash.IsValidIndex(PlayerIndex))
	{
		PlayerCash.SetNum(PlayerIndex + 1);
	}
	PlayerCash[PlayerIndex] += Earned;
	PlayerGoldGrams[PlayerIndex] = 0.f;

	UE_LOG(LogTemp, Log, TEXT("[Match] 玩家 %d 卖掉 %.1f g（单价 %d）→ +%d 币，现金 %d"),
		PlayerIndex, Grams, PricePerGram, Earned, PlayerCash[PlayerIndex]);
	return Earned;
}

float AstudyGameState::GetGoldGrams(int32 PlayerIndex) const
{
	return PlayerGoldGrams.IsValidIndex(PlayerIndex) ? PlayerGoldGrams[PlayerIndex] : 0.f;
}

void AstudyGameState::OnRep_Phase()
{
	UE_LOG(LogTemp, Log, TEXT("[Match] 阶段 -> %s"), PhaseName(Phase));
}

void AstudyGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AstudyGameState, Phase);
	DOREPLIFETIME(AstudyGameState, TimeRemaining);
	DOREPLIFETIME(AstudyGameState, PlayerGoldGrams);
	DOREPLIFETIME(AstudyGameState, UnloadCount);
	DOREPLIFETIME(AstudyGameState, PlayerCash);
	DOREPLIFETIME(AstudyGameState, DayIndex);
	DOREPLIFETIME(AstudyGameState, ClockMinutes);
	DOREPLIFETIME(AstudyGameState, FinalCash);
	DOREPLIFETIME(AstudyGameState, FinalUnsoldCash);
	DOREPLIFETIME(AstudyGameState, MineStopClockMinutes);
	DOREPLIFETIME(AstudyGameState, NoticeGoldGrams);
	DOREPLIFETIME(AstudyGameState, NoticeText);
	DOREPLIFETIME(AstudyGameState, NoticeTimeLeft);
}
