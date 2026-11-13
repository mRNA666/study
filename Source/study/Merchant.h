// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Merchant.generated.h"

class USkeletalMeshComponent;
class AstudyCharacter;

/** 商人的职能 */
UENUM(BlueprintType)
enum class EMerchantKind : uint8
{
	/** 卖矿洞开采权：付钱 → 传送到矿洞 */
	BuyMineRight UMETA(DisplayName = "卖开采权"),

	/** 收黄金：把金重按黑市价换成现金 */
	SellGold UMETA(DisplayName = "收黄金"),

	/** 矿洞里的向导：提前返回 / 查看已存放的黄金 */
	MineGuide UMETA(DisplayName = "矿洞向导"),

	/** 床：睡到下一天 */
	Bed UMETA(DisplayName = "床"),

	/** 正规买家：第二天白天来，全额收，每天有配额 */
	LegitBuyer UMETA(DisplayName = "正规买家")
};

/**
 * 营地里的 NPC。走近按 E 打开对话框。
 *
 * 对话系统的架构：
 *   Interact()        → 打开对话
 *   BuildDialog()     → 子类重写这里决定"弹什么、有几个选项"
 *   OnDialogOption(i) → 玩家选了第 i 条
 *   OnDialogNumber(v) → 玩家输入数字并确认（比如卖多少克）
 *
 * 以后加"装备店""情报贩子"只要继承本类、重写 BuildDialog / OnDialogOption 即可，
 * 不用碰 UI 和输入那一层。
 */
UCLASS()
class STUDY_API AMerchant : public AActor
{
	GENERATED_BODY()

public:
	AMerchant();

	virtual void Tick(float DeltaSeconds) override;

	/** 身体（白色机器人） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Merchant")
	TObjectPtr<USkeletalMeshComponent> Body;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant")
	EMerchantKind Kind = EMerchantKind::BuyMineRight;

	/** 名字（对话框标题） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant")
	FString DisplayName = TEXT("商人");

	/** 开采权价格（币） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant")
	int32 MineRightPrice = 4000;

	/** 交互半径（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant")
	float InteractRadiusCm = 250.f;

	/** 隐藏白色机器人（床用它，自己另外摆一个长方体） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant")
	bool bHideBody = false;

	/**
	 * 第几天之后才出现（0 = 一直都在）。
	 * 手动摆的正规买家填 2：第 1 天会自己藏起来、也交互不到。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Merchant")
	int32 MinDay = 0;

	UFUNCTION(BlueprintPure, Category = "Merchant")
	bool IsInRange(const AActor* Other) const;

	/** 玩家按 E 时调用。返回 true = 这次按键被消费掉了 */
	bool Interact(AstudyCharacter* Character);

	// ---------------- 对话（子类重写这三条） ----------------

	/** 决定弹什么内容、几个选项 */
	virtual void BuildDialog(AstudyCharacter* Character);

	/** 玩家选了第 Index 条（0 起） */
	virtual void OnDialogOption(AstudyCharacter* Character, int32 Index);

	/** 玩家输入数字并确认 */
	virtual void OnDialogNumber(AstudyCharacter* Character, float Value);

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	/** 把玩家的金重按黑市价卖出指定克数 */
	void SellGold(float Grams);

	/** 卖给正规买家：全额，但要吃配额 */
	void SellGoldLegit(float Grams);

	/** 尝试买一次开采权并传送进矿洞。返回 false = 钱不够 */
	bool BuyMineRight();

	/** 上一次买开采权是不是因为"钱不够"失败（用来在对话框上提示） */
	UPROPERTY(Transient)
	bool bBuyFailed = false;

	/** 矿洞向导：玩家是不是刚点过"查看已存放"（点了才把数字写进正文） */
	UPROPERTY(Transient)
	bool bStashRevealed = false;

	/** 按 MinDay 决定露不露面。要每帧算 —— 见 .cpp 里的注释 */
	void UpdateDayGate();
};
