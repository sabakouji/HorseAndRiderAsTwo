#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CheckpointActor.generated.h"

class UBoxComponent;
class AHorseCharacter;
class ARaceManager;

/**
 * ACheckpointActor
 * コース上の通過順序を強制するためのチェックポイントアクター。
 *
 * - レベルに複数配置し、CheckpointIndex を 0,1,2,... と順番に振る
 * - 馬がトリガーボックスにオーバーラップしたとき、ARaceManager に
 *   OnCheckpointPassed(Horse, CheckpointIndex) を通知する
 * - ARaceManager 側で「直前 CP +1 のみ進行」のルールで管理し、
 *   ショートカット（CP スキップ）による Lap 加算を防止する
 */
UCLASS()
class HORSEANDRIDERASTWO_API ACheckpointActor : public AActor
{
	GENERATED_BODY()

public:
	ACheckpointActor();

	/** 通知先 RaceManager を外部から設定する（Track 経由スポーン時の連携に使用） */
	UFUNCTION(BlueprintCallable, Category = "Race|Checkpoint")
	void SetRaceManager(ARaceManager* InManager) { RaceManager = InManager; }

	/** チェックポイント番号（0 始まり、コース順に振る） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race|Checkpoint")
	int32 CheckpointIndex = 0;

	/**
	 * 通知先の RaceManager。
	 * 未指定なら BeginPlay 時に GetAllActorsOfClass で自動検索する。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Checkpoint")
	TObjectPtr<ARaceManager> RaceManager;

protected:
	virtual void BeginPlay() override;

	/** ルート: チェックポイントトリガーボックス */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Checkpoint", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> TriggerBox;

private:
	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);
};
