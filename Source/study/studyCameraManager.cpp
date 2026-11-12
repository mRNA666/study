// Copyright Epic Games, Inc. All Rights Reserved.


#include "studyCameraManager.h"

AstudyCameraManager::AstudyCameraManager()
{
	// 视角上下限（角度：正 = 抬头，负 = 低头）
	//
	// ViewPitchMax 决定能抬多高：抬太高会看到第一人称模型的头部（"鼻子"），
	//   所以压到 70° 左右。
	// ViewPitchMin 决定能低多少：-89 已经接近垂直向下，挖脚边的石头够用了。
	//   引擎自己的硬上限是 ±89.9，再往里设没有意义。
	ViewPitchMin = -89.9f;
	ViewPitchMax = 89.9f;
}
