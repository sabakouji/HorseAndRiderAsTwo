#include "ChainSimulationSkeletal.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "ReferenceSkeleton.h"
#include "DrawDebugHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogChainSim, Log, All);

namespace
{
	/** SkeletalMesh のソケット名から付与ボーン名を解決（ボーン名そのものでも可） */
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
AChainSimulationSkeletal::AChainSimulationSkeletal()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	ChainMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ChainMesh"));
	ChainMesh->SetupAttachment(SceneRoot);
	ChainMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	ChainMesh->SetNotifyRigidBodyCollision(true);
	ChainMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	EndpointAConstraint = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("EndpointAConstraint"));
	EndpointBConstraint = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("EndpointBConstraint"));
	EndpointAConstraint->SetupAttachment(SceneRoot);
	EndpointBConstraint->SetupAttachment(SceneRoot);

	// --- 内部アンカー: 外部 Actor 未指定時の既定端点 ---
	// BP_Chain 単体エディタでもビューポートで動かして鎖の挙動を確認できるようにする。
	// 既定位置は X 軸方向に ±100cm（合計 200cm の鎖）。
	EndpointAAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("EndpointAAnchor"));
	EndpointAAnchor->SetupAttachment(SceneRoot);
	EndpointAAnchor->SetRelativeLocation(FVector(-100.0f, 0.0f, 0.0f));

	EndpointBAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("EndpointBAnchor"));
	EndpointBAnchor->SetupAttachment(SceneRoot);
	EndpointBAnchor->SetRelativeLocation(FVector(100.0f, 0.0f, 0.0f));
}

// =====================================================================
// OnConstruction — エディタプレビュー
// =====================================================================
void AChainSimulationSkeletal::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SetupChainMesh();

	if (ChainMesh)
	{
		ChainMesh->SetSimulatePhysics(false);
	}

	if (bPreviewInEditor)
	{
		LayoutBetweenEndpoints();
		EstablishEndpointConstraints();
	}
}

#if WITH_EDITOR
void AChainSimulationSkeletal::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (bPreviewInEditor)
	{
		if (ChainMesh) { ChainMesh->SetSimulatePhysics(false); }
		LayoutBetweenEndpoints();
		EstablishEndpointConstraints();
	}
}
#endif

// =====================================================================
// BeginPlay
// =====================================================================
void AChainSimulationSkeletal::BeginPlay()
{
	Super::BeginPlay();

	SetupChainMesh();
	RefreshLayout();
}

// =====================================================================
// Tick — Constraint 追従（接点が動くケース）／デバッグ描画
// =====================================================================
void AChainSimulationSkeletal::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bDrawDebugBones && ChainMesh && ChainMesh->GetSkeletalMeshAsset())
	{
		const FReferenceSkeleton& RefSkel = ChainMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
		const int32 NumBones = RefSkel.GetNum();
		for (int32 i = 0; i < NumBones; ++i)
		{
			const FName BoneName = RefSkel.GetBoneName(i);
			const FVector BoneWorld = ChainMesh->GetBoneLocation(BoneName);
			DrawDebugSphere(GetWorld(), BoneWorld, 2.0f, 8, FColor::Yellow, false, -1.0f, 0, 0.3f);
		}
	}

	if (bDrawDebugAnchors)
	{
		if (EndpointAAnchor)
		{
			DrawDebugSphere(GetWorld(), EndpointAAnchor->GetComponentLocation(), 8.0f, 12, FColor::Green, false, -1.0f, 0, 0.5f);
		}
		if (bUseEndpointB && EndpointBAnchor)
		{
			DrawDebugSphere(GetWorld(), EndpointBAnchor->GetComponentLocation(), 8.0f, 12, FColor::Red, false, -1.0f, 0, 0.5f);
		}
	}
}

// =====================================================================
// 端点設定 / 破断 / リセット
// =====================================================================
void AChainSimulationSkeletal::SetEndpointA(AActor* InActor, FName InSocketName, FVector InLocalOffset)
{
	EndpointAActor       = InActor;
	EndpointASocketName  = InSocketName;
	EndpointALocalOffset = InLocalOffset;
	if (HasActorBegunPlay())
	{
		RefreshLayout();
	}
}

void AChainSimulationSkeletal::SetEndpointB(AActor* InActor, FName InSocketName, FVector InLocalOffset)
{
	EndpointBActor       = InActor;
	EndpointBSocketName  = InSocketName;
	EndpointBLocalOffset = InLocalOffset;
	bUseEndpointB        = (InActor != nullptr);
	if (HasActorBegunPlay())
	{
		RefreshLayout();
	}
}

void AChainSimulationSkeletal::SetEndpointActors(AActor* InActorA, AActor* InActorB)
{
	EndpointAActor = InActorA;
	EndpointBActor = InActorB;
	if (InActorB) { bUseEndpointB = true; }
	if (HasActorBegunPlay())
	{
		RefreshLayout();
	}
}



void AChainSimulationSkeletal::DetachEndpointB()
{
	if (EndpointBConstraint)
	{
		EndpointBConstraint->BreakConstraint();
	}
	bUseEndpointB = false;
}

void AChainSimulationSkeletal::BreakChain()
{
	if (EndpointAConstraint) { EndpointAConstraint->BreakConstraint(); }
	if (EndpointBConstraint) { EndpointBConstraint->BreakConstraint(); }
	bChainBroken = true;
}

void AChainSimulationSkeletal::ResetChain()
{
	bChainBroken = false;
	SetupChainMesh();
	RefreshLayout();
}

void AChainSimulationSkeletal::ApplyImpulseAtTip(FVector Impulse, bool bVelChange)
{
	if (!ChainMesh) { return; }

	// EndBoneName 優先。物理ボディが無い場合は階層を遡って最初に見つかったボディに加える
	if (HasPhysicsBody(ChainMesh, EndBoneName))
	{
		ChainMesh->AddImpulse(Impulse, EndBoneName, bVelChange);
		return;
	}

	if (USkeletalMesh* SK = ChainMesh->GetSkeletalMeshAsset())
	{
		const FReferenceSkeleton& RefSkel = SK->GetRefSkeleton();
		const int32 NumBones = RefSkel.GetNum();
		for (int32 i = NumBones - 1; i >= 0; --i)
		{
			const FName BoneName = RefSkel.GetBoneName(i);
			if (HasPhysicsBody(ChainMesh, BoneName))
			{
				ChainMesh->AddImpulse(Impulse, BoneName, bVelChange);
				return;
			}
		}
	}
}

void AChainSimulationSkeletal::RefreshLayout()
{
	if (!ChainMesh) { return; }

	// 一旦物理 OFF
	ChainMesh->SetSimulatePhysics(false);

	LayoutBetweenEndpoints();

	// 物理 ON
	if (bSimulatePhysics && ChainMesh->GetPhysicsAsset())
	{
		ChainMesh->SetSimulatePhysics(true);
		ChainMesh->SetAllBodiesSimulatePhysics(true);
		ChainMesh->SetAllBodiesPhysicsBlendWeight(PhysicsBlendWeight, true);
		ChainMesh->SetLinearDamping(LinearDamping);
		ChainMesh->SetAngularDamping(AngularDamping);
		ChainMesh->SetEnableGravity(true);
		ChainMesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
		ChainMesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
		ChainMesh->WakeAllRigidBodies();

		// 重力／質量スケール
		if (USkeletalMesh* SK = ChainMesh->GetSkeletalMeshAsset())
		{
			const FReferenceSkeleton& RefSkel = SK->GetRefSkeleton();
			for (int32 i = 0; i < RefSkel.GetNum(); ++i)
			{
				const FName BoneName = RefSkel.GetBoneName(i);
				if (FBodyInstance* BI = ChainMesh->GetBodyInstance(BoneName))
				{
					BI->SetMassScale(MassScale);
					// GravityScale を直接設定する API は限定的なので、Z 加速度補正で代用
					// （厳密な GravityScale が欲しい場合は PhysicsAsset 側で設定する想定）
					BI->bEnableGravity = true;
				}
			}
		}
	}

	EstablishEndpointConstraints();
}

// =====================================================================
// SetupChainMesh — アセット適用と物理シム設定
// =====================================================================
void AChainSimulationSkeletal::SetupChainMesh()
{
	if (!ChainMesh) { return; }

	if (USkeletalMesh* Mesh = ResolveChainSkeletalMesh())
	{
		if (ChainMesh->GetSkeletalMeshAsset() != Mesh)
		{
			ChainMesh->SetSkeletalMeshAsset(Mesh);
		}
	}

	if (UPhysicsAsset* PhysAsset = ResolveChainPhysicsAsset())
	{
		if (ChainMesh->GetPhysicsAsset() != PhysAsset)
		{
			ChainMesh->SetPhysicsAsset(PhysAsset);
		}
	}

	if (bSimulatePhysics && ChainMesh->GetPhysicsAsset())
	{
		ChainMesh->SetSimulatePhysics(true);
		ChainMesh->SetAllBodiesSimulatePhysics(true);
		ChainMesh->SetAllBodiesPhysicsBlendWeight(PhysicsBlendWeight, true);
		ChainMesh->SetLinearDamping(LinearDamping);
		ChainMesh->SetAngularDamping(AngularDamping);
		ChainMesh->WakeAllRigidBodies();
	}
	else
	{
		ChainMesh->SetSimulatePhysics(false);
	}
}

// =====================================================================
// LayoutBetweenEndpoints — 始端／終端を接点に合わせて整列
// =====================================================================
void AChainSimulationSkeletal::LayoutBetweenEndpoints()
{
	if (!ChainMesh) { return; }

	FVector WorldA;
	const bool bHasA = GetEndpointWorld(EndpointAActor, EndpointASocketName, EndpointALocalOffset, EndpointAAnchor, WorldA);

	FVector WorldB;
	const bool bHasB = bUseEndpointB && GetEndpointWorld(EndpointBActor, EndpointBSocketName, EndpointBLocalOffset, EndpointBAnchor, WorldB);

	if (!bHasA && !bHasB)
	{
		// 接点なし: コンポーネントのトランスフォームをリセット
		ChainMesh->SetRelativeLocation(FVector::ZeroVector);
		ChainMesh->SetRelativeRotation(FRotator::ZeroRotator);
		ChainMesh->SetRelativeScale3D(FVector::OneVector);
		return;
	}

	USkeletalMesh* CableSK = ChainMesh->GetSkeletalMeshAsset();
	if (!CableSK)
	{
		UE_LOG(LogChainSim, Warning, TEXT("[Chain] SkeletalMesh が未設定です"));
		return;
	}

	const FReferenceSkeleton& RefSkel = CableSK->GetRefSkeleton();
	const int32 StartIdx = RefSkel.FindBoneIndex(StartBoneName);
	const int32 EndIdx   = RefSkel.FindBoneIndex(EndBoneName);
	if (StartIdx == INDEX_NONE || EndIdx == INDEX_NONE)
	{
		UE_LOG(LogChainSim, Warning,
			TEXT("[Chain] 始端 '%s' / 終端 '%s' が見つかりません"),
			*StartBoneName.ToString(), *EndBoneName.ToString());
		return;
	}

	// レファレンスポーズ → コンポーネント空間
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

	// コンポーネントの初期状態を設定
	ChainMesh->SetRelativeLocation(FVector::ZeroVector);
	ChainMesh->SetRelativeRotation(FRotator::ZeroRotator);
	ChainMesh->SetRelativeScale3D(FVector::OneVector);

	if (bHasA && bHasB)
	{
		// 両端固定: 始端→A、終端→B
		FVector AB = WorldB - WorldA;
		const float ABDist = AB.Size();
		if (ABDist < KINDA_SMALL_NUMBER) { return; }
		AB /= ABDist;

		const FQuat AlignQuat   = FQuat::FindBetweenNormals(ChainDir, AB);
		const float ScaleFactor = ABDist / ChainLen;

		const FVector StartScaled       = StartLocal * ScaleFactor;
		const FVector StartWorldOffset  = AlignQuat.RotateVector(StartScaled);
		const FVector MeshWorldLocation = WorldA - StartWorldOffset;

		ChainMesh->SetRelativeScale3D(FVector(ScaleFactor));
		ChainMesh->SetWorldLocationAndRotation(MeshWorldLocation, AlignQuat,
			/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	}
	else if (bHasA)
	{
		// 片端 A のみ: 重力方向に垂らす
		const FVector Down = FVector(0, 0, -1);
		const FQuat AlignQuat = FQuat::FindBetweenNormals(ChainDir, Down);

		// スケールは維持（アクターのスケールを基準にする）
		const float ParentScale = GetActorScale3D().X;
		const FVector StartLocalWorld = AlignQuat.RotateVector(StartLocal * ParentScale);
		const FVector MeshWorldLocation = WorldA - StartLocalWorld;

		ChainMesh->SetRelativeScale3D(FVector(ParentScale));
		ChainMesh->SetWorldLocationAndRotation(MeshWorldLocation, AlignQuat,
			/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	}
	else if (bHasB)
	{
		// 片端 B のみ: 同様に重力方向（終端→B）
		const FVector Up = FVector(0, 0, 1);
		const FQuat AlignQuat = FQuat::FindBetweenNormals(ChainDir, -Up);

		const float ParentScale = GetActorScale3D().X;
		const FVector EndLocalWorld = AlignQuat.RotateVector(EndLocal * ParentScale);
		const FVector MeshWorldLocation = WorldB - EndLocalWorld;

		ChainMesh->SetRelativeScale3D(FVector(ParentScale));
		ChainMesh->SetWorldLocationAndRotation(MeshWorldLocation, AlignQuat,
			/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	}

	UE_LOG(LogChainSim, Log,
		TEXT("[Chain] Layout: hasA=%d hasB=%d ChainLen=%.2f MeshLoc=%s MeshScale=%s"),
		bHasA, bHasB, ChainLen, *ChainMesh->GetComponentLocation().ToString(), *ChainMesh->GetComponentScale().ToString());
}

// =====================================================================
// EstablishEndpointConstraints
// =====================================================================
void AChainSimulationSkeletal::EstablishEndpointConstraints()
{
	if (!ChainMesh) { return; }

	// 解決順: 外部 Actor → GetParentActor() → this（内部アンカーへ Constraint を貼る）
	AActor* ResolvedA = EndpointAActor ? EndpointAActor.Get() : GetParentActor();
	if (!ResolvedA) { ResolvedA = this; }
	if (ResolvedA && EndpointAConstraint)
	{
		ConfigureEndpointConstraint(EndpointAConstraint, ResolvedA, EndpointASocketName, EndpointALocalOffset,
			/*AnchorFallback=*/EndpointAAnchor, StartBoneName);
	}

	AActor* ResolvedB = EndpointBActor ? EndpointBActor.Get() : GetParentActor();
	if (!ResolvedB) { ResolvedB = this; }
	if (bUseEndpointB && ResolvedB && EndpointBConstraint)
	{
		ConfigureEndpointConstraint(EndpointBConstraint, ResolvedB, EndpointBSocketName, EndpointBLocalOffset,
			/*AnchorFallback=*/EndpointBAnchor, EndBoneName);
	}
}

void AChainSimulationSkeletal::ConfigureEndpointConstraint(
	UPhysicsConstraintComponent* Cst,
	AActor* EndpointActor, FName EndpointSocket, const FVector& LocalOffset,
	USceneComponent* AnchorFallback,
	FName ChainBone)
{
	if (!Cst || !ChainMesh) { return; }

	if (!HasPhysicsBody(ChainMesh, ChainBone))
	{
		UE_LOG(LogChainSim, Warning,
			TEXT("[Chain] PhysicsAsset に '%s' の物理ボディがありません"),
			*ChainBone.ToString());
		return;
	}

	// --- 結合相手のコンポーネント / ボーンを解決 ---
	UPrimitiveComponent* EndpointComp = nullptr;
	FName EndpointBone = NAME_None;
	if (EndpointActor && EndpointActor != this)
	{
		ResolveEndpointBinding(EndpointActor, EndpointSocket, EndpointComp, EndpointBone);
		if (!EndpointSocket.IsNone() && EndpointBone.IsNone())
		{
			EndpointComp = nullptr;  // ソケット解決失敗 → ワールド固定にフォールバック
		}
	}

	// --- ① Constraint を「結合点に追従するコンポーネント」へ付け替える ---
	USceneComponent* AttachParent = nullptr;
	FName            AttachSocket = NAME_None;

	if (EndpointComp)
	{
		AttachParent = EndpointComp;
		// SkeletalMeshComponent はソケット名 / ボーン名どちらでもアタッチ可能
		if (!EndpointSocket.IsNone() &&
			(EndpointComp->DoesSocketExist(EndpointSocket)))
		{
			AttachSocket = EndpointSocket;
		}
	}
	else if (AnchorFallback)
	{
		AttachParent = AnchorFallback;
	}
	else
	{
		AttachParent = SceneRoot;  // 最後の砦
	}

	// 既存のアタッチ状態と異なる場合のみ付け替え
	const bool bNeedReattach =
		(Cst->GetAttachParent() != AttachParent) ||
		(Cst->GetAttachSocketName() != AttachSocket);
	if (bNeedReattach)
	{
		Cst->AttachToComponent(
			AttachParent,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			AttachSocket);
	}

	// --- ② LocalOffset を相対位置として適用 ---
	Cst->SetRelativeLocation(LocalOffset);
	Cst->SetRelativeRotation(FRotator::ZeroRotator);

	// --- 制限・破断設定 ---
	Cst->SetLinearXLimit(LCM_Locked, EndpointLinearLimit);
	Cst->SetLinearYLimit(LCM_Locked, EndpointLinearLimit);
	Cst->SetLinearZLimit(LCM_Locked, EndpointLinearLimit);
	Cst->SetAngularSwing1Limit(ACM_Free, EndpointAngularSwingLimit);
	Cst->SetAngularSwing2Limit(ACM_Free, EndpointAngularSwingLimit);
	Cst->SetAngularTwistLimit(ACM_Free, EndpointAngularTwistLimit);
	Cst->SetDisableCollision(true);

	if (bBreakable)
	{
		Cst->SetLinearBreakable(BreakLinearForce > 0.0f, BreakLinearForce);
		Cst->SetAngularBreakable(BreakAngularForce > 0.0f, BreakAngularForce);
	}
	else
	{
		Cst->SetLinearBreakable(false, 0.0f);
		Cst->SetAngularBreakable(false, 0.0f);
	}

	// --- ③ アタッチで結合点に居る状態のまま、結合確定 ---
	// (この時点での Constraint のワールド姿勢が拘束フレームになる)
	Cst->SetConstrainedComponents(
		EndpointComp, EndpointBone,
		ChainMesh, ChainBone);

	// デバッグ用ログ
	UE_LOG(LogChainSim, Log,
		TEXT("[Chain] Constraint configured: Parent=%s Socket=%s LocalOffset=%s ChainBone=%s WorldLoc=%s"),
		AttachParent ? *AttachParent->GetName() : TEXT("null"),
		*AttachSocket.ToString(),
		*LocalOffset.ToString(),
		*ChainBone.ToString(),
		*Cst->GetComponentLocation().ToString());
}

// =====================================================================
// 端点ワールド座標解決
// =====================================================================
bool AChainSimulationSkeletal::GetEndpointWorld(
	AActor* InActor, FName InSocketName, const FVector& LocalOffset,
	USceneComponent* AnchorFallback,
	FVector& OutWorld, UPrimitiveComponent** OutResolvedComp) const
{
	if (OutResolvedComp) { *OutResolvedComp = nullptr; }

	// アクター指定からの解決
	AActor* TargetActor = InActor ? InActor : GetParentActor();

	// 親アクターも無い場合は内部アンカーへフォールバック
	if (!TargetActor)
	{
		if (AnchorFallback)
		{
			OutWorld = AnchorFallback->GetComponentTransform().TransformPosition(LocalOffset);
			return true;
		}
		return false;
	}

	// --- ソケット指定がある場合 ---
	if (!InSocketName.IsNone())
	{
		// すべての SkeletalMeshComponent から検索
		TArray<USkeletalMeshComponent*> SkelMeshes;
		TargetActor->GetComponents<USkeletalMeshComponent>(SkelMeshes);
		for (USkeletalMeshComponent* SMC : SkelMeshes)
		{
			if (SMC && (SMC->DoesSocketExist(InSocketName) || SMC->GetBoneIndex(InSocketName) != INDEX_NONE))
			{
				const FTransform SocketWS = SMC->GetSocketTransform(InSocketName, RTS_World);
				OutWorld = SocketWS.TransformPosition(LocalOffset);
				if (OutResolvedComp) { *OutResolvedComp = SMC; }
				return true;
			}
		}

		// その他 PrimitiveComponent のソケット
		TArray<UPrimitiveComponent*> Prims;
		TargetActor->GetComponents<UPrimitiveComponent>(Prims);
		for (UPrimitiveComponent* P : Prims)
		{
			if (P && P->DoesSocketExist(InSocketName))
			{
				const FTransform SocketWS = P->GetSocketTransform(InSocketName, RTS_World);
				OutWorld = SocketWS.TransformPosition(LocalOffset);
				if (OutResolvedComp) { *OutResolvedComp = P; }
				return true;
			}
		}

		UE_LOG(LogChainSim, Verbose,
			TEXT("[Chain] ソケット '%s' が '%s' に見つかりません。アンカー/アクター原点を使用します。"),
			*InSocketName.ToString(), *TargetActor->GetName());

		if (AnchorFallback)
		{
			OutWorld = AnchorFallback->GetComponentTransform().TransformPosition(LocalOffset);
			return true;
		}
	}

	// --- フォールバック: アクター原点 + LocalOffset ---
	OutWorld = TargetActor->GetActorTransform().TransformPosition(LocalOffset);
	if (OutResolvedComp)
	{
		*OutResolvedComp = TargetActor->FindComponentByClass<UPrimitiveComponent>();
	}
	return true;
}

// =====================================================================
// 端点バインディング（Constraint に渡す Component + Bone 名）
// =====================================================================
void AChainSimulationSkeletal::ResolveEndpointBinding(
	AActor* InActor, FName InSocketName,
	UPrimitiveComponent*& OutComp, FName& OutBoneName) const
{
	OutComp     = nullptr;
	OutBoneName = NAME_None;

	// アクター指定からの解決
	if (!InActor) { return; }

	// すべての SkeletalMeshComponent から検索
	TArray<USkeletalMeshComponent*> SkelMeshes;
	InActor->GetComponents<USkeletalMeshComponent>(SkelMeshes);
	for (USkeletalMeshComponent* SMC : SkelMeshes)
	{
		if (SMC && (InSocketName.IsNone() || SMC->DoesSocketExist(InSocketName) || SMC->GetBoneIndex(InSocketName) != INDEX_NONE))
		{
			OutComp = SMC;
			if (!InSocketName.IsNone())
			{
				OutBoneName = ResolveSocketParentBone(SMC, InSocketName);
			}
			return;
		}
	}

	// それ以外: 最初に見つかった PrimitiveComponent
	TArray<UPrimitiveComponent*> Prims;
	InActor->GetComponents<UPrimitiveComponent>(Prims);
	for (UPrimitiveComponent* P : Prims)
	{
		if (P)
		{
			OutComp = P;
			return;
		}
	}
}

// =====================================================================
// アセット解決
// =====================================================================
USkeletalMesh* AChainSimulationSkeletal::ResolveChainSkeletalMesh() const
{
	if (ChainSkeletalMesh.IsNull()) { return nullptr; }
	if (USkeletalMesh* Loaded = ChainSkeletalMesh.Get())
	{
		return Loaded;
	}
	return ChainSkeletalMesh.LoadSynchronous();
}

UPhysicsAsset* AChainSimulationSkeletal::ResolveChainPhysicsAsset() const
{
	if (ChainPhysicsAssetOverride.IsNull()) { return nullptr; }
	if (UPhysicsAsset* Loaded = ChainPhysicsAssetOverride.Get())
	{
		return Loaded;
	}
	return ChainPhysicsAssetOverride.LoadSynchronous();
}
