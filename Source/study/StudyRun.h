// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/GameplayStatics.h"
#include "studyGameInstance.h"

/**
 * 从任意 UObject 拿到这一局的 GameInstance（拿不到就返回 nullptr）。
 *
 * 为什么要有这个头文件，而不是各自在 .cpp 里写一份：
 *   Merchant.cpp 和 RouletteTable.cpp 原来都在匿名 namespace 里定义了一份一模一样的
 *   GetRun。编辑器目标是"非 unity 编译"（这些文件被排除出 unity 文件），所以一直没事；
 *   但**游戏目标**（打包用的那个）走 unity 编译，会把多个 .cpp 拼进同一个 translation
 *   unit —— 同一个函数被定义两次，直接 error C2084: 已有函数体。
 *
 *   放成头文件里的 inline：同一个 TU 里只会有一份定义，多个 .cpp 包含它也合法。
 */
inline UstudyGameInstance* GetRun(class UObject* Context)
{
	return Context ? Cast<UstudyGameInstance>(UGameplayStatics::GetGameInstance(Context)) : nullptr;
}
