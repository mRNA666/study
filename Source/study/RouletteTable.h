// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Merchant.h"
#include "RouletteTable.generated.h"

class UStaticMesh;

/**
 * 轮盘赌台。
 *
 * 为什么继承 AMerchant 而不是新写一个 Actor：
 *   玩家的交互链（走近按 E → 找到"附近商人" → Interact → 弹对话 → 选项/数字回调）
 *   全部挂在 AMerchant 上（AstudyCharacter::FindNearbyMerchant 只找这个类）。
 *   继承它，交互、输入锁、鼠标、HUD 提示这些都自动复用，一处都不用改。
 *
 * 玩法（v1）：
 *   押注（数字输入，钱）→ 选颜色（红/黑 返还 2 倍，绿返还 GreenMultiplier 倍）
 *   → 扣钱 → 转 5 秒 → 停稳才结算。
 *   37 格：0 是绿，其余 36 格 18 红 18 黑。
 *
 * ★ 铁律一：先摇结果，再演动画。
 *     ResultNumber 在 StartSpin 里就定死了，动画只是把盘面转到"那一格在球下面"。
 *
 * ★ 铁律二：球停在哪，由"转子转到哪"来配合，而不是反过来。
 *     真轮盘是这样：球最后停在某个世界角度，盘自己转，让中奖那一格停在球下面。
 *     所以这里只算一个数：转子终角 = 球落点角 − 中奖格在转子上的本地角度。
 *
 * ★ 号码 → 角度的映射需要**校准**（PocketZeroYaw / bNumbersClockwise / 号码顺序）：
 *     模型上"0 号格"朝哪、号码顺时针还是逆时针排、是否用真实轮盘顺序。
 *     校准方法见 PocketZeroYaw 的注释（配合 DebugForceNumber）。
 */
UCLASS()
class STUDY_API ARouletteTable : public AMerchant
{
	GENERATED_BODY()

public:
	ARouletteTable();

	virtual void Tick(float DeltaSeconds) override;

	// ---------------- 网格（默认从 /Game/Roulette/ 取，可在关卡里改） ----------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Mesh")
	TObjectPtr<UStaticMesh> RotorAsset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Mesh")
	TObjectPtr<UStaticMesh> BowlAsset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Mesh")
	TObjectPtr<UStaticMesh> BallAsset;

	/** 整台轮盘抬高多少 cm（模型原点在盘面中心，摆到地面时用它抬起来） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Place")
	float WheelLiftCm = 60.f;

	// ---------------- 球道（三个数，单位都是 cm，第一次会自动按网格算） ----------------

	/** 球的起点半径（离盘心）。0 = 自动按转子半宽算 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Ball",
		meta = (ToolTip = "球起飞时离盘心的距离（cm）。看着球跑到盘外就调小，跑不进圈子就调大。0 = 自动"))
	float BallOuterCm = 0.f;

	/** 球落格时的半径（离盘心） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Ball",
		meta = (ToolTip = "球最后落进哪一圈（cm）。落在外圈没进格子就调小，落进中央塔就调大。0 = 自动"))
	float BallInnerCm = 0.f;

	/** 球的高度（相对 Actor 原点） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Ball",
		meta = (ToolTip = "球心的绝对高度（cm，相对 Actor 原点）。球陷进盘面就调大，悬空就调小。0 = 自动"))
	float BallHeightCm = 0.f;

	/** 球转几整圈（只影响观感） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Ball")
	float BallTurns = 5.f;

	// ---------------- 号码映射（校准用） ----------------

	/**
	 * 转子**本地坐标**里，0 号格（绿）的中心朝哪个方向（度）。
	 * 校准方法：把 DebugForceNumber 设成 0，转一次，看绿格是不是停在球下面；
	 * 不是就以 10° 为步长改这个值，直到绿格正好压在球下面。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Calibration")
	float PocketZeroYaw = 0.f;

	/** 号码在盘面上是不是顺时针递增（校准时如果左右反了就把这个反过来） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Calibration")
	bool bNumbersClockwise = true;

	/** 号码顺序用真实轮盘顺序（0,32,15,19…）还是简单的 0,1,2…36 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Calibration")
	bool bStandardWheelSequence = true;

	/**
	 * 号码盘是**不动的**，还是跟着转子一起转？
	 *   勾上 = 号码画在外碗上（固定）→ 球直接飞向"中奖号"所在的世界角度，转子只是空转
	 *   取消 = 号码在转子上        → 转子转过去，把中奖那一格停在球下面
	 * 判断方法：转的时候看数字动不动。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Calibration")
	bool bNumbersOnBowl = false;

	/**
	 * 一次性校准：上一把（DebugForceNumber 模式）球停稳后，**球下面实际是几号**。
	 * 填 -1 = 不动。填好之后会自动算出 PocketZeroYaw 并把它归 -1。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Calibration",
		meta = (ToolTip = "上一把球下面实际是几号？填上就自动校准 PocketZeroYaw（填 -1 = 不校准）"))
	int32 CalibrateFromNumber = -1;

	/**
	 * 游戏内校准模式：转完之后**问你"球停在几号"**，你填进去。
	 * ★ 转两把（两把最好停在不同的号上）：两个样本能同时解出偏移量和号码方向，
	 *   一个样本只能解偏移（方向得你手动判断）。
	 * 校准完记得关掉。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Calibration")
	bool bCalibrationMode = false;

	/** 调试：强制停某个号码（-1 = 正常随机）。用于校准，平时保持 -1 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Calibration")
	int32 DebugForceNumber = -1;

	/**
	 * ★ 直接从盘面上"读"球的颜色（推荐）：
	 * 球停稳后，从球心往下打一条线，命中哪块面就读那块面的材质名
	 * （MAT_lacquer_red / black / green）—— 球压着什么颜色，判定就是什么颜色。
	 * 需要可见的那个轮盘网格**有碰撞体**，否则射线只会打到地板（默认关）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Calibration")
	bool bReadColorFromSurface = false;

	/**
	 * 球落点吸附到**格子正中心**。
	 * 为什么要吸附：球直径 4cm，而一格弧长可能只有 5cm —— 球压在分界线上时
	 * 视觉上两格都沾，判定只能四舍五入选一格，于是"时对时错"。
	 * 吸附之后球永远停在某格正中，视觉和判定一致。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Calibration")
	bool bSnapBallToPocketCenter = true;

	/**
	 * 红黑对调。如果球明明停在红格、结果却报黑（或反过来），勾上这一个就对了。
	 * 绿不受影响。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Calibration")
	bool bSwapRedBlack = false;

	// ---------------- 赌注参数 ----------------

	/** 押绿命中返还几倍（真实轮盘 36 倍 = 2.7% 庄家优势） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Rules")
	int32 GreenMultiplier = 36;

	/** 一次转多久（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Rules")
	float SpinDuration = 5.f;

	/** 转子转几整圈 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roulette|Rules")
	float RotorTurns = 4.f;

	// ---------------- 组件 ----------------

	/** 底座（不转） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roulette|Parts")
	TObjectPtr<UStaticMeshComponent> BowlMesh;

	/** 转子的轴心（空节点，摆在盘面中心） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roulette|Parts")
	TObjectPtr<USceneComponent> RotorPivot;

	/** 转子（内圈 + 号码 + 中央塔，只转这个） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roulette|Parts")
	TObjectPtr<UStaticMeshComponent> RotorMesh;

	/** 球 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roulette|Parts")
	TObjectPtr<UStaticMeshComponent> BallMesh;

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BuildDialog(AstudyCharacter* Character) override;
	virtual void OnDialogOption(AstudyCharacter* Character, int32 Index) override;
	virtual void OnDialogNumber(AstudyCharacter* Character, float Value) override;

private:
	enum class EStage : uint8
	{
		Idle,			// 主菜单
		AskBet,			// 正在填赌注（数字输入页）
		PickColor,		// 正在选颜色
		CalibrateInput	// 校准模式：正在填"球实际停在几号"
	};

	EStage Stage = EStage::Idle;

	/** 这一注押了多少（已扣过钱） */
	int32 PendingBet = 0;

	/** 押的颜色：0 红 / 1 黑 / 2 绿 */
	int32 PendingColor = -1;

	/** 这一把摇出来的：格位下标（0-36）/ 号码（0-36）/ 应返多少钱 */
	int32 ResultIndex = -1;
	int32 ResultNumber = -1;
	int32 PendingPayout = 0;

	// ---- 动画状态 ----
	bool bSpinning = false;
	float SpinTime = 0.f;

	/** 转子：起始角 / 总转角 */
	float StartYaw = 0.f;
	float TotalSpin = 0.f;

	/** 球：起始角 / 总转角 / 落点角 */
	float BallStartYaw = 0.f;
	float BallSweep = 0.f;
	float BallLandingYaw = 0.f;

	/** 球最后停在哪儿（cm），由上面几个 knobs 算出来 */
	float BallOuterRadius = 0.f;
	float BallInnerRadius = 0.f;
	float BallZ = 0.f;

	/** 这一把球落在**转子本地坐标**里的角度（校准用，和 PocketZeroYaw 无关） */
	float LastLandingLocalYaw = 0.f;

	/** 校准样本：格位下标 + 实测本地角度。两个样本就能同时解出偏移和方向 */
	struct FCalibSample
	{
		int32 Index = 0;
		float Angle = 0.f;
	};
	TArray<FCalibSample> CalibSamples;

	/** 用已收集的样本解出 PocketZeroYaw / bNumbersClockwise */
	void SolveCalibration(AstudyCharacter* Character);

	/** 在球正下方打一条线，读出那块面是哪块材质 → 0 红 / 1 黑 / 2 绿 / -1 判不出 */
	int32 SampleSurfaceColor() const;

	/**
	 * ★★ 从网格几何里把"每种颜色在哪个角度"读出来（不依赖碰撞、不需要校准）。
	 * 红/黑/绿是三个独立材质，所以逐三角形分析：算出每个三角形在盘面上的角度和半径，
	 * 按材质归类 → 得到一张 1° 一格的"角度 → 颜色"表。
	 * 球停在哪个角度，直接查表就是颜色 —— 视觉和判定从几何上保证一致。
	 */
	void BuildColorTableFromMesh();

	/** 查表：转子本地角度 → 0 红 / 1 黑 / 2 绿 / -1 未知 */
	int32 ColorAtLocalAngle(float LocalYaw) const;

	/** 角度 → 颜色表（360 格，1° 一格） */
	TArray<uint8> ColorByAngle;
	bool bColorTableReady = false;

	/** 转盘停下来要给谁弹结算（玩家可能中途跑开，但钱照给） */
	TWeakObjectPtr<AstudyCharacter> SpinningChar;

	void AskBet(AstudyCharacter* Character, const FString& Warning = FString());
	void StartSpin(AstudyCharacter* Character, int32 Color);
	void FinishSpin();
	void UpdateSpinVisual(float Ease);
	void RecomputeBallOrbit();

	/** 第 Index 格在转子本地坐标里的角度（度） */
	float PocketLocalYaw(int32 Index) const;

	/** 反过来：本地角度 → 是第几格（球落在哪一格就靠它算） */
	int32 IndexFromLocalYaw(float LocalYaw) const;

	/** 读 CalibrateFromNumber，一次性把 PocketZeroYaw 算对 */
	void ApplyCalibration();

	/** 第 Index 格上画的是几号 */
	int32 NumberAtPocketIndex(int32 Index) const;

	/** 某个号码在盘面上的格位下标（校准 DebugForceNumber 用） */
	int32 IndexOfNumber(int32 Number) const;

	/** 0 = 红，1 = 黑，2 = 绿（0 号格） */
	static int32 PocketColorOf(int32 Number);
	static FString ColorName(int32 Color);
};
