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
class UInputAction;
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

private:

	/** 准星当前对准的掉落物 */
	UPROPERTY()
	TObjectPtr<ADropItem> FocusedDrop;

	/** 按住左键时的循环计时器 */
	FTimerHandle DigRepeatHandle;

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

	/** 按 E 时调用：优先捡准星对准的那块，没有就捡附近最近的 */
	void TryPickup();

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

