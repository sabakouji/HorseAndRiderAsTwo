#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "HorseGameInstance.h"
#include "FrontEndPlayerController.generated.h"

class ACameraActor;

/**
 * AFrontEndPlayerController
 * FrontEnd Level（タイトル／モード選択／マルチ部屋）用の PlayerController。
 *
 * - メニュー操作のため、BeginPlay でマウスカーソルを表示し UI 入力モードに設定する。
 * - 始点（カメラ）は「1 台のメニューカメラを位置だけ滑らかに移動」させる方式。
 *   ビュー対象は切り替えない（＝アスペクト/FOV ブレンドの黒帯が出ない）。
 *   タグ付き CameraActor（FE_Title / FE_ModeSelect）は「位置の基準点」としてのみ使い、
 *   角度は変更しない（メニューカメラ＝FE_Title の角度のまま固定）。
 * - 位置補間は Tick で VInterpTo（CameraMoveSpeed）。瞬間移動せずシームレスに移動する。
 * - MultiRoom は位置も変えない（ModeSelect の構図を維持し UI のみ切替）。
 * - 実際のウィジェット生成・配置は BP（見た目担当）側で行う。
 */
UCLASS()
class HORSEANDRIDERASTWO_API AFrontEndPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AFrontEndPlayerController();

	virtual void Tick(float DeltaTime) override;

	/** メニューカメラの位置補間速度（VInterpTo の速度）。大きいほど速く到達する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FrontEnd|Camera", meta = (ClampMin = "0.1"))
	float CameraMoveSpeed = 3.0f;

protected:
	virtual void BeginPlay() override;

	/** シーンに対応する基準位置へ移動目標を更新する（角度は変えない） */
	void FocusScene(EAppScene Scene);

	/** OnAppSceneChanged のハンドラ。FocusScene を呼ぶ */
	UFUNCTION()
	void HandleAppSceneChanged(EAppScene NewScene);

private:
	/** EAppScene → 基準カメラを検索するためのアクタータグ名を返す（位置の基準点用） */
	static FName SceneToCameraTag(EAppScene Scene);

	/** タグ付き CameraActor の位置を収集し、メニューカメラ（ビュー対象）を確定する */
	void SetupMenuCamera();

	/** ビュー対象として固定する単一メニューカメラ（位置のみ移動・角度は固定） */
	UPROPERTY()
	TObjectPtr<ACameraActor> MenuCamera;

	/** 各シーンの基準位置（BeginPlay でキャッシュ。移動先として参照） */
	TMap<EAppScene, FVector> SceneCameraLocations;

	/** 現フレームの移動目標位置（Tick で VInterpTo の終点に使う） */
	FVector DesiredCameraLocation = FVector::ZeroVector;
};
