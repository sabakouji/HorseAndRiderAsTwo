#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IntroCameraDirector.generated.h"

class UCameraComponent;
class ARaceManager;
class ATrackActor;
class AHorseCharacter;

/**
 * AIntroCameraDirector
 * レース開始時の演出カメラ。RaceManager が実行時にスポーンし StartIntro() で起動する。
 *
 * - 演出スタイル・距離・秒数は ATrackActor の Track|IntroCamera 設定から読む。
 * - グリッド整列中の馬から「先頭/最後尾・右/左」をスプライン前方/右ベクトルへの射影で判定する。
 * - 側面=最後尾→先頭パン / 正面=右馬→左馬の水平 or 右馬から引き / 側面→正面のシーケンスに対応。
 * - 演出完了で RaceManager->StartCountdown() を呼び、ビューをプレイヤー馬へ戻す。
 */
UCLASS()
class HORSEANDRIDERASTWO_API AIntroCameraDirector : public AActor
{
	GENERATED_BODY()

public:
	AIntroCameraDirector();

	virtual void Tick(float DeltaTime) override;

	/** 演出を開始する。Manager 経由で Track と馬リストを取得する。 */
	void StartIntro(ARaceManager* InManager);

protected:
	/** 描画カメラ。ルートに設定し Actor の Transform がそのまま POV になる。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Intro", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> IntroCamera;

private:
	/** グリッドの馬位置・コース基準ベクトルを算出する。失敗時 false。 */
	bool ComputeGridReference();

	/** 現在の ShotIndex / Elapsed からカメラ Transform を計算する。 */
	void EvaluateCamera(FVector& OutLocation, FRotator& OutRotation) const;

	/** 側面パン区間のカメラを計算（sideSign: +1=右, -1=左, t:0..1）。 */
	void EvalSidePan(float SideSign, float Alpha, FVector& OutLoc, FVector& OutLook) const;

	/** 正面移動区間のカメラを計算（t:0..1）。 */
	void EvalFront(float Alpha, FVector& OutLoc, FVector& OutLook) const;

	/** 演出を終了し、カウントダウンへ移行する。 */
	void FinishIntro();

	/** カット列の総数（Track 未設定時は 0）。 */
	int32 NumShots() const;

	/** 現在のカットの所要秒（側面/正面で異なる）。 */
	float CurrentShotDuration() const;

	UPROPERTY()
	TObjectPtr<ARaceManager> Manager;

	UPROPERTY()
	TObjectPtr<ATrackActor> Track;

	/** グリッド基準ベクトル・主要馬位置（ComputeGridReference で確定） */
	FVector TrackForward = FVector::ForwardVector;
	FVector TrackRight = FVector::RightVector;
	FVector GridCenter = FVector::ZeroVector;
	FVector LeadHorseLoc = FVector::ZeroVector;
	FVector LastHorseLoc = FVector::ZeroVector;
	FVector RightHorseLoc = FVector::ZeroVector;
	FVector LeftHorseLoc = FVector::ZeroVector;

	bool bActive = false;
	/** 再生中のカット番号（IntroCameraShots のインデックス） */
	int32 ShotIndex = 0;
	float Elapsed = 0.0f;
};
