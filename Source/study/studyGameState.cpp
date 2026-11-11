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

void AstudyGameState::SetTimeRemaining(float NewRemaining)
{
	if (!HasAuthority())
	{
		return;
	}

	TimeRemaining = FMath::Max(NewRemaining, 0.f);
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
}
