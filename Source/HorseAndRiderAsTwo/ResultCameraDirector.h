#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ResultCameraDirector.generated.h"

class UCameraComponent;
class AHorseCharacter;

/**
 * AResultCameraDirector
 * レース Level に配置するリザルト演出カメラ。
 *
 * - BP から StartResultCamera(自分の馬) を呼ぶと、ゲームカメラから自身へ
 *   SetViewTargetWithBlend でシームレスに移動・回転する。
 * - 以後 Tick で対象馬を追従し、馬の顔を「前方・左斜め」から映し続ける
 *   （馬は別途エキシビション走行で走り続けるため、動きのある画になる）。
 *
 * 位置・角度・追従速度は EditAnywhere プロパティで調整可能。GUI は WBP_Result（BP）側。
 */
UCLASS()
class HORSEANDRIDERASTWO_API AResultCameraDirector : public AActor
{
	GENERATED_BODY()

public:
	AResultCameraDirector();

	virtual void Tick(float DeltaTime) override;

	/** 指定馬（通常は自分の馬）を追従するリザルトカメラを開始する */
	UFUNCTION(BlueprintCallable, Category = "Result")
	void StartResultCamera(AHorseCharacter* Target);

	/** 追従を停止する（ビュー対象は戻さない。必要なら BP 側で制御） */
	UFUNCTION(BlueprintCallable, Category = "Result")
	void StopResultCamera();

	// --- 注視点（馬の顔）の基準（馬アクタ原点からのオフセット） ---

	/** 顔として注視する前方オフセット(cm)。馬の頭の位置に合わせる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Camera")
	float FaceForward = 80.0f;

	/** 顔として注視する高さ(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Camera")
	float FaceHeight = 150.0f;

	// --- カメラ位置（馬基準のオフセット） ---

	/** カメラの前方距離(cm)。FaceForward より前に置くと顔を正面〜斜め前から映す */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Camera")
	float CameraFront = 250.0f;

	/** カメラの横オフセット(cm)。+ で馬の右、- で左。左斜めにするなら負値（既定 -200） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Camera")
	float CameraSide = -200.0f;

	/** カメラの高さ(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Camera")
	float CameraHeight = 120.0f;

	/** 追従の補間速度（位置・回転とも）。大きいほど機敏に追従する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Camera", meta = (ClampMin = "0.1"))
	float FollowSpeed = 5.0f;

	/** ゲームカメラから演出カメラへ移る際のブレンド秒数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result|Camera", meta = (ClampMin = "0.0"))
	float BlendTime = 1.0f;

protected:
	/** 描画に使うカメラ。ルートに設定し、Actor の Transform がそのままカメラの POV になる */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Result", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> ResultCamera;

private:
	/** 対象馬基準の目標カメラ Transform（位置・向き）を計算する */
	bool ComputeDesiredTransform(FVector& OutLocation, FRotator& OutRotation) const;

	/** 追従対象（自分の馬） */
	UPROPERTY()
	TObjectPtr<AHorseCharacter> TargetHorse;

	/** 追従中か */
	bool bActive = false;
};
