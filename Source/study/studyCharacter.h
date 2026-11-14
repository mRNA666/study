// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "studyCharacter.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class USpotLightComponent;
class UStaticMeshComponent;
class UInputAction;
class AMerchant;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  A basic first person character
 */
UCLASS(abstract)
class AstudyCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Pawn mesh: first person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	class UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	class UInputAction* MouseLookAction;

	/** Dig Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* DigAction;
	
public:
	AstudyCharacter();

	/** 当前负重（kg）。HUD / 蓝图都能读 */
	UFUNCTION(BlueprintPure, Category = "Bag")
	float GetCarriedWeightKg() const { return CarriedWeightKg; }

	/** 背包容量（kg） */
	UFUNCTION(BlueprintPure, Category = "Bag")
	float GetMaxCarryKg() const { return MaxCarryKg; }

	/** 当前负重比例 0~1（1 = 装满） */
	UFUNCTION(BlueprintPure, Category = "Bag")
	float GetLoadRatio() const { return (MaxCarryKg > 0.f) ? FMath::Clamp(CarriedWeightKg / MaxCarryKg, 0.f, 1.f) : 0.f; }

	/** 当前背着的黄金克数 */
	UFUNCTION(BlueprintPure, Category = "Bag")
	float GetCarriedGoldGrams() const { return CarriedGoldGrams; }

	/**
	 * 卸货：清空背包重量与黄金，移速恢复。
	 * @return 这次交回的黄金克数（包里没东西返回 0）
	 */
	float UnloadAll();

	/** 准星当前对准的掉落物。没有对准任何石头时是 nullptr（HUD 用它来显示信息） */
	UFUNCTION(BlueprintPure, Category = "Bag")
	ADropItem* GetFocusedDrop() const { return FocusedDrop; }

protected:

	/** Called from Input Actions for movement input */
	void MoveInput(const FInputActionValue& Value);

	/** Called from Input Actions for looking input */
	void LookInput(const FInputActionValue& Value);

	/** Handles aim inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoAim(float Yaw, float Pitch);

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles jump start inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump end inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

	/** 按下左键：检查冷却后挖一次，并启动"按住连挖"的计时器 */
	void DoDig();

	/** 计时器回调：按住左键时每 DigCooldownSeconds 挖一次（不再检查冷却） */
	void DoDigRepeat();

	/** 松开左键：停掉连挖计时器 */
	void StopDigRepeat();

	/** 真正执行一次挖掘（射线 + DigAt），不管冷却 */
	void PerformDig();

	/** 两次挖掘之间的最小间隔（秒）。必须大于物理烘焙的延迟（0.35 秒），否则碰撞会冻住 */
	UPROPERTY(EditAnywhere, Category = "Dig")
	float DigCooldownSeconds = 0.5f;

	/** 挖掘射程（cm）。从相机往前打射线，超出这个距离打不中 */
	UPROPERTY(EditAnywhere, Category = "Dig")
	float DigReachCm = 230.f;

	/**
	 * 单次挖掘挖掉的球形半径（cm）。
	 * 调大 → 挖得快、洞更圆；但每块石头的总挥击次数变少，
	 * 掉落是按"挥一次"触发的，所以掉落总数会跟着变少。
	 */
	UPROPERTY(EditAnywhere, Category = "Dig")
	float DigRadiusCm = 35.f;

	// ---------------- 头灯（矿洞全靠它） ----------------

	/** 戴在相机上的矿灯 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HeadLight")
	TObjectPtr<USpotLightComponent> HeadLight;

	/** 头灯相对相机的偏移（cm）。X 前 / Y 右 / Z 上 */
	UPROPERTY(EditAnywhere, Category = "HeadLight")
	FVector HeadLightOffset = FVector(8.f, 0.f, -8.f);

	/** 头灯亮度（流明） */
	UPROPERTY(EditAnywhere, Category = "HeadLight")
	float HeadLightIntensity = 20000.f;

	/** 头灯照多远（cm） */
	UPROPERTY(EditAnywhere, Category = "HeadLight")
	float HeadLightRadiusCm = 1500.f;

	/** 头灯现在是开着的吗 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HeadLight")
	bool bHeadLightOn = true;

	/** 开关头灯（按 T，且没有别的事情可做时） */
	void ToggleHeadLight();

	/** ESC：有对话框就取消对话，没有就开关设置菜单 */
	void OnEscapePressed();

	/** Tab：开关资产排行面板 */
	void OnRankPressed();

public:
	/** 设置头灯亮度倍数（1 = 默认）。ESC 菜单里的滑块调这个 */
	UFUNCTION(BlueprintCallable, Category = "HeadLight")
	void SetHeadLightScale(float Scale);

	// ---------------- 黑屏过渡（睡觉 / 传送回营地） ----------------

	/** 弹一段黑屏文字，Seconds 秒后自动消失 */
	void ShowTransition(const FString& Text, float Seconds);

	bool IsTransitioning() const { return TransitionTimeLeft > 0.f; }
	const FString& GetTransitionText() const { return TransitionText; }

	// ---------------- 视角 ----------------

	/**
	 * 相机相对第一人称手部（head 骨骼）的偏移（cm）。
	 * 想让镜头往前/后、左/右、高/低就调这里。
	 * 默认 (-2.8, 5.89, 0) 是模板自带的。
	 */
	UPROPERTY(EditAnywhere, Category = "View")
	FVector CameraOffset = FVector(-2.8f, 5.89f, 0.f);

	// ---------------- 手感 ----------------

	/** 挖一下的震屏幅度（cm） */
	UPROPERTY(EditAnywhere, Category = "Feel")
	float DigShakeOffsetCm = 1.2f;

	/**
	 * 挖矿音效从第几秒开始播。
	 * 实测 Rock_Impact_11 / 37 开头只有 0.1 / 2.7 ms 静音 —— 素材是干净的，
	 * 所以这里默认 0。如果换成了开头有静音的素材，把这个调大即可。
	 */
	UPROPERTY(EditAnywhere, Category = "Feel", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float DigSoundStartTime = 0.f;

	/** 拾取音效跳过开头多少秒（实测 Bag_Handle_1-5 开头有 90ms 静音） */
	UPROPERTY(EditAnywhere, Category = "Feel", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float PickupSoundStartTime = 0.09f;

	/** 震屏时长（秒） */
	UPROPERTY(EditAnywhere, Category = "Feel")
	float DigShakeDuration = 0.12f;

	// ---------------- 背包 ----------------

	/** 当前背着的重量（kg） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bag")
	float CarriedWeightKg = 0.f;

	/** 当前背着的黄金克数（这些石头熔出来值多少）。卸货时结算 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bag")
	float CarriedGoldGrams = 0.f;

	/** 背包容量（kg）。超过这个重量就装不进去了 */
	UPROPERTY(EditAnywhere, Category = "Bag")
	float MaxCarryKg = 50.f;

	/** 装满时的移速倍率（PRD：满包移速 60%） */
	UPROPERTY(EditAnywhere, Category = "Bag")
	float FullLoadSpeedScale = 0.6f;

	/** 准星识别掉落物的最大距离（cm）。和挖掘射程（DigReachCm）保持一致；超出这个距离按 E 也捡不到 */
	UPROPERTY(EditAnywhere, Category = "Bag")
	float FocusReachCm = 230.f;

	/** 按住 E 时每隔多久捡一块（秒） */
	UPROPERTY(EditAnywhere, Category = "Bag")
	float PickupInterval = 0.12f;

	/** 走多远响一次脚步（cm）。走 600cm/s、140cm 一步 → 约每秒 4 步 */
	UPROPERTY(EditAnywhere, Category = "Feel")
	float FootstepDistanceCm = 140.f;

private:

	/** 准星当前对准的掉落物 */
	UPROPERTY()
	TObjectPtr<ADropItem> FocusedDrop;

	/** 按住左键时的循环计时器 */
	FTimerHandle DigRepeatHandle;

	/** 准星是不是指着梯子（每帧在 UpdateFocus 里更新） */
	bool bFocusingLadder = false;

	/** 黑屏过渡：显示的文字和剩余时间 */
	FString TransitionText;
	float TransitionTimeLeft = 0.f;

	/** 脚步：累计走过的距离（到 FootstepDistanceCm 就响一次） */
	float FootstepAccumCm = 0.f;

	/** 按住 E 时的循环计时器 */
	FTimerHandle PickupRepeatHandle;

	/** 震屏：剩余时间 / 总时长 / 幅度 */
	float ShakeTimeLeft = 0.f;
	float ShakeTotalTime = 0.f;
	float ShakeAmplitude = 0.f;

	/** 相机原始的相对位置。震屏偏移从这个位置算，结束时还原到这里 */
	FVector CameraBaseRelativeLocation = FVector::ZeroVector;

	/** 上一次成功挖掘的时间（秒） */
	double LastDigTime = -1000.0;

	/** 空手时的最大移速（构造时从移动组件读出来存着） */
	float BaseMaxWalkSpeed = 600.f;

	/** 每帧从相机发射线，更新"准星对准的掉落物" */
	void UpdateFocus();

public:
	// ---------------- 对话（NPC 界面） ----------------

	/** 对话的两种形态：选项列表 / 数字输入 */
	enum class EDialogMode : uint8
	{
		None,
		Options,		// 列出几条选项，按 1/2/3 选
		NumberInput		// 让玩家输入一个数字（比如卖多少克金）
	};

	bool IsDialogOpen() const { return DialogMode != EDialogMode::None; }
	bool IsDialogNumberMode() const { return DialogMode == EDialogMode::NumberInput; }

	const FString& GetDialogTitle() const { return DialogTitle; }
	const FString& GetDialogBody() const { return DialogBody; }
	const FString& GetDialogPrompt() const { return DialogPrompt; }
	const FString& GetDialogInput() const { return DialogInput; }
	const TArray<FString>& GetDialogOptions() const { return DialogOptions; }

	/** 商人调用：记录这次对话的发起者（选项确认后回调它） */
	void SetDialogMerchant(AMerchant* InMerchant) { DialogMerchant = InMerchant; }

	/** 商人调用：弹一个"选项列表"对话框 */
	void OpenOptionDialog(const FString& Title, const FString& Body, const TArray<FString>& Options);

	/** 商人调用：弹一个"输入数字"对话框 */
	void OpenNumberDialog(const FString& Title, const FString& Body, const FString& Prompt, float MaxValue);

	/** 关闭对话 */
	void CloseDialog();

	// ---------------- 给 UMG 用的入口（都在 public） ----------------

	/** 点第 Index 条选项 */
	void ChooseDialogOption(int32 Index) { DialogChoose(Index); }

	/** 数字输入框按"确认" */
	void DialogSubmitNumber(float Value);

	/** 数字输入框的上限（卖金时 = 手上的金重）。> 0 时对话框里会出现行内输入行 */
	float GetDialogGoldHeld() const { return DialogMaxValue; }

	/** 商人调用：开启 / 关闭对话框里的"行内数字输入"（0 = 关闭） */
	void SetDialogNumberEnabled(float MaxValue) { DialogMaxValue = FMath::Max(MaxValue, 0.f); }

	/** 附近可交互的商人（HUD 提示用） */
	AMerchant* GetNearbyMerchant() const { return FindNearbyMerchant(); }

protected:
	// ---------------- 对话内部状态 ----------------

	EDialogMode DialogMode = EDialogMode::None;

	FString DialogTitle;
	FString DialogBody;
	FString DialogPrompt;
	FString DialogInput;			// 玩家已经敲进去的数字
	float DialogMaxValue = 0.f;		// 输入上限（卖金用）

	TArray<FString> DialogOptions;

	/** 当前对话的发起者。选项确认后回调它 */
	UPROPERTY()
	TObjectPtr<AMerchant> DialogMerchant;

	/** 数字键 0-9（只在对话打开时有意义） */
	void OnDialogDigit(int32 Digit);
	void DialogDigit0() { OnDialogDigit(0); }
	void DialogDigit1() { OnDialogDigit(1); }
	void DialogDigit2() { OnDialogDigit(2); }
	void DialogDigit3() { OnDialogDigit(3); }
	void DialogDigit4() { OnDialogDigit(4); }
	void DialogDigit5() { OnDialogDigit(5); }
	void DialogDigit6() { OnDialogDigit(6); }
	void DialogDigit7() { OnDialogDigit(7); }
	void DialogDigit8() { OnDialogDigit(8); }
	void DialogDigit9() { OnDialogDigit(9); }

	void DialogBackspace();
	void DialogConfirm();
	void DialogCancel();

	/** 选第 Index 条选项（0 起） */
	void DialogChoose(int32 Index);

public:
	/** 准星是不是指着梯子（HUD 显示"按 E 爬梯"） */
	bool IsFocusingLadder() const { return bFocusingLadder; }

	/** 按 E 时调用：优先捡准星对准的那块，没有就捡附近最近的 */
	void TryPickup();

	/** 找一个在交互范围内的商人（没有就返回 nullptr） */
	AMerchant* FindNearbyMerchant() const;

	/** 按 R 时调用：结算之后重开一局（其他阶段无反应） */
	void RequestRestart();

	/** 按下 E：立刻捡一次，并启动"按住连续捡"的计时器 */
	void OnPickupPressed();

	/** 松开 E：停掉连续捡 */
	void OnPickupReleased();

	/** 挖中石头时触发一次震屏 */
	void TriggerDigShake();

	/** 每帧更新震屏偏移；结束后把相机放回原位 */
	void UpdateCameraShake(float DeltaSeconds);

	/** 按当前负重更新移速 */
	void UpdateSpeedPenalty();

protected:

	/** 初始化：套头灯参数、记住相机的原始相对位置 */
	virtual void BeginPlay() override;

	/** 每帧更新准星焦点 */
	virtual void Tick(float DeltaSeconds) override;

protected:

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;
	

public:

	/** Returns the first person mesh **/
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	/** Returns first person camera component **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

};

