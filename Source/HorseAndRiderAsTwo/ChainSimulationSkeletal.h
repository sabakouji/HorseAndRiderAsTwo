#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChainSimulationSkeletal.generated.h"

class USceneComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UPhysicsAsset;
class UPhysicsConstraintComponent;

/**
 * AChainSimulationSkeletal
 *
 * スケルタルメッシュ + PhysicsAsset + PhysicsConstraint による汎用「鎖」アクター。
 * フレイル鎖のように振り回したり、重力でしならせて 2 点を繋いだりできる。
 *
 * 接続形態:
 *   Endpoint A のみ        : 片端固定／反対側は自由 → フレイル運用
 *   Endpoint A + B 両方    : 両端固定 → 重力でしなる catenary（手綱・係留など）
 *   Endpoint なし          : 完全自由落下
 *
 * 既存 AReins（手綱専用 3 拘束）とは別系統の汎用実装。
 * 既存 ARopeSimulationSpline が Verlet ベースなのに対し、本クラスは
 * Chaos rigid body ベースで他オブジェクトとの衝突応答が可能。
 */
UCLASS()
class HORSEANDRIDERASTWO_API AChainSimulationSkeletal : public AActor
{
	GENERATED_BODY()

public:
	AChainSimulationSkeletal();

	virtual void Tick(float DeltaTime) override;

	// ========== BP 呼び出し可能 API ==========

	/**
	 * 端点 A を設定。
	 * - InSocketName が SkeletalMeshComponent のソケット名／ボーン名なら、その SocketTransform を参照
	 * - 一般 PrimitiveComponent のソケットも参照可
	 * - 解決失敗時はアクター原点
	 * - InLocalOffset はソケット（またはアクター）ローカル空間で加算
	 */
	UFUNCTION(BlueprintCallable, Category = "Chain")
	void SetEndpointA(AActor* InActor, FName InSocketName, FVector InLocalOffset = FVector::ZeroVector);

	/** 端点 B を設定（仕様は SetEndpointA と同じ） */
	UFUNCTION(BlueprintCallable, Category = "Chain")
	void SetEndpointB(AActor* InActor, FName InSocketName, FVector InLocalOffset = FVector::ZeroVector);

	/**
	 * 端点 A/B の Actor 参照だけを差し替える（SocketName / LocalOffset は維持）。
	 * BP テンプレート側でソケット名を編集して、C++ 側からは this を注入する用途。
	 * HorseCharacter のように ChildActorComponent 経由で配置する場合に便利。
	 */
	UFUNCTION(BlueprintCallable, Category = "Chain")
	void SetEndpointActors(AActor* InActorA, AActor* InActorB);



	/** 端点 B 拘束を解除しフレイル運用に切り替え */
	UFUNCTION(BlueprintCallable, Category = "Chain")
	void DetachEndpointB();

	/** 両端拘束を破断（破壊表現） */
	UFUNCTION(BlueprintCallable, Category = "Chain")
	void BreakChain();

	/** 鎖を再初期化（物理リセット＋端点へ再整列） */
	UFUNCTION(BlueprintCallable, Category = "Chain")
	void ResetChain();

	/** 末端ボーンへインパルスを加える（振り回し用） */
	UFUNCTION(BlueprintCallable, Category = "Chain")
	void ApplyImpulseAtTip(FVector Impulse, bool bVelChange = false);

	/** 接点がソケット解決できるタイミング（BeginPlay 後など）で呼び出すレイアウト関数 */
	UFUNCTION(BlueprintCallable, Category = "Chain")
	void RefreshLayout();

	UFUNCTION(BlueprintCallable, Category = "Chain")
	bool IsChainBroken() const { return bChainBroken; }

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// ========== コンポーネント ==========

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chain", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chain", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> ChainMesh;

	/** 端点 A 拘束（始端ボーン ⇔ 接点 A 親アクター） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chain", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPhysicsConstraintComponent> EndpointAConstraint;

	/** 端点 B 拘束（終端ボーン ⇔ 接点 B 親アクター） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chain", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPhysicsConstraintComponent> EndpointBConstraint;

	/**
	 * 端点 A の既定アンカー（内部 SceneComponent）。
	 * 外部アクターが未指定のとき、このコンポーネントのワールド位置を端点 A として使う。
	 * BP_Chain のコンポーネントツリーに表示され、ビューポートで自由に移動できる。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chain", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> EndpointAAnchor;

	/** 端点 B の既定アンカー（内部 SceneComponent） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chain", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> EndpointBAnchor;

	// ========== Endpoints ==========

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Endpoints")
	TObjectPtr<AActor> EndpointAActor;



	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Endpoints")
	FName EndpointASocketName = NAME_None;

	/** ソケット（またはアクター原点）からの追加オフセット（ローカル空間） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Endpoints")
	FVector EndpointALocalOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Endpoints")
	bool bUseEndpointB = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Endpoints",
		meta = (EditCondition = "bUseEndpointB"))
	TObjectPtr<AActor> EndpointBActor;



	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Endpoints",
		meta = (EditCondition = "bUseEndpointB"))
	FName EndpointBSocketName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Endpoints",
		meta = (EditCondition = "bUseEndpointB"))
	FVector EndpointBLocalOffset = FVector::ZeroVector;

	// ========== アセット／ボーン名 ==========

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Asset")
	TSoftObjectPtr<USkeletalMesh> ChainSkeletalMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Asset")
	TSoftObjectPtr<UPhysicsAsset> ChainPhysicsAssetOverride;

	/** 始端ボーン（端点 A 側） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Bones")
	FName StartBoneName = FName("Bone");

	/** 終端ボーン（端点 B 側／フレイル運用時の振り回し末端） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Bones")
	FName EndBoneName = FName("Bone_008");

	// ========== 物理設定 ==========

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Physics")
	bool bSimulatePhysics = true;

	/** エディタ（PIE 前）でも姿勢プレビューを行う */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Physics")
	bool bPreviewInEditor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Physics",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PhysicsBlendWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Physics",
		meta = (ClampMin = "0.0"))
	float LinearDamping = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Physics",
		meta = (ClampMin = "0.0"))
	float AngularDamping = 0.05f;

	/** 重力スケール（フレイル振り回し時に重みを増やしたい場合 > 1） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Physics")
	float GravityScale = 1.0f;

	/** 各ボディの質量スケール（重量感） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Physics",
		meta = (ClampMin = "0.01"))
	float MassScale = 1.0f;

	// ========== Constraint パラメータ（A/B 共通） ==========

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Constraint",
		meta = (ClampMin = "0.0"))
	float EndpointLinearLimit = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Constraint",
		meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float EndpointAngularSwingLimit = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Constraint",
		meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float EndpointAngularTwistLimit = 180.0f;

	/** Constraint が破断可能か */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Constraint")
	bool bBreakable = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Constraint",
		meta = (EditCondition = "bBreakable", ClampMin = "0.0"))
	float BreakLinearForce = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Constraint",
		meta = (EditCondition = "bBreakable", ClampMin = "0.0"))
	float BreakAngularForce = 0.0f;

	// ========== デバッグ ==========

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Debug")
	bool bDrawDebugBones = false;

	/** プレビュー時にアンカー位置を球で描画 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chain|Debug")
	bool bDrawDebugAnchors = true;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void SetupChainMesh();
	void EstablishEndpointConstraints();
	void LayoutBetweenEndpoints();

	USkeletalMesh*  ResolveChainSkeletalMesh() const;
	UPhysicsAsset*  ResolveChainPhysicsAsset() const;

	/**
	 * 接点ワールド座標を解決する。優先順位:
	 *   1. InActor が指定されていればソケット/ボーン/アクター原点を解決
	 *   2. それ以外は AnchorFallback（内部アンカー）のワールド位置を返す
	 * 必ず true を返す（アンカーフォールバックで保証）。
	 */
	bool GetEndpointWorld(
		AActor* InActor, FName InSocketName, const FVector& LocalOffset,
		USceneComponent* AnchorFallback,
		FVector& OutWorld, UPrimitiveComponent** OutResolvedComp = nullptr) const;

	/** 接点アクターから「物理結合に使う」コンポーネントとボーン名を解決する */
	void ResolveEndpointBinding(
		AActor* InActor, FName InSocketName,
		UPrimitiveComponent*& OutComp, FName& OutBoneName) const;

	void ConfigureEndpointConstraint(
		UPhysicsConstraintComponent* Cst,
		AActor* EndpointActor, FName EndpointSocket, const FVector& LocalOffset,
		USceneComponent* AnchorFallback,
		FName ChainBone);

	bool bChainBroken = false;
};
