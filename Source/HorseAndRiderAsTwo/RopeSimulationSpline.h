#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SplineMeshComponent.h"
#include "RopeSimulationSpline.generated.h"

class USceneComponent;
class USplineComponent;
class USplineMeshComponent;
class UStaticMesh;

/**
 * ARopeSimulationSpline
 * SplineComponent + 複数 SplineMeshComponent で構築する縄。
 *
 * - 始点 / 終点は BP から指定可能 (Actor + Socket または World 座標)
 * - 内部はパーティクル列を Verlet 統合 + 距離拘束で結ぶ簡易物理
 * - bPreviewInEditor = true でエディタ実行前 (OnConstruction / Editor Tick)
 *   にも物理プレビュー可能
 */
UCLASS()
class HORSEANDRIDERASTWO_API ARopeSimulationSpline : public AActor
{
	GENERATED_BODY()

public:
	ARopeSimulationSpline();

	virtual void Tick(float DeltaTime) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return bPreviewInEditor; }

	/** BP から始点 / 終点を差し替える */
	UFUNCTION(BlueprintCallable, Category = "Rope")
	void SetEndpoints(AActor* InStartActor, FName InStartSocket,
	                  AActor* InEndActor, FName InEndSocket);

	/** パーティクルを初期直線配置に戻す */
	UFUNCTION(BlueprintCallable, Category = "Rope")
	void ResetRope();

	/**
	 * 端点ソケットが利用可能になったタイミングで親 (HorseCharacter 等) から呼ぶ。
	 * 未初期化なら長さを確定する。既に初期化済みなら端点だけ追従させる。
	 */
	UFUNCTION(BlueprintCallable, Category = "Rope")
	void RefreshLayout();

	/** Socket 名を保持したまま、端点 Actor 参照だけを差し替える */
	UFUNCTION(BlueprintCallable, Category = "Rope")
	void SetEndpointActors(AActor* InStartActor, AActor* InEndActor);

	/** 1 ステップだけシミュレーションを進める（BP からも呼び出し可能） */
	UFUNCTION(BlueprintCallable, Category = "Rope")
	void StepSimulation(float DeltaTime);

	/**
	 * ジョッキー (またはその他コンポーネント) のソケットに縄の中央粒子を物理的にピン留めする。
	 * @param InGripComponent ジョッキーのスケルタルメッシュ等
	 * @param InGripSocket    ソケット名 (例: "hand_l")
	 * @param InParticleIndex ピン留め粒子の index。-1 で中央 (NumSegments/2)。
	 */
	UFUNCTION(BlueprintCallable, Category = "Rope|HandGrip")
	void AttachHandGrip(USceneComponent* InGripComponent, FName InGripSocket, int32 InParticleIndex = -1);

	/** ハンドグリップを解除（手離し） */
	UFUNCTION(BlueprintCallable, Category = "Rope|HandGrip")
	void DetachHandGrip();

	UFUNCTION(BlueprintCallable, Category = "Rope|HandGrip")
	bool IsHandGripAttached() const { return HandGripComponent != nullptr; }

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// =====================================================================
	// コンポーネント
	// =====================================================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rope", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rope", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USplineComponent> SplineComp;

	// =====================================================================
	// 始点 / 終点設定
	// =====================================================================

	/** 始点 Actor（未指定なら StartWorldOffset を使用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Endpoints")
	TObjectPtr<AActor> StartActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Endpoints")
	FName StartSocketName;

	/** 始点 Actor 未指定時、または追加オフセットとしてアクター相対 / ワールド座標 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Endpoints")
	FVector StartLocalOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Endpoints")
	TObjectPtr<AActor> EndActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Endpoints")
	FName EndSocketName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Endpoints")
	FVector EndLocalOffset = FVector::ZeroVector;

	// =====================================================================
	// 縄パラメータ
	// =====================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Shape",
		meta = (ClampMin = "2", ClampMax = "256"))
	int32 NumSegments = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Shape",
		meta = (ClampMin = "0.1"))
	float RopeWidth = 2.0f;

	/**
	 * たるみ係数: 縄の全長 = 始点〜終点直線距離 × SlackFactor
	 * 1.0 でピンと張る。1.2〜1.5 程度で自然なたわみ。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Shape",
		meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float SlackFactor = 1.3f;

	/** SlackFactor の代わりに明示的に全長 (cm) を指定したい場合に使用 (>0 のとき優先) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Shape",
		meta = (ClampMin = "0.0"))
	float OverrideTotalLength = 0.0f;

	/** スプラインメッシュに割り当てる円筒等の StaticMesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Shape")
	TSoftObjectPtr<UStaticMesh> SegmentMesh;

	/**
	 * メッシュの「長さ方向」軸。
	 * - Engine の /Engine/BasicShapes/Cylinder は Z 軸が高さ → Z を選択
	 * - 自作の横向き円柱 (Blender 等で X 方向に長い) なら X を選択
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Shape")
	TEnumAsByte<ESplineMeshAxis::Type> SegmentForwardAxis = ESplineMeshAxis::Z;

	/**
	 * 各セグメントの個別影投影を有効にするか。
	 * false にすると各セグメント間の影の継ぎ目が消えて自然になる。
	 * 視覚優先なら false 推奨 (動的影だけ落ちなくなる)。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Shape")
	bool bSegmentCastShadow = false;

	/**
	 * セグメント同士を少し重ねる比率 (0.0〜0.3)。
	 * Engine の Cylinder のように両端にキャップがあるメッシュで、
	 * 継ぎ目が見えるのを隠すために使用。0.1 程度が自然。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Shape",
		meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float SegmentOverlap = 0.1f;

	/**
	 * 端点をスプライン方向に内側へオフセットする距離 (cm)。
	 * フタなしチューブメッシュ使用時に、開口部をソケット位置の
	 * 馬メッシュ内部に押し込んで隠すために使う。
	 * 例: 5.0 で 5cm 内側にめり込ませる。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Shape",
		meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float EndpointInsetDepth = 0.0f;

	/**
	 * 始点から縄が突き出す方向 (StartActor のアクター空間ベクトル)。
	 * 例: (-20, 0, 0) で StartActor の -X 方向に 20cm 突き出してから下に垂れる。
	 * ZeroVector のときは物理に任せる (純粋な懸垂)。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Shape")
	FVector StartEmergeOffset = FVector::ZeroVector;

	/** 終点から縄が突き出す方向 (EndActor のアクター空間ベクトル) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Shape")
	FVector EndEmergeOffset = FVector::ZeroVector;

	/**
	 * 始点・終点側それぞれから何粒子分を emerge 方向の直線上に等間隔でピン留めするか。
	 * 1 = 2 番目の粒子だけを Start+EmergeOffset にピン留め (デフォルト・推奨)
	 *      → ベジエ補間せず、その先は純粋な物理シミュレーションでたわむ
	 * 2 以上 = emerge 方向の直線に沿って複数粒子をピン留め (硬い "首" を作る)
	 * 0 = emerge ピン留め無効
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Shape",
		meta = (ClampMin = "0", ClampMax = "8"))
	int32 EmergeSmoothCount = 1;

	/**
	 * Emerge ピン粒子の配置方式:
	 *  - true  : 二次ベジエ曲線 (滑らかなS字立ち上がり、自然な懸垂と接続)
	 *  - false : 直線 (Particles[1] を Anchor + EmergeOffset に直接固定、以降の粒子は emerge 方向に沿って rest 長で配置)
	 * EmergeSmoothCount=1 の場合はどちらも結果ほぼ同じだが、2 以上で違いが顕著。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Shape")
	bool bUseEmergeBezier = false;

	/**
	 * 特定セグメントの rest 長 (cm) を上書きする辞書。
	 * Key = セグメントインデックス (0..NumSegments-1)、Value = rest 長 (cm)。
	 * 例: { 1 → 3.0, 2 → 3.0 } とすると、セグメント 1, 2 の間隔だけ 3cm になる。
	 *      他のセグメントは TotalRopeLength / NumSegments の均等値のまま。
	 *
	 * NumSegments を変えてもインデックスは保持されるため、
	 * "現在のセグメント数の何番目を縮める" という指定が直感的に可能。
	 * 範囲外のインデックスは無視される。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Shape")
	TMap<int32, float> SegmentLengthOverrides;

	// =====================================================================
	// 物理
	// =====================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Physics")
	bool bSimulatePhysics = true;

	/** エディタプレビュー（PIE 前にビューポートで物理を回す） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Physics")
	bool bPreviewInEditor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Physics")
	FVector Gravity = FVector(0.0f, 0.0f, -980.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Physics",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Damping = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Physics",
		meta = (ClampMin = "1", ClampMax = "32"))
	int32 ConstraintIterations = 8;

	/**
	 * 張力（剛性）: 0=完全に伸びる, 1=ほぼ伸びない (ロープが硬い)
	 * 各反復で距離拘束による補正にこの係数を掛ける。
	 * 0.3〜0.6 程度で「ある程度伸びる」自然な挙動。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Physics",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Stiffness = 0.5f;

	/** 最大伸び率: rest 長に対する許容上限 (例: 1.5 で最大 1.5 倍まで伸びる) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|Physics",
		meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float MaxStretchRatio = 1.5f;

	// =====================================================================
	// ハンドグリップ反発 (縄の伸び制限)
	// =====================================================================

	/**
	 * グリップが縄の許容長を超えたとき、グリップ対象ボーンに引き戻し力を加える。
	 * - 始点〜グリップ粒子の累積 rest 長 × GripMaxLengthRatio を超えたら反発
	 * - グリップ〜終点側も同様
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|HandGrip")
	bool bGripPushbackEnabled = true;

	/** 許容長の倍率 (1.0 で純粋な rest 長基準、1.1 で 10% の遊び) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|HandGrip",
		meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float GripMaxLengthRatio = 1.0f;

	/** 反発スプリング剛性（伸び量に比例して加算される力の係数） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|HandGrip",
		meta = (ClampMin = "0.0"))
	float GripPushbackStiffness = 5000.0f;

	/** 反発スプリング減衰（揺れ収束） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|HandGrip",
		meta = (ClampMin = "0.0"))
	float GripPushbackDamping = 80.0f;

	/**
	 * 許容長に達した時点でグリップボーンの位置を強制的にクランプし、
	 * 外向き速度をゼロ化する（反発しない・伸び切り拘束）。
	 * これを有効にしておけば縄は限界値以上に絶対伸びない。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|HandGrip")
	bool bGripHardClamp = true;

	/**
	 * 限界外を維持している間、毎フレーム接線速度に掛ける減衰係数 (秒あたりの保持率)。
	 * 例: 0.9 なら 1 秒あたり 10% の接線エネルギーが失われる (DeltaTime で補間)。
	 * 1.0 で減衰無し（永久にスイング）。0 でも瞬時停止ではなく接線方向だけ即停止。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope|HandGrip",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GripContinuousTangentialDamping = 0.9f;

	UPROPERTY(EditAnywhere, Category = "Rope|HandGrip", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AnchorVelSmoothing = 0.5f;  // 0=フィルタなし, 1=完全に過去値

	FVector SmoothedStartAnchorVel = FVector::ZeroVector;
	FVector SmoothedEndAnchorVel = FVector::ZeroVector;

private:
	void RebuildSplineMeshes();
	void ResolveEndpoint(AActor* Actor, FName Socket, const FVector& LocalOffset,
	                     FVector& OutWorld) const;
	/** Inset を適用した最終的な両端 world 座標を取得 */
	void ResolveBothEndpointsWithInset(FVector& OutStart, FVector& OutEnd) const;
	/** Emerge オフセットをワールド方向ベクトルに変換 (該当 Actor の transform を介する) */
	FVector ResolveEmergeWorldOffset(AActor* Actor, const FVector& LocalOffset) const;

	/** セグメント i の rest 長を返す (SegmentLengthOverrides にエントリがあれば優先) */
	float GetSegmentRestLength(int32 SegmentIndex) const;

	/**
	 * 端点から emerge 方向に沿った滑らかな曲線で複数粒子をピン留めする。
	 * @param Anchor       端点ワールド座標 (Particles[0] または Particles[Last] の位置)
	 * @param EmergeW      emerge 方向のワールド オフセット
	 * @param NaturalEnd   ピン留め最終粒子が向かう先の自然位置 (中央方向の sag 位置等)
	 * @param StartIdx     ピン留めを開始する粒子インデックス (始点側=1、終点側=Particles.Num()-2 から逆順)
	 * @param Direction    +1 で始点側 (1,2,3,...)、-1 で終点側 (N-2, N-3,...)
	 * @param Count        ピン留めする粒子数
	 */
	void PinEmergeCurve(const FVector& Anchor, const FVector& EmergeW,
	                    const FVector& NaturalEnd, int32 StartIdx, int32 Direction,
	                    int32 Count);
	void InitializeParticlesAlongLine();
	void UpdateSplineFromParticles();

	UPROPERTY(Transient)
	TArray<TObjectPtr<USplineMeshComponent>> SegmentMeshes;

	struct FRopeParticle
	{
		FVector Pos = FVector::ZeroVector;
		FVector PrevPos = FVector::ZeroVector;
	};

	TArray<FRopeParticle> Particles;

	/** セグメント間 rest 長 (確定後は端点が動いても変化しない) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rope|Runtime", meta = (AllowPrivateAccess = "true"))
	float SegmentRestLength = 10.0f;

	/** 縄の全長 (確定後は保持) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rope|Runtime", meta = (AllowPrivateAccess = "true"))
	float TotalRopeLength = 0.0f;

	bool bRopeInitialized = false;

	// =====================================================================
	// ハンドグリップ（ジョッキーが縄を持つ処理）
	// =====================================================================

	/** ピン留め対象のコンポーネント（ジョッキー側スケルタルメッシュ等） */
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> HandGripComponent = nullptr;

	/** ピン留めソケット名 */
	UPROPERTY(Transient)
	FName HandGripSocket = NAME_None;

	/** ピン留めする粒子インデックス */
	UPROPERTY(Transient)
	int32 HandGripParticleIndex = INDEX_NONE;

	/** ハンドグリップ位置を取得（解決失敗時は false） */
	bool ResolveHandGripWorld(FVector& OutWorld) const;

	// =====================================================================
	// アンカー速度追跡（馬の動きを相対速度計算で打ち消すため）
	// =====================================================================
	FVector PrevStartWorld = FVector::ZeroVector;
	FVector PrevEndWorld   = FVector::ZeroVector;
	bool bAnchorPrevValid  = false;
};
