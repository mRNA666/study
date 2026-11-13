// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "studyGameMode.generated.h"

class AstudyGameState;

/**
 *  这一局的"裁判"：推进昼夜时钟、什么时候结算。
 *
 *  它只跑在服务器上，所以不放任何要显示给玩家的数据 ——
 *  那些放 GameState（会复制到客户端）。
 *
 *  注意：真正跨关卡活下来的数据在 UstudyGameInstance 里（切关卡本体会被销毁）。
 */
UCLASS(abstract)
class AstudyGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AstudyGameMode();

	/** 取本局的 GameState（不是我们的类型时返回 nullptr） */
	UFUNCTION(BlueprintPure, Category = "Match")
	AstudyGameState* GetStudyGameState() const;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	/**
	 * 关卡是空的（Empty Level）：没有地板玩家会掉下去，营地还没光。
	 * 这里在运行时补一块地板；营地再补一盏平行光 + 天光。
	 * 矿洞故意不补光 —— 那里只该有头灯。
	 */
	void EnsureWorldBasics();

	/** 营地：在出生点附近摆上两个商人（卖开采权 / 收黄金） */
	void SpawnCampMerchants();

	/**
	 * 是否用代码自动摆营地 NPC。
	 * 你在关卡里手动摆好之后把它关掉（BP_FirstPersonGameMode → 类默认值），
	 * 否则会出现两份。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Camp")
	bool bAutoSpawnCampMerchants = false;

	/** 昼夜：按游戏时钟调太阳和天光（只在营地） */
	void UpdateDayNightLighting();

	/** 黑屏 1.5 秒后送回营地（矿洞超时 / 太晚两种情况共用） */
	void BeginReturnToCamp(const FString& TransitionText);

	/** 已经在走"送回营地"流程了，防止 Tick 每帧重复触发 */
	bool bReturnToCampPending = false;

	/** 缓存的太阳 / 天光，避免每帧遍历关卡找 */
	TWeakObjectPtr<class ADirectionalLight> SunLight;
	TWeakObjectPtr<class ASkyLight> SkyLightActor;

	/** 矿洞环境音（常驻播放的音频组件，必须存引用否则会被 GC） */
	UPROPERTY(Transient)
	TObjectPtr<class UAudioComponent> AmbienceComp;

	/** 第二天 20:00：结算（没卖掉的黄金按黑市价折算进现金） */
	void EndMatch();

public:
	/** 立刻结算（床在最后一天睡觉时调用） */
	void ForceSettle();
};
