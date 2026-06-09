#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Reins.generated.h"

class USceneComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UPhysicsAsset;
class UPhysicsConstraintComponent;

/**
 * AReins
 * 馬とジョッキーを物理的に接続する唯一の手綱アクター。
 *
 * 構造（1 本の SkeletalMesh + 3 本の PhysicsConstraint）:
 *
 *   [HorseMesh:BridleSocket_L] ─LeftBridleConstraint─→ [ReinsMesh:Bone (始端)]
 *                                                            ↓ 物理シミュレート
 *                                                      [Bone.001..Bone.003]
 *                                                            ↓
 *                                                      [ReinsMesh:Bone.004 (中央)] ─HandConstraint─→ [JockeyMesh:hand_l]
 *                                                            ↓
 *                                                      [Bone.005..Bone.007]
 *                                                            ↓
 *   [HorseMesh:BridleSocket_R] ─RightBridleConstraint─→ [ReinsMesh:Bone.008 (終端)]
 *
 *   ・ReinsMesh は Cable.fbx (9 ボーン 1 本鎖) を 1 体配置し PhysicsAsset で物理シミュレート
 *   ・左右ブライドル Constraint は常時維持（手綱はちぎれない）
 *   ・HandConstraint は AddImpactToReins 蓄積閾値超 or DetachFromJockey で BreakConstraint
 *     （= ジョッキーが手を離す表現）
 */
UCLASS()
class HORSEANDRIDERASTWO_API AReins : public AActor
{
	GENERATED_BODY()

public:
	AReins();

	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Reins")
	void Initialize(USkeletalMeshComponent* HorseMesh);

	UFUNCTION(BlueprintCallable, Category = "Reins")
	void AttachToJockey(USkeletalMeshComponent* JockeyMesh);

	UFUNCTION(BlueprintCallable, Category = "Reins")
	void DetachFromJockey();

	UFUNCTION(BlueprintCallable, Category = "Reins")
	void AddImpactToReins(float Magnitude);

	UFUNCTION(BlueprintCallable, Category = "Reins")
	void ResetCables();

	/**
	 * エディタプレビュー / 実行時の物理開始前に、
	 * 手綱の始端 (CableStartBoneName) と終端 (CableEndBoneName) を
	 * 馬の BridleSocket_L / BridleSocket_R に合わせて配置する。
	 *
	 * - 物理は OFF にしてからレイアウトを行う（再開は Initialize 側で行う）
	 * - Cable.fbx の骨方向は Blender Y 軸基準。コンポーネント空間でのチェーン方向を
	 *   実測してから L→R ソケット方向に揃える
	 * - 始端〜終端の距離がソケット間距離に一致するようにアクタースケールを調整
	 */
	UFUNCTION(BlueprintCallable, Category = "Reins")
	void PreviewLayoutAtBridleSockets(USkeletalMeshComponent* HorseMesh);

	UFUNCTION(BlueprintCallable, Category = "Reins")
	bool IsReinsBroken() const { return bReinsBroken; }

	/** 互換 API: 旧 JointPoint の代わりに ReinsMesh を返す */
	UFUNCTION(BlueprintCallable, Category = "Reins")
	USceneComponent* GetJointPoint() const;

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// =====================================================================
	// コンポーネント
	// =====================================================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Reins", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	/** Cable.fbx スケルタルメッシュ本体（PhysicsAsset で物理シミュレート） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Reins", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> ReinsMesh;

	/** 馬左ソケット ⇔ Cable 始端 (Bone) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Reins", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPhysicsConstraintComponent> LeftBridleConstraint;

	/** 馬右ソケット ⇔ Cable 終端 (Bone.008) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Reins", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPhysicsConstraintComponent> RightBridleConstraint;

	/** Cable 中央 (Bone.004) ⇔ ジョッキー hand_l */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Reins", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPhysicsConstraintComponent> HandConstraint;

	// =====================================================================
	// ボーン・ソケット名
	// =====================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|Bones")
	FName HorseHeadBoneName = FName("head");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|Bones")
	FName JockeyHandBoneName = FName("hand_l");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|Bones")
	FName LeftBridleSocketName = FName("BridleSocket_L");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|Bones")
	FName RightBridleSocketName = FName("BridleSocket_R");

	/** Cable.fbx 始端ボーン（馬左ブライドル接続側）。FBX インポートでドットがアンダースコアに置換されるため "Bone" を既定とする */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|Bones")
	FName CableStartBoneName = FName("Bone");

	/** Cable.fbx 終端ボーン（馬右ブライドル接続側） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|Bones")
	FName CableEndBoneName = FName("Bone_008");

	/** Cable.fbx 中央ボーン（ジョッキー左手の握り点） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|Bones")
	FName CableHandGripBoneName = FName("Bone_004");

	// =====================================================================
	// アセット
	// =====================================================================

	/** Cable.fbx のスケルタルメッシュアセット (/Game/3DModel/Cable.Cable) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|Asset")
	TSoftObjectPtr<USkeletalMesh> CableSkeletalMesh;

	/** PhysicsAsset の上書き指定（未指定時は SkeletalMesh 既定の PhysicsAsset を使用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|Asset")
	TSoftObjectPtr<UPhysicsAsset> CablePhysicsAssetOverride;

	// =====================================================================
	// 物理設定
	// =====================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|Physics")
	bool bSimulateReinsPhysics = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|Physics",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReinsPhysicsBlendWeight = 1.0f;

	// =====================================================================
	// Bridle Constraint（馬側 2 本／同一パラメータ）
	// =====================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|BridleConstraint",
		meta = (ClampMin = "0.0"))
	float BridleLinearLimit = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|BridleConstraint",
		meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float BridleAngularSwingLimit = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|BridleConstraint",
		meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float BridleAngularTwistLimit = 180.0f;

	// =====================================================================
	// Hand Constraint（中央 1 本）
	// =====================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|HandConstraint",
		meta = (ClampMin = "0.0"))
	float HandLinearLimit = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|HandConstraint",
		meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float HandAngularSwingLimit = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|HandConstraint",
		meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float HandAngularTwistLimit = 180.0f;

	// =====================================================================
	// 衝撃蓄積（手離し用）
	// =====================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|Impact",
		meta = (ClampMin = "0.0"))
	float ImpactAccumThreshold = 8000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reins|Impact",
		meta = (ClampMin = "0.0"))
	float ImpactDecayPerSec = 1500.0f;

private:
	void SetupReinsMesh();
	void EstablishBridleConstraints();
	void EstablishHandConstraint();
	USkeletalMesh* ResolveCableSkeletalMesh() const;
	UPhysicsAsset* ResolveCablePhysicsAsset() const;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> CachedHorseMesh;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> CachedJockeyMesh;

	float ImpactAccumulator = 0.0f;
	bool bReinsBroken = false;
};
