#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GhostRecorder.generated.h"

/**
 * FGhostFrame
 * ゴースト 1 サンプル分の記録（記録開始からの相対秒・ワールド位置・ワールド回転）。
 */
USTRUCT(BlueprintType)
struct FGhostFrame
{
	GENERATED_BODY()

	/** 記録開始からの相対経過秒 */
	UPROPERTY(BlueprintReadOnly)
	float TimeStamp = 0.0f;

	/** ワールド位置 */
	UPROPERTY(BlueprintReadOnly)
	FVector Location = FVector::ZeroVector;

	/** ワールド回転 */
	UPROPERTY(BlueprintReadOnly)
	FRotator Rotation = FRotator::ZeroRotator;
};

/**
 * UGhostRecorder
 * 記録対象アクターに付与し、SampleInterval ごとに自アクタの位置・回転を蓄積する ActorComponent。
 * StartRecord でバッファをリセットして記録開始、StopRecord で停止、GetFrames で取得。
 * ラップ境界での切り出し（StartRecord 再呼び出し）は呼び出し側 (ARaceManager) が行う。
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class HORSEANDRIDERASTWO_API UGhostRecorder : public UActorComponent
{
	GENERATED_BODY()

public:
	UGhostRecorder();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** サンプリング間隔（秒）。既定 0.05s（20Hz） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost", meta = (ClampMin = "0.001"))
	float SampleInterval = 0.05f;

	/** 記録を開始する（バッファをリセットし、相対時刻基点を現在時刻に揃え、開始サンプルを 1 つ追加） */
	UFUNCTION(BlueprintCallable, Category = "Ghost")
	void StartRecord();

	/** 記録を停止する */
	UFUNCTION(BlueprintCallable, Category = "Ghost")
	void StopRecord();

	/** 記録済みフレーム列のコピーを返す */
	UFUNCTION(BlueprintCallable, Category = "Ghost")
	TArray<FGhostFrame> GetFrames() const { return Frames; }

	/** 記録バッファを空にする */
	UFUNCTION(BlueprintCallable, Category = "Ghost")
	void ClearFrames();

	/** 現在記録中か */
	UFUNCTION(BlueprintCallable, Category = "Ghost")
	bool IsRecording() const { return bRecording; }

private:
	/** 記録バッファ */
	UPROPERTY()
	TArray<FGhostFrame> Frames;

	/** 記録中フラグ */
	bool bRecording = false;

	/** StartRecord 時のワールド時刻（TimeStamp の相対基点） */
	float RecordStartTime = 0.0f;

	/** 直近サンプル時刻からの経過（SampleInterval 到達で 1 サンプル追加） */
	float TimeSinceLastSample = 0.0f;
};
