#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SplineMeshComponent.h"  // ESplineMeshAxis 定義のため
#include "TrackActor.generated.h"

class USplineComponent;
class UStaticMesh;
class UChildActorComponent;
class AGoalTrigger;
class ACheckpointActor;

/** 開始演出カメラ1カットの方向（IntroCameraShots に順番に積む） */
UENUM(BlueprintType)
enum class ECameraIntroShot : uint8
{
	LeftSide  UMETA(DisplayName = "左側面"),
	RightSide UMETA(DisplayName = "右側面"),
	Front     UMETA(DisplayName = "正面")
};

/** 正面演出カメラの動かし方 */
UENUM(BlueprintType)
enum class ECameraIntroFrontMove : uint8
{
	HorizontalRightToLeft UMETA(DisplayName = "水平(右馬→左馬)"),
	PullBackFromRight      UMETA(DisplayName = "引き(右馬から後退)")
};

/**
 * ATrackActor
 * USplineComponent をルートに持ち、スプラインの各セグメントに USplineMeshComponent を
 * 並べてコース形状を生成するレースコースアクター。
 *
 * 使用方法:
 *   - レベルに配置し、Spline ポイントを編集してコース形状を作る
 *   - RoadMesh に板状のスタティックメッシュを割り当てる
 *   - 順位計算は ARaceManager 側で GetDistanceAlongSplineAtLocation を使用
 */
UCLASS()
class HORSEANDRIDERASTWO_API ATrackActor : public AActor
{
	GENERATED_BODY()

public:
	ATrackActor();

	/** スプラインへアクセス（RaceManager から使用） */
	UFUNCTION(BlueprintCallable, Category = "Track")
	USplineComponent* GetSpline() const { return Spline; }

	/** スプラインの全長 */
	UFUNCTION(BlueprintCallable, Category = "Track")
	float GetTrackLength() const;

	/**
	 * スプラインを NumSamples 等分し、各点のワールド XY を取得 → コース全体の
	 * バウンディングボックスで長辺基準・アスペクト比維持の [0,1] に正規化した点列を返す。
	 * ClosedLoop / Open 両対応。短辺方向は中央寄せ。Spline 未設定時は空配列を返す。
	 */
	UFUNCTION(BlueprintCallable, Category = "Track|MiniMap")
	TArray<FVector2D> GetTrackShape2D(int32 NumSamples = 64) const;

	/** 任意のワールド座標を GetTrackShape2D と同一の正規化座標系へ変換する。 */
	UFUNCTION(BlueprintCallable, Category = "Track|MiniMap")
	FVector2D WorldToNormalizedMiniMap(const FVector& WorldLocation) const;

	/**
	 * 実コース幅の平均を、ミニマップ座標系（ComputeTrackBounds2D の長辺基準）に
	 * 正規化して返す。戻り値 × MiniMapSize.X = ミニマップ上のコース線の太さ(ピクセル)。
	 * 1. NumSamples 点で GetWidthAtDistance（中心からの片側半幅 cm）を取得し平均
	 * 2. コース全幅 = 平均半幅 × 2
	 * 3. ComputeTrackBounds2D の長辺（OutSize.X、両軸同値）で正規化
	 * Spline 未設定・長辺 0 など破綻時は 0.0 を返す。
	 */
	UFUNCTION(BlueprintCallable, Category = "Track|MiniMap")
	float GetNormalizedTrackWidth(int32 NumSamples = 64) const;

	/**
	 * 指定したスプライン距離におけるコース許容幅(cm)を返す。
	 * 隣接ポイントの PointWidths を線形補間する。
	 * Spline 未設定 / PointWidths 未同期時は DefaultWidth を返す。
	 */
	UFUNCTION(BlueprintCallable, Category = "Track|Width")
	float GetWidthAtDistance(float Distance) const;

	/** スプライン上にスポーンされた Goal を返す（未スポーン時は nullptr） */
	UFUNCTION(BlueprintCallable, Category = "Track|Race")
	AGoalTrigger* GetSpawnedGoal() const;

	/** スプライン上にスポーンされた Checkpoint 一覧を返す（CheckpointIndex 昇順） */
	UFUNCTION(BlueprintCallable, Category = "Track|Race")
	TArray<ACheckpointActor*> GetSpawnedCheckpoints() const;

	// --- 開始演出カメラ設定の Getter（IntroCameraDirector から参照） ---
	const TArray<ECameraIntroShot>& GetIntroCameraShots() const { return IntroCameraShots; }
	ECameraIntroFrontMove GetIntroFrontMove() const { return IntroFrontMove; }
	float GetIntroSideDistance() const { return IntroSideDistance; }
	float GetIntroFrontDistance() const { return IntroFrontDistance; }
	float GetIntroCameraHeight() const { return IntroCameraHeight; }
	float GetIntroSidePanDuration() const { return IntroSidePanDuration; }
	float GetIntroFrontMoveDuration() const { return IntroFrontMoveDuration; }
	float GetIntroPullBackDistance() const { return IntroPullBackDistance; }
	float GetIntroLookHeight() const { return IntroLookHeight; }

protected:
	/** エディタ上で Spline を編集すると呼ばれ、SplineMesh を再生成する */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** ルート: スプライン */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Track", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USplineComponent> Spline;

	/** コースをループさせるか。true で終端と始端を接続して閉じる。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track")
	bool bClosedLoop = false;

	/** スプライン全体の補間方式。true=直線(Linear/ポリライン)、false=ベジエ曲線(Curve)。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track")
	bool bLinearSpline = false;

	/** 各セグメントに沿わせる道メッシュ（板状の Static Mesh を推奨） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track")
	TObjectPtr<UStaticMesh> RoadMesh;

	/** SplineMesh の前進軸（メッシュの向きに合わせる） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track")
	TEnumAsByte<ESplineMeshAxis::Type> ForwardAxis = ESplineMeshAxis::X;

	/** SplineMesh のスケール（幅・厚みを調整） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track")
	FVector2D MeshScale = FVector2D(1.0f, 1.0f);

	/** コース許容幅の既定値(cm)。PointWidths 未設定ポイントの初期値。
	 *  AHorseCharacter::MaxAllowedDistanceFromSpline の既定値と一致させ後方互換を担保する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|Width")
	float DefaultWidth = 1200.0f;

	/** 各スプラインポイントのコース許容幅(cm)。OnConstruction で Spline ポイント数に同期される */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|Width", meta = (TitleProperty = "{i}: {0}"))
	TArray<float> PointWidths;

	/** 生成する SplineMesh に当たり判定（コリジョン）を付与するか。
	 *  ※ 当たり判定は元の RoadMesh（StaticMesh）にコリジョン形状が存在することが前提。
	 *    モデリングモード等で作成しコリジョンが無いメッシュの場合は、Static Mesh Editor で
	 *    簡易コリジョンを追加するか、Collision Complexity を "Use Complex Collision As Simple" に設定すること。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|Collision")
	bool bGenerateCollision = true;

	/** SplineMesh に適用するコリジョンプロファイル名（bGenerateCollision == true のとき有効）。
	 *  既定の "BlockAll" は馬・ジョッキー・手綱など全アクター/物理をブロックする。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|Collision",
		meta = (EditCondition = "bGenerateCollision"))
	FName CollisionProfileName = TEXT("BlockAll");

	// =====================================================================
	// Race 連携: Goal / Checkpoint をスプライン上に自動配置
	// =====================================================================

	/** Goal / Checkpoint を OnConstruction でスプライン上に自動スポーンするか。
	 *  false の場合は従来通りレベル直配置の Goal / Checkpoint を使用する想定。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|Race")
	bool bAutoSpawnGoalAndCheckpoints = true;

	/** Goal の配置距離(cm、スプライン始点からの距離)。0 でスタート位置兼用。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|Race",
		meta = (EditCondition = "bAutoSpawnGoalAndCheckpoints", ClampMin = "0.0"))
	float GoalDistance = 0.0f;

	/** Checkpoint の配置距離(cm) 一覧。昇順で評価される。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|Race",
		meta = (EditCondition = "bAutoSpawnGoalAndCheckpoints", TitleProperty = "{i}: {0}"))
	TArray<float> CheckpointDistances;

	/** Goal にスポーンするクラス。未指定なら AGoalTrigger を使用。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|Race",
		meta = (EditCondition = "bAutoSpawnGoalAndCheckpoints"))
	TSubclassOf<AGoalTrigger> GoalTriggerClass;

	/** Checkpoint にスポーンするクラス。未指定なら ACheckpointActor を使用。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|Race",
		meta = (EditCondition = "bAutoSpawnGoalAndCheckpoints"))
	TSubclassOf<ACheckpointActor> CheckpointClass;

	/** Goal/Checkpoint 判定ボックスの横幅倍率（コース幅に対する比率。1.3 = コース幅の130%）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|Race",
		meta = (EditCondition = "bAutoSpawnGoalAndCheckpoints", ClampMin = "0.1"))
	float TriggerWidthScale = 1.3f;

	// =====================================================================
	// 開始演出カメラ設定（IntroCameraDirector が参照）
	// =====================================================================

	/** 演出カメラのカット列。先頭から順に再生し、各カットは即座に切り替わる（ブレンドなし）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|IntroCamera")
	TArray<ECameraIntroShot> IntroCameraShots = { ECameraIntroShot::RightSide, ECameraIntroShot::Front };

	/** 正面カットの動かし方（Front カットで有効） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|IntroCamera")
	ECameraIntroFrontMove IntroFrontMove = ECameraIntroFrontMove::HorizontalRightToLeft;

	/** 側面カメラのコース横方向オフセット(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|IntroCamera", meta = (ClampMin = "0.0"))
	float IntroSideDistance = 700.0f;

	/** 正面カメラの前方距離(cm、グリッド前方へどれだけ離すか) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|IntroCamera", meta = (ClampMin = "0.0"))
	float IntroFrontDistance = 600.0f;

	/** 演出カメラの高さ(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|IntroCamera")
	float IntroCameraHeight = 200.0f;

	/** 注視点の高さ(cm、馬を見上げ/見下げる調整) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|IntroCamera")
	float IntroLookHeight = 100.0f;

	/** 側面パン（最後尾→先頭）の所要秒 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|IntroCamera", meta = (ClampMin = "0.1"))
	float IntroSidePanDuration = 2.5f;

	/** 正面移動（水平 or 引き）の所要秒 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|IntroCamera", meta = (ClampMin = "0.1"))
	float IntroFrontMoveDuration = 2.5f;

	/** 引き演出の終端で追加で後退する距離(cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track|IntroCamera", meta = (ClampMin = "0.0"))
	float IntroPullBackDistance = 800.0f;

private:
	/**
	 * Spline 形状（XY）のバウンディングボックスを算出する共通ヘルパ。
	 * OutMin = 正規化原点（短辺中央寄せ補正済の左下）、OutSize = 長辺基準の正規化分母
	 * （= 長辺の長さ。両軸同値。0 ガード済）。
	 * GetTrackShape2D / WorldToNormalizedMiniMap が同一基準で正規化するため共用する。
	 */
	void ComputeTrackBounds2D(FVector2D& OutMin, FVector2D& OutSize) const;

	/** 動的生成された SplineMesh の参照（再生成時に破棄するため保持） */
	UPROPERTY()
	TArray<TObjectPtr<USplineMeshComponent>> SplineMeshes;

	/** 既存の SplineMesh をクリア */
	void ClearSplineMeshes();

	/** Spline の各ポイント間に SplineMesh を並べる */
	void BuildSplineMeshes();

	/** PointWidths の要素数を Spline ポイント数に同期する（既存値は保持し、追加分を DefaultWidth で埋める） */
	void SyncPointWidthsToSpline();

	/** 既存の自動スポーン子アクターコンポーネントを破棄 */
	void ClearAutoSpawnedTriggers();

	/** GoalDistance / CheckpointDistances を読み取り、子アクターを生成 */
	void BuildAutoSpawnedTriggers();

	/** 自動スポーンされた Goal ChildActorComponent */
	UPROPERTY()
	TObjectPtr<UChildActorComponent> SpawnedGoalChild;

	/** 自動スポーンされた Checkpoint ChildActorComponent 群（CheckpointIndex 昇順） */
	UPROPERTY()
	TArray<TObjectPtr<UChildActorComponent>> SpawnedCheckpointChildren;
};
