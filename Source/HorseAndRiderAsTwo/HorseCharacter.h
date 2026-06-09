#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "HorseCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
class AJockey;
class AReins;
class ARopeSimulationSpline;
class UChildActorComponent;
class ATrackActor;
class ARaceManager;
class UGhostRecorder;
struct FInputActionValue;
enum class ERaceState : uint8;

/**
 * AHorseCharacter
 * プレイヤーが操作する馬キャラクター。
 * EnhancedInput（Axis2D）で前後・旋回を制御し、
 * Tick 内で毎フレーム AddActorWorldRotation を呼ぶことで確実に旋回する。
 */
UCLASS()
class HORSEANDRIDERASTWO_API AHorseCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AHorseCharacter();
	virtual void Tick(float DeltaTime) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	/** 騎乗中ジョッキーへのアクセス（ゴール判定などで使用） */
	UFUNCTION(BlueprintCallable, Category = "Jockey")
	AJockey* GetCurrentJockey() const { return CurrentJockey; }

	/** 現在の手綱インスタンスへのアクセス */
	UFUNCTION(BlueprintCallable, Category = "Reins")
	AReins* GetCurrentReins() const { return CurrentReins; }

	/**
	 * デバッグ用: 現在騎乗中のジョッキーを強制的にラグドール化させる。
	 * BP の入力イベント（任意のキー）から呼び出すことで挙動確認に使用。
	 */
	UFUNCTION(BlueprintCallable, Category = "Jockey|Debug")
	void DebugForceJockeyRagdoll();

	/** AI 制御馬か（ゴースト記録対象判定などに使用） */
	UFUNCTION(BlueprintCallable, Category = "Race|AI")
	bool IsAIControlled() const { return bIsAI; }

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// =====================================================================
	// カメラ
	// =====================================================================

	/** ブームアーム（三人称視点の距離を管理） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> SpringArm;

	/** フォローカメラ */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	/**
	 * ダッシュ中のカメラ目標位置 (SceneComponent)。
	 * BP_HorseCharacter で Transform を編集して「ダッシュ時のカメラ位置・向き」を指定する。
	 * 通常時は影響しない (見た目用のマーカー)。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> DashCameraPoint;

	/** ダッシュ中の SpringArm 長さ (cm)。DashCameraPoint 自体をカメラ位置にしたい場合は 0 推奨 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera",
		meta = (ClampMin = "0.0"))
	float DashCameraArmLength = 0.0f;

	/**
	 * カメラ補間速度 (1/s)。FInterpTo に渡す係数。
	 * 値が大きいほど早く目標位置に到達する。3?8 程度が自然。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera",
		meta = (ClampMin = "0.0"))
	float CameraBlendSpeed = 6.0f;

	// =====================================================================
	// EnhancedInput
	// Blueprint の Details パネルで割り当てる
	// =====================================================================

	/** デフォルト Mapping Context（IMC_Horse を割り当てる） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	/**
	 * 移動アクション（IA_Move を割り当てる）
	 * Value Type: Axis2D  X軸 = 前後（W/S）  Y軸 = 左右旋回（A/D）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> MoveAction;

	/**
	 * ピックアップアクション（IA_Pickup を割り当てる、Bool 型）。
	 * 落馬したジョッキーが PickupRadius 内にいれば再騎乗させる。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> PickupAction;

	/**
	 * デバッグ用ラグドール強制アクション（IA_DebugRagdoll を割り当てる、Bool 型）。
	 * Details パネルで割り当て後、押すと即座にジョッキーをラグドール化する。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input|Debug", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> DebugRagdollAction;

	/**
	 * ダッシュアクション（IA_Dash を割り当てる、Bool 型）。
	 * 押している間 DashSpeedMultiplier 倍の速度になる。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> DashAction;

	/**
	 * ブレーキアクション（IA_Brake を割り当てる、Bool 型）。
	 * ダッシュ中に押すとジョッキーを前方放物線で射出する。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> BrakeAction;

	/**
	 * デバッグ振り回しアクション（IA_DebugSwing を割り当てる、Bool 型）。
	 * 押すたびに振り回しモードを toggle する。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input|Debug", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> DebugSwingAction;

	/** 振り回し時のアンカー: 馬メッシュ上のソケット名 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Swing")
	FName SwingAnchorSocketName = FName("BridleSocket_L");

	// =====================================================================
	// 物理パラメータ（エディタで数値調整可能）
	// =====================================================================

	/**
	 * 物理とアニメーションのブレンド比率。
	 * 0.0 = アニメーション完全優先 / 1.0 = 物理完全優先。
	 * TABS 感には 0.2?0.5 が推奨。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PhysicsBlendWeight = 0.3f;

	/**
	 * 物理シミュレーションを開始するボーン名。
	 * このボーン以下のボーンが物理演算に切り替わる。
	 * スケルトンに合わせて変更すること（例: pelvis, spine_01 など）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	FName SimulateBelowBoneName = FName("pelvis");

	// =====================================================================
	// 移動パラメータ（エディタで数値調整可能）
	// =====================================================================

	/** 最高移動速度 (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (ClampMin = "0.0"))
	float MoveSpeed = 800.0f;

	/** 旋回速度 (deg/s) ? Tick 内で DeltaTime と掛け合わせて使用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (ClampMin = "0.0"))
	float TurnSpeed = 100.0f;

	/** カメラブームの長さ (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera",
		meta = (ClampMin = "0.0"))
	float CameraArmLength = 400.0f;

	// =====================================================================
	// ダッシュ / 射出パラメータ
	// =====================================================================

	/** ダッシュ中の速度倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Dash",
		meta = (ClampMin = "1.0"))
	float DashSpeedMultiplier = 2.0f;

	/** 射出を許可する最低速度 (cm/s) ? これ未満では急ブレーキしても射出しない */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Dash",
		meta = (ClampMin = "0.0"))
	float MinSpeedToEject = 600.0f;

	/** ジョッキー射出時の前方向初速 (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Dash",
		meta = (ClampMin = "0.0"))
	float EjectForwardSpeed = 1500.0f;

	/** ジョッキー射出時の上方向初速 (cm/s)（山なり軌道用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Dash",
		meta = (ClampMin = "0.0"))
	float EjectUpSpeed = 800.0f;

	// =====================================================================
	// Jockey / Reins （ChildActorComponent 方式）
	//   BP_HorseCharacter の Components パネルで Child Actor Class に
	//   BP_Jockey / BP_Reins を割り当てて使用する。
	// =====================================================================

	/** ジョッキー用 ChildActorComponent。馬メッシュの JockeySocket にアタッチ */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jockey", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UChildActorComponent> JockeyChildActor;

	/** 手綱用 ChildActorComponent。馬メッシュの BridleSocket にアタッチ */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Reins", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UChildActorComponent> ReinsChildActor;

	/**
	 * Spline 縄手綱用 ChildActorComponent。BridleSocket_L ? BridleSocket_R 間に張る。
	 * BP_HorseCharacter で Child Actor Class に BP_RopeSimulationSpline を割り当てる。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Reins", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UChildActorComponent> RopeReinsChildActor;

	/**
	 * 馬が衝突を受けた際にジョッキーへ伝える衝撃の倍率。
	 * 1.0 で全伝搬、0.0 で伝搬しない。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey",
		meta = (ClampMin = "0.0"))
	float ImpactToJockeyRatio = 0.7f;

	/** 現在騎乗しているジョッキー（ChildActorComponent から取得した参照） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Jockey")
	TObjectPtr<AJockey> CurrentJockey;

	/** ピックアップ可能距離 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey",
		meta = (ClampMin = "0.0"))
	float PickupRadius = 200.0f;

	/** 現在の手綱（ChildActorComponent から取得した参照） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Reins")
	TObjectPtr<AReins> CurrentReins;

	// =====================================================================
	// AI および コース監視パラメータ
	// =====================================================================

	/** AI馬として自律走行させるか */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|AI")
	bool bIsAI = false;

	/** AIが追従走行する際のスプライン上の先行目標距離(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|AI", meta = (EditCondition = "bIsAI"))
	float LookaheadDistance = 600.0f;

	/** 曲率最大時に前進入力を絞り込む下限比率。1.0で減速なし、0.5で半速まで低下する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|AI", meta = (EditCondition = "bIsAI", ClampMin = "0.0", ClampMax = "1.0"))
	float MinAISpeedRatio = 0.5f;

	/** 他馬回避を発動する近傍検索半径(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|AI", meta = (EditCondition = "bIsAI", ClampMin = "0.0"))
	float AvoidanceRadius = 300.0f;

	/** 回避時に CurrentRightInput へ加算する回避オフセットの強さ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|AI", meta = (EditCondition = "bIsAI", ClampMin = "0.0"))
	float AvoidanceWeight = 0.5f;

	/** 曲率推定のためのLookahead前後サンプリング間隔(cm)。±CurvatureSampleStepの2点でTangentを取り差分する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|AI", meta = (EditCondition = "bIsAI", ClampMin = "1.0"))
	float CurvatureSampleStep = 200.0f;

	/** コースアウトと判定するスプラインからの最大許容距離(cm)。
	 *  M5 以降は ATrackActor::PointWidths（GetWidthAtDistance）が実際の許容幅を供給するため、
	 *  本値は実行時のコースアウト判定には使用されない。後方互換のため宣言を残置している。
	 *  許容幅を調整する場合は配置済 ATrackActor の PointWidths を編集すること。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Rules")
	float MaxAllowedDistanceFromSpline = 1200.0f;

	/** コースアウトしてから自動復帰するまでの猶予時間(秒) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Rules")
	float OutOfCourseTimeout = 2.0f;

	/** 逆走中フラグ */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race|Rules")
	bool bWrongWay = false;

public:
	/** 逆走中かどうかを外部から参照するための public getter */
	UFUNCTION(BlueprintCallable, Category = "Race|Rules")
	bool IsWrongWay() const { return bWrongWay; }

	/**
	 * 入力ロックフラグ。
	 * true の間は Tick 内の前進・旋回入力消費が 0 化される（AI 経路含む）。
	 * 物理シミュレーション（手綱・ジョッキー）は継続して動作する。
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Race|Input")
	bool bInputLocked = false;

	/** bInputLocked を BP から切り替えるためのヘルパ */
	UFUNCTION(BlueprintCallable, Category = "Race|Input")
	void SetInputLocked(bool bLocked) { bInputLocked = bLocked; }

	/** RaceManager の OnRaceStateChanged を購読するハンドラ */
	UFUNCTION()
	void HandleRaceStateChanged(ERaceState NewState);

protected:

	/** スプライン（コース）への参照 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Race")
	TObjectPtr<ATrackActor> TrackActorRef;

	/** ResetToTrack 通知用にキャッシュした RaceManager（BeginPlay で自動検索） */
	UPROPERTY()
	TObjectPtr<ARaceManager> CachedRaceManager;

	/** ゴースト記録コンポーネント（プレイヤー馬のみ BeginPlay で動的付与） */
	UPROPERTY()
	TObjectPtr<UGhostRecorder> GhostRecorder;

	/** コースアウトから自動復帰するための関数 */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void ResetToTrack();

private:
	// Tick で参照する入力値（Move コールバックで更新）
	float CurrentForwardInput = 0.0f;  // W=+1 / S=-1
	float CurrentRightInput   = 0.0f;  // D=+1 / A=-1

	/** コースアウト時間の計測タイマー */
	float OutOfCourseTimer = 0.0f;

	/** AI自動走行の入力更新 */
	void UpdateAIControl(float DeltaTime);

	/** スプライン距離をコース種別(ClosedLoop=Fmod / 非Loop=Clamp)に応じて正規化する */
	float NormalizeSplineDistance(float Distance, float SplineLength, bool bClosed) const;

	/** 逆走・コースアウトの監視 */
	void CheckCourseRules(float DeltaTime);

	// EnhancedInput コールバック
	void Move(const FInputActionValue& Value);          // Triggered
	void MoveCompleted(const FInputActionValue& Value); // Completed（キー離し時にリセット）

	/** 馬メッシュ/カプセル衝突 → ジョッキーへ衝撃伝搬 */
	UFUNCTION()
	void OnHorseHit(UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		FVector NormalImpulse,
		const FHitResult& Hit);

	/** ChildActorComponent から AJockey を取得し、騎乗状態をセットアップ */
	void InitializeJockeyFromChildActor();

	/** ChildActorComponent から AReins を取得し、両ケーブルを馬ブライドルへ接続 */
	void InitializeReinsFromChildActor();

	/** Spline 縄手綱の端点を BridleSocket_L / BridleSocket_R に紐付け */
	void InitializeRopeReinsFromChildActor();

	/**
	 * Reins の両ケーブルを馬ブライドルへ接続する純粋な処理。
	 * OnConstruction（エディタプレビュー時）と BeginPlay（実行時）の両方から呼ぶ。
	 */
	void LinkReinsToHorse();

	/** E キー: 落馬ジョッキーを再騎乗させる */
	void TryPickupJockey();

	/** ダッシュ入力 (Triggered/Completed) */
	void DashStarted();
	void DashCompleted();

	/** ブレーキ入力 (Triggered) ? ダッシュ中ならジョッキー射出 */
	void BrakePressed();

	/** ジョッキーを前方放物線で射出する */
	void EjectJockey();

	/** デバッグ: 振り回しモードを toggle */
	void ToggleDebugSwing();

	/** デバッグ: Enter キーで RaceManager のカウントダウン開始を発火する */
	void DebugStartRace();

	/** ダッシュ中フラグ */
	bool bIsDashing = false;

	/** Tick でカメラ補間を行う */
	void UpdateDashCameraBlend(float DeltaTime);

	// SpringArm 既定値 (BeginPlay 時にキャッシュ)
	FVector  DefaultSpringArmRelLoc  = FVector::ZeroVector;
	FRotator DefaultSpringArmRelRot  = FRotator::ZeroRotator;
	float    DefaultSpringArmLength  = 400.0f;

	/** ピックアップ可能なら画面ヒントを表示 */
	void DrawPickupHint();
};
