#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StartGridSpawner.generated.h"

class ATrackActor;
class AHorseCharacter;

/**
 * AStartGridSpawner
 * スプライン先頭付近に最大 GridCount 台の馬を等間隔・左右オフセット付きで配置するスポーナ。
 *
 * 動作モード:
 *   - bRespawnExisting == true: レベルに既に配置された AHorseCharacter を i 番目から再配置する
 *   - bRespawnExisting == false: HorseClass を用いて新規スポーンする（HorseClass 必須）
 *
 * 配置ロジック:
 *   - 距離は i * GridSpacing （スプライン全長を超える場合は GridSpacing をクランプして詰める）
 *   - 左右オフセットは i の偶奇で +/- LateralOffset を交互適用
 *   - 向きは GetTangentAtDistanceAlongSpline で得た接線方向
 */
UCLASS()
class HORSEANDRIDERASTWO_API AStartGridSpawner : public AActor
{
	GENERATED_BODY()

public:
	AStartGridSpawner();

	/** 対象 Track。未指定なら BeginPlay で GetAllActorsOfClass により自動取得 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Grid")
	TObjectPtr<ATrackActor> Track;

	/** 配置する馬の総数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Grid", meta = (ClampMin = "1", ClampMax = "8"))
	int32 GridCount = 4;

	/** 縦方向（スプライン進行方向）の間隔 cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Grid", meta = (ClampMin = "0.0"))
	float GridSpacing = 300.0f;

	/** 左右オフセット cm（偶数 i は +、奇数 i は -） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Grid", meta = (ClampMin = "0.0"))
	float LateralOffset = 150.0f;

	/** 新規スポーン時に使用する馬クラス（bRespawnExisting == false のとき必須） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Grid")
	TSubclassOf<AHorseCharacter> HorseClass;

	/** true: レベル内の既存馬を再配置 / false: HorseClass で新規スポーン */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Grid")
	bool bRespawnExisting = true;

	/**
	 * スポーン時に下向きライントレースで路面を検出し、カプセル下端が路面に乗るよう
	 * 配置高さを補正する。false の場合はスプライン高さ + SpawnHeightOffset に配置する。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Grid|Ground")
	bool bSnapToGroundOnSpawn = true;

	/** トレース開始点をスプライン位置からどれだけ上方に取るか(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Grid|Ground",
		meta = (EditCondition = "bSnapToGroundOnSpawn", ClampMin = "0.0"))
	float GroundTraceUpHeight = 500.0f;

	/** トレースを下方向に何 cm まで延ばすか(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Grid|Ground",
		meta = (EditCondition = "bSnapToGroundOnSpawn", ClampMin = "0.0"))
	float GroundTraceDownDepth = 2000.0f;

	/** 路面とカプセル下端の余白(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Grid|Ground",
		meta = (EditCondition = "bSnapToGroundOnSpawn", ClampMin = "0.0"))
	float GroundClearanceMargin = 5.0f;

	/** トレース失敗時 / 着地無効時にスプライン高さへ加算する持ち上げ量(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Grid|Ground",
		meta = (ClampMin = "0.0"))
	float SpawnHeightOffset = 100.0f;

	/** グリッド配置を実行する（BeginPlay からも呼ばれ、BP から手動再実行も可能） */
	UFUNCTION(BlueprintCallable, Category = "Race|Grid")
	void SpawnGrid();

protected:
	virtual void BeginPlay() override;

private:
	/** Track 未指定時にレベルから自動取得 */
	void ResolveTrack();

	/**
	 * スプライン上の配置候補位置 SpawnLoc に対し、路面に着地させた最終位置を返す。
	 * @param SpawnLoc         スプライン由来の配置候補位置（XY 確定、Z はスプライン高さ）
	 * @param CapsuleHalfHeight 配置対象カプセルの半高(cm)
	 * @param IgnoredActors    トレースで無視するアクター（全馬・自身）
	 */
	FVector ResolveSpawnLocationOnGround(const FVector& SpawnLoc, float CapsuleHalfHeight,
		const TArray<AActor*>& IgnoredActors) const;
};
