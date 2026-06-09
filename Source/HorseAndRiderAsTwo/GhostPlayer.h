#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GhostRecorder.h"
#include "GhostPlayer.generated.h"

class UStaticMeshComponent;

/**
 * AGhostPlayer
 * ベストラップ等の記録済みフレーム列を TimeStamp ベースの線形補間で再生するゴーストアクター。
 * メッシュ・半透明マテリアルは BP 派生 (BP_GhostPlayer) でアサインする前提。
 */
UCLASS(Blueprintable)
class HORSEANDRIDERASTWO_API AGhostPlayer : public AActor
{
	GENERATED_BODY()

public:
	AGhostPlayer();

	virtual void Tick(float DeltaTime) override;

	/** ゴーストとして表示するメッシュ（半透明マテリアルは BP で割り当て） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ghost")
	TObjectPtr<UStaticMeshComponent> GhostMesh;

	/**
	 * 記録済みフレーム列を再生する。
	 * 先頭フレームへ瞬間移動して再生を開始する。
	 * Frames が 2 点未満なら再生せず、1 点なら先頭にスナップして終了する。
	 */
	UFUNCTION(BlueprintCallable, Category = "Ghost")
	void PlayFrames(const TArray<FGhostFrame>& InFrames);

	/** 再生を停止する */
	UFUNCTION(BlueprintCallable, Category = "Ghost")
	void StopPlayback();

	/** ループ再生するか（既定 true。次レース中に何周も重ねたい場合に使用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ghost")
	bool bLoop = true;

private:
	/** 再生対象フレーム列（PlayFrames でコピー保持） */
	UPROPERTY()
	TArray<FGhostFrame> Frames;

	/** 再生中フラグ */
	bool bPlaying = false;

	/** 再生開始からの相対経過秒 */
	float PlaybackTime = 0.0f;

	/** 直近の補間区間先頭インデックス（前進探索の効率化用） */
	int32 LastFrameIndex = 0;

	/** Frames を PlaybackTime で補間したトランスフォームを適用する */
	void ApplyInterpolatedFrame();
};
