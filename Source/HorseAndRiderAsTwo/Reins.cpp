#include "Reins.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/EngineTypes.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "ReferenceSkeleton.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogReins, Log, All);

namespace
{
	/** SkeletalMeshComponent から socket が付与されているボーン名を解決する */
	FName ResolveSocketParentBone(const USkeletalMeshComponent* Mesh, FName SocketOrBone)
	{
		if (!Mesh || SocketOrBone.IsNone()) { return NAME_None; }
		if (Mesh->GetBoneIndex(SocketOrBone) != INDEX_NONE) { return SocketOrBone; }
		if (const USkeletalMeshSocket* Socket = Mesh->GetSocketByName(SocketOrBone))
		{
			return Socket->BoneName;
		}
		const FName BoneFromSocket = Mesh->GetSocketBoneName(SocketOrBone);
		if (!BoneFromSocket.IsNone()) { return BoneFromSocket; }
		return NAME_None;
	}

	/** PhysicsAsset に対象ボーンの物理ボディが存在するか */
	bool HasPhysicsBody(const USkeletalMeshComponent* Mesh, FName BoneName)
	{
		if (!Mesh || BoneName.IsNone()) { return false; }
		const UPhysicsAsset* PA = Mesh->GetPhysicsAsset();
		if (!PA) { return false; }
		return PA->FindBodyIndex(BoneName) != INDEX_NONE;
	}
}

// =====================================================================
// コンストラクタ
// =====================================================================
AReins::AReins()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// --- Cable スケルタルメッシュ ---
	ReinsMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ReinsMesh"));
	ReinsMesh->SetupAttachment(SceneRoot);
	ReinsMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	ReinsMesh->SetNotifyRigidBodyCollision(true);
	ReinsMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	// --- 3 本の PhysicsConstraint ---
	LeftBridleConstraint  = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("LeftBridleConstraint"));
	RightBridleConstraint = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("RightBridleConstraint"));
	HandConstraint        = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("HandConstraint"));
	LeftBridleConstraint->SetupAttachment(SceneRoot);
	RightBridleConstraint->SetupAttachment(SceneRoot);
	HandConstraint->SetupAttachment(SceneRoot);

	// --- 既定のスケルタルメッシュアセット指定 ---
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> DefaultCableFinder(
		TEXT("/Game/3DModel/Cable.Cable"));
	if (DefaultCableFinder.Succeeded())
	{
		CableSkeletalMesh = DefaultCableFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UPhysicsAsset> DefaultPhysAssetFinder(
		TEXT("/Game/3DModel/Cable_PhysicsAsset.Cable_PhysicsAsset"));
	if (DefaultPhysAssetFinder.Succeeded())
	{
		CablePhysicsAssetOverride = DefaultPhysAssetFinder.Object;
	}
}

// =====================================================================
// OnConstruction — エディタ上での見た目反映
// =====================================================================
void AReins::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SetupReinsMesh();

	// エディタプレビューでは物理シムを無効化（PIE/BeginPlay 時に再有効化）
	if (ReinsMesh)
	{
		ReinsMesh->SetSimulatePhysics(false);
	}

	// 親アクタが AHorseCharacter（ChildActorComponent 経由）の場合、
	// その SkeletalMesh を引いて BridleSocket_L/R にレイアウトする。
	if (AActor* Parent = GetParentActor())
	{
		if (USkeletalMeshComponent* HorseMesh = Parent->FindComponentByClass<USkeletalMeshComponent>())
		{
			PreviewLayoutAtBridleSockets(HorseMesh);
		}
	}
}

// =====================================================================
// BeginPlay
// =====================================================================
void AReins::BeginPlay()
{
	Super::BeginPlay();

	SetupReinsMesh();
}

// =====================================================================
// Tick — 衝撃蓄積の減衰
// =====================================================================
void AReins::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bReinsBroken && ImpactAccumulator > 0.0f)
	{
		ImpactAccumulator = FMath::Max(0.0f, ImpactAccumulator - ImpactDecayPerSec * DeltaTime);
	}
}

// =====================================================================
// Initialize — 馬側のセットアップ
// =====================================================================
void AReins::Initialize(USkeletalMeshComponent* HorseMesh)
{
	if (!HorseMesh) { return; }
	CachedHorseMesh = HorseMesh;

	SetupReinsMesh();

	// 物理開始前に始端・終端を BridleSocket_L/R に合わせて配置
	PreviewLayoutAtBridleSockets(HorseMesh);

	// 物理シムを有効化（PreviewLayout が物理を切るため、Initialize で再 ON）
	if (bSimulateReinsPhysics && ReinsMesh && ReinsMesh->GetPhysicsAsset())
	{
		ReinsMesh->SetSimulatePhysics(true);
		ReinsMesh->SetAllBodiesSimulatePhysics(true);
		ReinsMesh->SetAllBodiesPhysicsBlendWeight(ReinsPhysicsBlendWeight, true);
		ReinsMesh->WakeAllRigidBodies();
	}

	EstablishBridleConstraints();

	if (CachedJockeyMesh && !bReinsBroken)
	{
		EstablishHandConstraint();
	}
}

// =====================================================================
// AttachToJockey — Hand Constraint 接続
// =====================================================================
void AReins::AttachToJockey(USkeletalMeshComponent* JockeyMesh)
{
	if (!JockeyMesh) { return; }
	CachedJockeyMesh = JockeyMesh;

	bReinsBroken = false;
	ImpactAccumulator = 0.0f;

	EstablishHandConstraint();
}

// =====================================================================
// DetachFromJockey — ジョッキーが手を離す（HandConstraint のみ Break）
// =====================================================================
void AReins::DetachFromJockey()
{
	if (HandConstraint)
	{
		HandConstraint->BreakConstraint();
	}
	bReinsBroken = true;
}

// =====================================================================
// AddImpactToReins — 衝撃蓄積、閾値超で手離し
// =====================================================================
void AReins::AddImpactToReins(float Magnitude)
{
	if (bReinsBroken) { return; }
	if (Magnitude <= 0.0f) { return; }

	ImpactAccumulator += Magnitude;
	if (ImpactAccumulator >= ImpactAccumThreshold)
	{
		DetachFromJockey();
	}
}

// =====================================================================
// ResetCables — 物理リセット＋必要に応じ再接続
// =====================================================================
void AReins::ResetCables()
{
	SetupReinsMesh();

	if (CachedHorseMesh)
	{
		const FVector HeadWorld = CachedHorseMesh->GetSocketLocation(HorseHeadBoneName);
		if (ReinsMesh && !HeadWorld.IsNearlyZero())
		{
			ReinsMesh->SetWorldLocation(HeadWorld, /*bSweep=*/false, /*OutSweepHit=*/nullptr, ETeleportType::TeleportPhysics);
		}
		EstablishBridleConstraints();
	}

	bReinsBroken = false;
	ImpactAccumulator = 0.0f;

	if (CachedJockeyMesh)
	{
		EstablishHandConstraint();
	}
}

// =====================================================================
// GetJointPoint — 互換 API
// =====================================================================
USceneComponent* AReins::GetJointPoint() const
{
	return ReinsMesh;
}

// =====================================================================
// SetupReinsMesh — SkeletalMesh / PhysicsAsset / 物理シム
// =====================================================================
void AReins::SetupReinsMesh()
{
	if (!ReinsMesh) { return; }

	if (USkeletalMesh* Mesh = ResolveCableSkeletalMesh())
	{
		if (ReinsMesh->GetSkeletalMeshAsset() != Mesh)
		{
			ReinsMesh->SetSkeletalMeshAsset(Mesh);
		}
	}

	if (UPhysicsAsset* PhysAsset = ResolveCablePhysicsAsset())
	{
		if (ReinsMesh->GetPhysicsAsset() != PhysAsset)
		{
			ReinsMesh->SetPhysicsAsset(PhysAsset);
		}
	}

	if (bSimulateReinsPhysics && ReinsMesh->GetPhysicsAsset())
	{
		ReinsMesh->SetSimulatePhysics(true);
		ReinsMesh->SetAllBodiesSimulatePhysics(true);
		ReinsMesh->SetAllBodiesPhysicsBlendWeight(ReinsPhysicsBlendWeight, true);
		ReinsMesh->WakeAllRigidBodies();
	}
	else
	{
		ReinsMesh->SetSimulatePhysics(false);
	}

	// 診断ログ — 実際のスケルトンボーン名と PhysicsAsset の Body 有無を一度だけ表示
	if (GetWorld() && GetWorld()->IsGameWorld() && ReinsMesh->GetSkeletalMeshAsset())
	{
		const FReferenceSkeleton& RefSkel = ReinsMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
		const int32 NumBones = RefSkel.GetNum();
		UE_LOG(LogReins, Log, TEXT("[Reins] Cable Skeleton bones (%d):"), NumBones);
		for (int32 i = 0; i < NumBones; ++i)
		{
			const FName BoneName = RefSkel.GetBoneName(i);
			const bool bHasBody = HasPhysicsBody(ReinsMesh, BoneName);
			UE_LOG(LogReins, Log, TEXT("  [%d] '%s' (Body: %s)"),
				i, *BoneName.ToString(), bHasBody ? TEXT("OK") : TEXT("MISSING"));
		}
	}
}

// =====================================================================
// EstablishBridleConstraints — 馬左右ソケット ⇔ Cable 両端
// =====================================================================
void AReins::EstablishBridleConstraints()
{
	if (!CachedHorseMesh || !ReinsMesh) { return; }

	auto ConfigureBridle = [&](UPhysicsConstraintComponent* Cst, FName HorseSocketName, FName CableBone)
	{
		if (!Cst) { return; }

		// ソケットは Constraint API では使えないので、ソケットが付与されたボーン名を解決する
		const FName HorseBone = ResolveSocketParentBone(CachedHorseMesh, HorseSocketName);
		if (HorseBone.IsNone())
		{
			UE_LOG(LogReins, Warning,
				TEXT("[Reins] BridleSocket '%s' から馬ボーンを解決できません。Constraint 接続をスキップします。"),
				*HorseSocketName.ToString());
			return;
		}

		// PhysicsAsset の物理ボディ有無を検証（無いと Constraint は接続できない）
		if (!HasPhysicsBody(ReinsMesh, CableBone))
		{
			UE_LOG(LogReins, Warning,
				TEXT("[Reins] Cable PhysicsAsset に '%s' の物理ボディがありません。PhysicsAsset に Body を追加してください。"),
				*CableBone.ToString());
			return;
		}

		Cst->SetLinearXLimit(LCM_Locked, BridleLinearLimit);
		Cst->SetLinearYLimit(LCM_Locked, BridleLinearLimit);
		Cst->SetLinearZLimit(LCM_Locked, BridleLinearLimit);

		Cst->SetAngularSwing1Limit(ACM_Free, BridleAngularSwingLimit);
		Cst->SetAngularSwing2Limit(ACM_Free, BridleAngularSwingLimit);
		Cst->SetAngularTwistLimit(ACM_Free, BridleAngularTwistLimit);

		Cst->SetDisableCollision(true);

		// Constraint のワールド位置を馬ソケット位置に合わせる（物理 Teleport）
		const FVector SocketWorld = CachedHorseMesh->GetSocketLocation(HorseSocketName);
		Cst->SetWorldLocation(SocketWorld, /*bSweep=*/false, /*OutSweepHit=*/nullptr, ETeleportType::TeleportPhysics);

		Cst->SetConstrainedComponents(
			CachedHorseMesh, HorseBone,
			ReinsMesh, CableBone);
	};

	ConfigureBridle(LeftBridleConstraint,  LeftBridleSocketName,  CableStartBoneName);
	ConfigureBridle(RightBridleConstraint, RightBridleSocketName, CableEndBoneName);
}

// =====================================================================
// EstablishHandConstraint — Cable 中央 ⇔ ジョッキー左手
// =====================================================================
void AReins::EstablishHandConstraint()
{
	if (!HandConstraint) { return; }
	if (!CachedJockeyMesh || !ReinsMesh) { return; }

	if (!HasPhysicsBody(ReinsMesh, CableHandGripBoneName))
	{
		UE_LOG(LogReins, Warning,
			TEXT("[Reins] Cable PhysicsAsset に '%s' の物理ボディがありません。PhysicsAsset に Body を追加してください。"),
			*CableHandGripBoneName.ToString());
		return;
	}
	if (!HasPhysicsBody(CachedJockeyMesh, JockeyHandBoneName))
	{
		UE_LOG(LogReins, Warning,
			TEXT("[Reins] Jockey PhysicsAsset に '%s' の物理ボディがありません。"),
			*JockeyHandBoneName.ToString());
		return;
	}

	HandConstraint->SetLinearXLimit(LCM_Locked, HandLinearLimit);
	HandConstraint->SetLinearYLimit(LCM_Locked, HandLinearLimit);
	HandConstraint->SetLinearZLimit(LCM_Locked, HandLinearLimit);

	HandConstraint->SetAngularSwing1Limit(ACM_Free, HandAngularSwingLimit);
	HandConstraint->SetAngularSwing2Limit(ACM_Free, HandAngularSwingLimit);
	HandConstraint->SetAngularTwistLimit(ACM_Free, HandAngularTwistLimit);

	HandConstraint->SetDisableCollision(true);

	const FVector HandWorld = CachedJockeyMesh->GetBoneLocation(JockeyHandBoneName);
	if (!HandWorld.IsNearlyZero())
	{
		HandConstraint->SetWorldLocation(HandWorld, /*bSweep=*/false, /*OutSweepHit=*/nullptr, ETeleportType::TeleportPhysics);
	}

	HandConstraint->SetConstrainedComponents(
		ReinsMesh, CableHandGripBoneName,
		CachedJockeyMesh, JockeyHandBoneName);
}

// =====================================================================
// アセット解決
// =====================================================================
USkeletalMesh* AReins::ResolveCableSkeletalMesh() const
{
	if (CableSkeletalMesh.IsNull()) { return nullptr; }
	if (USkeletalMesh* Loaded = CableSkeletalMesh.Get())
	{
		return Loaded;
	}
	return CableSkeletalMesh.LoadSynchronous();
}

// =====================================================================
// PreviewLayoutAtBridleSockets — 始端・終端を L/R ソケットへ合わせる
// =====================================================================
void AReins::PreviewLayoutAtBridleSockets(USkeletalMeshComponent* HorseMesh)
{
	if (!HorseMesh || !ReinsMesh) { return; }

	// レイアウト中は物理を切る（始端・終端のワールド位置をテレポートで確定させるため）
	ReinsMesh->SetSimulatePhysics(false);

	// 馬側ソケットの存在検証
	if (!HorseMesh->DoesSocketExist(LeftBridleSocketName) ||
		!HorseMesh->DoesSocketExist(RightBridleSocketName))
	{
		UE_LOG(LogReins, Warning,
			TEXT("[Reins] 馬スケルトンに BridleSocket_L または BridleSocket_R が存在しません"));
		return;
	}

	// 馬側ソケットのワールド位置（始点=L、終点=R）
	const FVector LWorld = HorseMesh->GetSocketLocation(LeftBridleSocketName);
	const FVector RWorld = HorseMesh->GetSocketLocation(RightBridleSocketName);

	FVector LR = RWorld - LWorld;
	const float SocketDist = LR.Size();
	if (SocketDist < KINDA_SMALL_NUMBER) { return; }
	LR /= SocketDist;

	// Cable のレファレンス・スケルトンからコンポーネント空間でのチェーン方向を実測
	// （OnConstruction 時点では SkeletalMeshComponent の bone transforms が
	//   まだ更新されていない可能性があるため、SkeletalMesh アセット側を直接参照する）
	USkeletalMesh* CableSK = ReinsMesh->GetSkeletalMeshAsset();
	if (!CableSK)
	{
		UE_LOG(LogReins, Warning, TEXT("[Reins] Cable SkeletalMesh が未設定です"));
		return;
	}
	const FReferenceSkeleton& RefSkel = CableSK->GetRefSkeleton();
	const int32 StartIdx = RefSkel.FindBoneIndex(CableStartBoneName);
	const int32 EndIdx   = RefSkel.FindBoneIndex(CableEndBoneName);
	if (StartIdx == INDEX_NONE || EndIdx == INDEX_NONE)
	{
		UE_LOG(LogReins, Warning,
			TEXT("[Reins] Cable に始端 '%s' / 終端 '%s' のボーンが見つかりません"),
			*CableStartBoneName.ToString(), *CableEndBoneName.ToString());
		return;
	}

	// レファレンスポーズ（ローカル）→ コンポーネント空間へ畳み込む
	const TArray<FTransform>& LocalPose = RefSkel.GetRefBonePose();
	TArray<FTransform> CSTransforms;
	CSTransforms.SetNum(LocalPose.Num());
	for (int32 i = 0; i < LocalPose.Num(); ++i)
	{
		const int32 ParentIdx = RefSkel.GetParentIndex(i);
		CSTransforms[i] = (ParentIdx == INDEX_NONE)
			? LocalPose[i]
			: LocalPose[i] * CSTransforms[ParentIdx];
	}
	const FVector StartLocal = CSTransforms[StartIdx].GetLocation();
	const FVector EndLocal   = CSTransforms[EndIdx].GetLocation();
	FVector ChainDir = EndLocal - StartLocal;
	const float ChainLen = ChainDir.Size();
	if (ChainLen < KINDA_SMALL_NUMBER) { return; }
	ChainDir /= ChainLen;

	// チェーン方向を L→R ソケット方向に揃える回転
	const FQuat AlignQuat = FQuat::FindBetweenNormals(ChainDir, LR);

	// 始端〜終端の距離がソケット間距離に一致するように一様スケール
	const float ScaleFactor = SocketDist / ChainLen;

	// アクター transform を算出:
	//   World(Start) = LWorld となるよう、回転とスケールを掛けたローカル始端位置を相殺
	const FVector StartLocalScaled = StartLocal * ScaleFactor;
	const FVector StartLocalWorldOffset = AlignQuat.RotateVector(StartLocalScaled);
	const FVector ActorLocation = LWorld - StartLocalWorldOffset;

	SetActorScale3D(FVector(ScaleFactor));
	SetActorLocationAndRotation(
		ActorLocation,
		AlignQuat,
		/*bSweep=*/false,
		/*OutSweepHit=*/nullptr,
		ETeleportType::TeleportPhysics);

	UE_LOG(LogReins, Log,
		TEXT("[Reins] PreviewLayout: L=%s R=%s ChainLen=%.2f SocketDist=%.2f Scale=%.3f ActorLoc=%s"),
		*LWorld.ToString(), *RWorld.ToString(), ChainLen, SocketDist, ScaleFactor, *ActorLocation.ToString());
}

UPhysicsAsset* AReins::ResolveCablePhysicsAsset() const
{
	if (CablePhysicsAssetOverride.IsNull()) { return nullptr; }
	if (UPhysicsAsset* Loaded = CablePhysicsAssetOverride.Get())
	{
		return Loaded;
	}
	return CablePhysicsAssetOverride.LoadSynchronous();
}
