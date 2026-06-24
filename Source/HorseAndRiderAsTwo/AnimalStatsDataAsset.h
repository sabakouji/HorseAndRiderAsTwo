#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AnimalStatsDataAsset.generated.h"

/**
 * UAnimalStatsDataAsset
 * 動物（馬・その他）の挙動パラメータをまとめたデータ駆動アセット。
 * 各動物 BP の AHorseCharacter::AnimalStats に割り当てて使用する。
 * BeginPlay で AHorseCharacter::ApplyAnimalStats() により実行時へ反映される。
 * 未割当時は AHorseCharacter 側の既定値でフォールバックする。
 */
UCLASS(BlueprintType)
class HORSEANDRIDERASTWO_API UAnimalStatsDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// =====================================================================
	// 移動
	// =====================================================================

	/** 最高移動速度 (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement",
		meta = (ClampMin = "0.0"))
	float MaxMoveSpeed = 800.0f;

	/** 通常時の加速度 (cm/s^2)。大きいほど素早く最高速に達する */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement",
		meta = (ClampMin = "0.0"))
	float MaxAcceleration = 2048.0f;

	/** 旋回速度 (deg/s) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement",
		meta = (ClampMin = "0.0"))
	float TurnSpeed = 100.0f;

	// =====================================================================
	// ダッシュ
	// =====================================================================

	/** ダッシュ中の最高速度倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dash",
		meta = (ClampMin = "1.0"))
	float DashSpeedMultiplier = 2.0f;

	/** ダッシュ中の加速度倍率。大きいほどダッシュの伸びが鋭い */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dash",
		meta = (ClampMin = "1.0"))
	float DashAccelerationMultiplier = 1.5f;

	// =====================================================================
	// ブレーキ
	// =====================================================================

	/** ブレーキ時の減速度 (cm/s^2)。大きいほど短距離で止まる */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Brake",
		meta = (ClampMin = "0.0"))
	float BrakingDeceleration = 4096.0f;

	// =====================================================================
	// 射出（ジョッキー）
	// =====================================================================

	/** ジョッキー射出時の前方初速 (cm/s)。飛距離を決める */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eject",
		meta = (ClampMin = "0.0"))
	float EjectForwardSpeed = 2000.0f;

	/** ジョッキー射出時の上方初速 (cm/s)。小さいほど低弾道（カメラ内に収まる） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eject",
		meta = (ClampMin = "0.0"))
	float EjectUpSpeed = 250.0f;

	// =====================================================================
	// スタミナ（Phase 2 で使用）
	// =====================================================================

	/** スタミナ最大値 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina",
		meta = (ClampMin = "0.0"))
	float StaminaMax = 100.0f;

	/** ダッシュ中の毎秒スタミナ消費量 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina",
		meta = (ClampMin = "0.0"))
	float StaminaDrainPerSec = 25.0f;

	/** 非ダッシュ時の毎秒スタミナ回復量 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina",
		meta = (ClampMin = "0.0"))
	float StaminaRegenPerSec = 15.0f;

	/** ダッシュ終了後、回復が始まるまでの猶予時間 (秒) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina",
		meta = (ClampMin = "0.0"))
	float StaminaRegenDelay = 1.0f;
};
