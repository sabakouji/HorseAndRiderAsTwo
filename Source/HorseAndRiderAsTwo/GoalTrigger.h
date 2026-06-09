#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GoalTrigger.generated.h"

class UBoxComponent;
class AHorseCharacter;
class ARaceManager;

/**
 * AGoalTrigger
 * BoxComponent を持ち、馬がオーバーラップしたときに
 * 騎乗中（IsRiding）であれば ARaceManager にゴール通過を通知する。
 */
UCLASS()
class HORSEANDRIDERASTWO_API AGoalTrigger : public AActor
{
	GENERATED_BODY()

public:
	AGoalTrigger();

	/** 通知先 RaceManager を外部から設定する（Track 経由スポーン時の連携に使用） */
	UFUNCTION(BlueprintCallable, Category = "Goal")
	void SetRaceManager(ARaceManager* InManager) { RaceManager = InManager; }

protected:
	virtual void BeginPlay() override;

	/** ルート: ゴールトリガーボックス */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Goal", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> TriggerBox;

	/**
	 * 通知先の RaceManager。
	 * 未指定なら BeginPlay 時に GetAllActorsOfClass で自動検索する。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal")
	TObjectPtr<ARaceManager> RaceManager;

private:
	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);
};
