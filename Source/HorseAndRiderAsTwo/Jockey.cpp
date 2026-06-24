#include "Jockey.h"
#include "HorseCharacter.h"
#include "Reins.h"
#include "RopeSimulationSpline.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ChildActorComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

// =====================================================================
// コンストラクタ
// =====================================================================
AJockey::AJockey()
{
	PrimaryActorTick.bCanEverTick = true;

	// --- ルート: カプセルコンポーネント ---
	CapsuleComp = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CapsuleComp"));
	CapsuleComp->InitCapsuleSize(34.0f, 88.0f);
	CapsuleComp->SetCollisionProfileName(TEXT("Pawn"));
	CapsuleComp->SetNotifyRigidBodyCollision(true); // OnHit を発火させる
	// 馬のカメラ SpringArm に引っかからないよう Camera チャンネルを無視
	CapsuleComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	RootComponent = CapsuleComp;

	// --- スケルタルメッシュ ---
	MeshComp = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MeshComp"));
	MeshComp->SetupAttachment(CapsuleComp);
	MeshComp->SetRelativeLocation(FVector(0.0f, 0.0f, -88.0f));
	MeshComp->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	MeshComp->SetCollisionProfileName(TEXT("CharacterMesh"));
	MeshComp->SetNotifyRigidBodyCollision(true);
	// メッシュも Camera チャンネルを無視（ラグドール時もカメラに干渉させない）
	MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
}

// =====================================================================
// BeginPlay
// =====================================================================
void AJockey::BeginPlay()
{
	Super::BeginPlay();

	// 初期（騎乗）状態のメッシュ相対トランスフォームをキャッシュ（ピックアップ復帰用）。
	// 物理シミュレーション開始前のこの時点が正しい着座姿勢の基準となる。
	if (MeshComp)
	{
		InitialMeshRelativeTransform = MeshComp->GetRelativeTransform();
	}

	// OnHit バインド
	if (CapsuleComp)
	{
		CapsuleComp->OnComponentHit.AddDynamic(this, &AJockey::OnCapsuleHit);
	}
	if (MeshComp)
	{
		MeshComp->OnComponentHit.AddDynamic(this, &AJockey::OnMeshHit);
	}
}

// =====================================================================
// Tick ? 衝撃蓄積の減衰とデバッグUI
// =====================================================================
void AJockey::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// --- 蓄積値の自然減衰 ---
	if (!bIsKnockedOut && ImpactAccumulation > 0.0f)
	{
		ImpactAccumulation = FMath::Max(0.0f, ImpactAccumulation - ImpactDecayRate * DeltaTime);
	}

	// --- 衝撃計上の不応期を減算（1衝突1判定のための無視ウィンドウ） ---
	if (ImpactRefractoryTimer > 0.0f)
	{
		ImpactRefractoryTimer = FMath::Max(0.0f, ImpactRefractoryTimer - DeltaTime);
	}

	// --- 射出中の経過時間と着地判定（近接ベース） ---
	if (bLaunchedAsProjectile)
	{
		AirborneTime += DeltaTime;
		UpdateProjectileLanding();
	}

	// --- ヘッドバング攻撃の近接ヒット判定（物理コールバックに依存しない確定判定） ---
	if (HeadbangActiveTimer > 0.0f)
	{
		UpdateHeadbangProximityHit();
	}

	// 鞍ラグドール騎乗中の着座は PhysicsConstraint が物理ソルバ側で保持するため Tick 処理は不要

	// --- 振り回し中のスプリング拘束 ---
	if (bIsSwinging)
	{
		ApplySwingSpring(DeltaTime);
	}

	// --- ヘッドバンキング攻撃の有効時間を減算 ---
	if (HeadbangActiveTimer > 0.0f)
	{
		HeadbangActiveTimer = FMath::Max(0.0f, HeadbangActiveTimer - DeltaTime);
	}

	// --- デバッグ表示 ---
	if (bShowDebugUI)
	{
		DrawDebugUI();
	}
}

// =====================================================================
// 馬にアタッチ（騎乗開始）
// =====================================================================
void AJockey::AttachToHorse(AHorseCharacter* Horse)
{
	// 再騎乗（落馬から拾い直す等）: アクターを鞍ソケットへ再アタッチして鞍ラグドール騎乗を開始
	BeginRideAsRagdoll(Horse, /*bAttachActorToSeat=*/true);
}

// =====================================================================
// 鞍ラグドール騎乗の開始（共通実装）
//   ジョッキーを完全ラグドール化し、PhysicsConstraint（SetupSeatConstraint）で鞍へ拘束して乗せる。
//   bAttachActorToSeat: true で アクタールートを馬メッシュの鞍ソケットへアタッチ（再騎乗時）。
//   ChildActor 由来の初期騎乗では false（既にコンポーネント経由で馬へ追従しているため）。
// =====================================================================
void AJockey::BeginRideAsRagdoll(AHorseCharacter* Horse, bool bAttachActorToSeat)
{
	if (!Horse) { return; }

	OwningHorse = Horse;

	USkeletalMeshComponent* HorseMesh = Horse->GetMesh();

	// 再騎乗時はアクターを鞍ソケットへスナップ・アタッチして馬に追従させる
	if (bAttachActorToSeat && HorseMesh)
	{
		const FAttachmentTransformRules Rules(
			EAttachmentRule::SnapToTarget,   // Location: 鞍ソケットへ
			EAttachmentRule::SnapToTarget,   // Rotation: 鞍ソケットへ
			EAttachmentRule::KeepWorld,      // Scale: 維持
			true);
		AttachToComponent(HorseMesh, Rules, SeatAnchorSocketName);
		SetActorRelativeRotation(AttachRelativeRotation);
		SetActorRelativeLocation(AttachRelativeLocation);

		// 再騎乗（ピックアップ）時: ラグドールで散らばったメッシュを初期着座姿勢へ戻す。
		// 物理を一旦OFFにするとメッシュは参照ポーズへ戻る。続けて初期相対トランスフォームへ
		// 戻すことで、以降の SetSimulatePhysics(true) がこの整った着座姿勢からボディを
		// 再初期化し、初期と同じ位置・ポーズで着座ラグドールを再開できる。
		if (MeshComp && MeshComp->GetPhysicsAsset())
		{
			MeshComp->SetSimulatePhysics(false);
			MeshComp->SetAllBodiesSimulatePhysics(false);
			MeshComp->SetRelativeLocationAndRotation(
				InitialMeshRelativeTransform.GetLocation(),
				InitialMeshRelativeTransform.GetRotation());
		}
	}

	// 騎乗中は Capsule のコリジョンを無効化（衝突はラグドールメッシュが担う）
	if (CapsuleComp)
	{
		CapsuleComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// メッシュを完全ラグドール化する。
	// 位置は PhysicsConstraint で鞍に拘束するため、馬とは衝突させない（Pawn=馬カプセルを Ignore）。
	// 他ジョッキー（PhysicsBody）とは Block を維持し、ヘッドバンキング攻撃の接触を取る。
	if (MeshComp && MeshComp->GetPhysicsAsset())
	{
		MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
		MeshComp->SetCollisionResponseToAllChannels(ECR_Block);
		MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore); // 馬カプセルとは拘束で位置決めするため無視
		MeshComp->SetSimulatePhysics(true);
		MeshComp->SetAllBodiesSimulatePhysics(true);
		MeshComp->WakeAllRigidBodies();
		// 物理有効化後に剛体衝突通知を再適用（OnMeshHit 経路を生かす補助）
		MeshComp->SetNotifyRigidBodyCollision(true);
	}
	else if (GEngine)
	{
		// Physics Asset 未割当だとラグドール化できず、騎乗が破綻する
		GEngine->AddOnScreenDebugMessage(-1, 6.0f, FColor::Red,
			TEXT("[Jockey] ラグドール化不可: メッシュに Physics Asset が未割当"));
	}

	bIsRiding = true;
	bIsKnockedOut = false;
	bIsSwinging = false;
	ImpactAccumulation = 0.0f;

	// 手綱を左手へ物理結合
	if (Horse->GetCurrentReins() && MeshComp)
	{
		Horse->GetCurrentReins()->AttachToJockey(MeshComp);
	}

	// Spline 縄手綱（RopeReinsChildActor）も hand_l にグリップ
	if (MeshComp)
	{
		TInlineComponentArray<UChildActorComponent*> CACs(Horse);
		for (UChildActorComponent* CAC : CACs)
		{
			if (!CAC) { continue; }
			if (ARopeSimulationSpline* Rope = Cast<ARopeSimulationSpline>(CAC->GetChildActor()))
			{
				Rope->AttachHandGrip(MeshComp, FName("hand_l"), -1);
				break;
			}
		}
	}

	// 着座拘束（PhysicsConstraint）を生成して pelvis を鞍へ拘束する
	SetupSeatConstraint();
}

// =====================================================================
// 馬から切り離し（落馬）
// =====================================================================
void AJockey::DetachFromHorse()
{
	// 着座拘束があれば破棄
	BreakSeatConstraint();

	// 手綱の物理結合を切る
	if (OwningHorse && OwningHorse->GetCurrentReins())
	{
		OwningHorse->GetCurrentReins()->DetachFromJockey();
	}

	// Spline 縄手綱のグリップも解除
	if (OwningHorse)
	{
		TInlineComponentArray<UChildActorComponent*> CACs(OwningHorse);
		for (UChildActorComponent* CAC : CACs)
		{
			if (!CAC) { continue; }
			if (ARopeSimulationSpline* Rope = Cast<ARopeSimulationSpline>(CAC->GetChildActor()))
			{
				Rope->DetachHandGrip();
				break;
			}
		}
	}

	// 部分ラグドール解除（フル物理に切り替える前に必須）
	ClearRidingPhysicsBlend();

	const FDetachmentTransformRules Rules(EDetachmentRule::KeepWorld, true);
	DetachFromActor(Rules);

	// Capsule のコリジョンを復帰（落下後の地面接地・通常コリジョン用）
	if (CapsuleComp)
	{
		CapsuleComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	OwningHorse = nullptr;
	bIsRiding = false;
}

// =====================================================================
// 全身ラグドール解除（再騎乗・状態リセット前に呼ぶ）
// =====================================================================
void AJockey::ClearRidingPhysicsBlend()
{
	if (!MeshComp || !MeshComp->GetPhysicsAsset()) { return; }

	// 全ボディを物理 OFF に戻す（ラグドール化前に必須）
	MeshComp->SetAllBodiesBelowSimulatePhysics(RidingSimulateBelowBoneName, false, true);
	MeshComp->SetAllBodiesBelowPhysicsBlendWeight(RidingSimulateBelowBoneName, 0.0f);
}

// =====================================================================
// ChildActorComponent 経由のセットアップ
// =====================================================================
void AJockey::SetupAsChildActor(AHorseCharacter* Horse)
{
	// ChildActor 由来の初期騎乗: 既にコンポーネント経由で馬へ追従しているため再アタッチ不要
	BeginRideAsRagdoll(Horse, /*bAttachActorToSeat=*/false);
}

// =====================================================================
// 落馬／引きずりから復帰して再騎乗
// =====================================================================
void AJockey::WakeUpAndRide(AHorseCharacter* Horse)
{
	if (!Horse) { return; }

	// 引きずり中なら振り回しを終了してから再騎乗
	if (bIsSwinging)
	{
		ExitSwingMode();
	}

	// 状態リセット
	bIsKnockedOut = false;
	ImpactAccumulation = 0.0f;

	// 鞍ラグドール騎乗を再開（アクターを鞍ソケットへ再アタッチ）
	BeginRideAsRagdoll(Horse, /*bAttachActorToSeat=*/true);

	// 手綱を初期形状（首後ろ周り）にリセット
	if (Horse->GetCurrentReins())
	{
		Horse->GetCurrentReins()->ResetCables();
	}
}

// =====================================================================
// 外部衝撃の受信（馬→ジョッキー伝搬用）
// =====================================================================
void AJockey::ReceiveExternalImpact(float ImpactMagnitude)
{
	if (bIsKnockedOut) { return; }
	AddImpact(ImpactMagnitude * HorseImpactTransferRatio);
}

void AJockey::ReceiveExternalImpact(float ImpactMagnitude, const FVector& ImpactWorldDir)
{
	if (bIsKnockedOut) { return; }

	// 落馬時に吹っ飛ぶ方向として、衝撃の押し出し方向（水平成分）を記録する
	FVector Dir = ImpactWorldDir;
	Dir.Z = 0.0f;
	if (Dir.Normalize())
	{
		PendingKnockoffDir = Dir;
	}

	AddImpact(ImpactMagnitude * HorseImpactTransferRatio);
}

// =====================================================================
// 衝撃を加算し、閾値を超えれば落馬
// =====================================================================
void AJockey::AddImpact(float ImpactMagnitude)
{
	if (bIsKnockedOut) { return; }

	// 1回の衝突につき1回だけ計上する。不応期中の追加衝撃（多重コリジョン・複数経路）は無視。
	if (ImpactRefractoryTimer > 0.0f) { return; }
	ImpactRefractoryTimer = ImpactHitRefractory;

	ImpactAccumulation += ImpactMagnitude;

	if (ImpactAccumulation >= ImpactThreshold)
	{
		if (bIsRiding)
		{
			// 騎乗中に閾値超 → 即落馬。
			// EnterRagdollState → DetachFromHorse により手綱(AReins)・縄グリップを解除し、
			// 馬から切り離して完全ラグドールで地面へ落下させる（引きずり段階は経由しない）。
			EnterRagdollState();
		}
		else if (bIsSwinging)
		{
			// 引きずり中にさらに衝撃 → 手綱が外れて完全脱落
			ExitSwingMode();
		}
	}
}

// =====================================================================
// ラグドール状態へ移行
// =====================================================================
void AJockey::EnterRagdollState()
{
	if (bIsKnockedOut) { return; }

	// 1. 馬から切り離す
	if (bIsRiding)
	{
		DetachFromHorse();
	}

	// 2. カプセルの衝突を無効化（メッシュ物理に委譲）
	if (CapsuleComp)
	{
		CapsuleComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// 3. メッシュ全体を物理シミュレーションへ
	if (MeshComp && MeshComp->GetPhysicsAsset())
	{
		// 騎乗中に NoCollision にしていたメッシュ衝突を復帰
		MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
		// Camera チャンネルは引き続き無視（落下中もカメラに干渉させない）
		MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		MeshComp->SetSimulatePhysics(true);
		MeshComp->SetAllBodiesSimulatePhysics(true);
		MeshComp->WakeAllRigidBodies();
		// 物理有効化後に剛体衝突通知を再適用（着弾爆発の OnMeshHit 経路を生かす補助）
		MeshComp->SetNotifyRigidBodyCollision(true);

		// 4. 落馬方向にインパルスを与える。
		//    直近に受けた衝撃方向（PendingKnockoffDir）が有効ならその方向へ吹っ飛ばす。
		//    未指定なら従来どおり後方へフォールバック。常に上方向の弧を加える。
		FVector HorizDir = PendingKnockoffDir;
		HorizDir.Z = 0.0f;
		if (!HorizDir.Normalize())
		{
			HorizDir = -GetActorForwardVector();
			HorizDir.Z = 0.0f;
			HorizDir.Normalize();
		}
		const FVector ImpulseDir = (HorizDir + FVector(0.0f, 0.0f, 1.0f)).GetSafeNormal();
		MeshComp->AddImpulse(ImpulseDir * KnockoffImpulseStrength, NAME_None, true);

		// 使用後はリセット（次回の落馬で再指定されるまで方向未指定に戻す）
		PendingKnockoffDir = FVector::ZeroVector;
	}

	bIsKnockedOut = true;
}

// =====================================================================
// デバッグ: 強制ラグドール
// =====================================================================
void AJockey::ForceRagdoll()
{
	if (bIsKnockedOut) { return; }

	// 蓄積値を閾値まで一気に上げてからラグドール（UI 表示も整合）
	ImpactAccumulation = ImpactThreshold;
	EnterRagdollState();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Magenta,
			TEXT("[Jockey][Debug] ForceRagdoll triggered"));
	}
}

// =====================================================================
// メッシュ衝突 (ラグドール時の衝撃で再衝撃は不要、ログ目的のみ)
// =====================================================================
void AJockey::OnMeshHit(UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	// 射出中のジョッキーが地面 or 障害物に着弾 → 爆発
	if (bLaunchedAsProjectile
		&& AirborneTime >= MinAirborneBeforeLanding
		&& NormalImpulse.Size() >= LandingMinNormalImpulse)
	{
		bLaunchedAsProjectile = false;
		TriggerExplosion(Hit.ImpactPoint);
	}

	// ヘッドバンキング攻撃中に相手ジョッキーへ接触 → 攻撃成立
	// 相手へ大きい衝撃値（落馬を狙う）。自分への少量蓄積は DoHeadbang 側で加算済み。
	if (!bIsKnockedOut
		&& HeadbangActiveTimer > 0.0f
		&& NormalImpulse.Size() >= HeadbangHitMinNormalImpulse)
	{
		if (AJockey* OtherJockey = Cast<AJockey>(OtherActor))
		{
			if (OtherJockey != this && !OtherJockey->IsKnockedOut())
			{
				// 攻撃側→被弾側の方向へ吹っ飛ばす
				const FVector KnockDir = OtherJockey->GetBodyWorldLocation() - GetBodyWorldLocation();
				OtherJockey->ReceiveExternalImpact(HeadbangHitImpact, KnockDir);
				HeadbangActiveTimer = 0.0f; // 1 回のヘッドバンキングにつき 1 ヒット
			}
		}
	}
}

// =====================================================================
// カプセル衝突 ? 騎乗中にジョッキー本人が衝突した場合の蓄積
// =====================================================================
void AJockey::OnCapsuleHit(UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (bIsKnockedOut) { return; }

	const float Magnitude = NormalImpulse.Size();
	if (Magnitude > 0.0f)
	{
		AddImpact(Magnitude);

		// 手綱へ衝撃を伝搬（閾値超で手綱が外れる）
		if (OwningHorse && OwningHorse->GetCurrentReins())
		{
			OwningHorse->GetCurrentReins()->AddImpactToReins(Magnitude);
		}
	}
}

// =====================================================================
// デバッグUI
// =====================================================================
void AJockey::DrawDebugUI()
{
	if (!GEngine) { return; }

	const float Ratio = (ImpactThreshold > 0.0f)
		? FMath::Clamp(ImpactAccumulation / ImpactThreshold, 0.0f, 1.0f)
		: 0.0f;

	FColor TextColor = FColor::Green;
	FString StateLabel = TEXT("Normal");

	if (bIsKnockedOut)
	{
		TextColor = FColor::Red;
		StateLabel = TEXT("KnockedOut (Ragdoll)");
	}
	else if (Ratio >= 0.7f)
	{
		TextColor = FColor::Yellow;
		StateLabel = TEXT("Warning");
	}

	const FString Msg = FString::Printf(
		TEXT("[Jockey] State: %s | Impact: %.0f / %.0f (%.0f%%)"),
		*StateLabel,
		ImpactAccumulation,
		ImpactThreshold,
		Ratio * 100.0f);

	// キーを Actor の UniqueID にすることで複数ジョッキーが共存可能
	GEngine->AddOnScreenDebugMessage(
		static_cast<int32>(GetUniqueID()),
		0.0f,
		TextColor,
		Msg);
}

// =====================================================================
// 射出（プレイヤー能動: 急ブレーキ等）
// =====================================================================
void AJockey::LaunchAsProjectile(const FVector& InitialVelocity)
{
	// 騎乗中なら馬から切り離し、ラグドール化を経由する
	if (bIsRiding)
	{
		DetachFromHorse();
	}

	// ラグドール状態へ強制移行（蓄積マックスで EnterRagdollState）
	if (!bIsKnockedOut)
	{
		ImpactAccumulation = ImpactThreshold;
		EnterRagdollState();
	}

	// EnterRagdollState 直後のインパルスを上書きする形で射出速度を付与
	if (MeshComp && MeshComp->GetPhysicsAsset())
	{
		// 既存の速度をリセットしてから射出速度を設定
		MeshComp->SetAllPhysicsLinearVelocity(InitialVelocity);
	}

	bLaunchedAsProjectile = true;
	AirborneTime = 0.0f;

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Orange,
			FString::Printf(TEXT("[Jockey] LaunchAsProjectile v=%s"), *InitialVelocity.ToString()));
	}
}

// =====================================================================
// 着弾爆発（範囲内全ジョッキーの衝撃蓄積をマックス化）
// =====================================================================
void AJockey::TriggerExplosion(const FVector& Location)
{
	UWorld* World = GetWorld();
	if (!World) { return; }

	const float R2 = ExplosionRadius * ExplosionRadius;

	// 範囲内のすべての AJockey を走査
	for (TActorIterator<AJockey> It(World); It; ++It)
	{
		AJockey* Other = *It;
		if (!Other) { continue; }

		const float DSq = FVector::DistSquared(Location, Other->GetActorLocation());
		if (DSq > R2) { continue; }

		// 既にラグドール化している（自分含む）なら蓄積処理はスキップし、メッシュにインパルスのみ与える
		if (!Other->bIsKnockedOut)
		{
			// 落馬時の吹っ飛び方向を爆発中心からの外向きに設定してからラグドール化
			Other->PendingKnockoffDir = (Other->GetActorLocation() - Location);
			Other->ImpactAccumulation = Other->ImpactThreshold;
			Other->EnterRagdollState();
		}

		// 爆発インパルス（中心から外向き＋上）
		if (Other->MeshComp && Other->MeshComp->GetPhysicsAsset())
		{
			FVector Dir = Other->GetActorLocation() - Location;
			if (Dir.IsNearlyZero())
			{
				Dir = FVector(0.0f, 0.0f, 1.0f);
			}
			Dir = Dir.GetSafeNormal() + FVector(0.0f, 0.0f, 0.5f);
			Other->MeshComp->AddImpulse(Dir.GetSafeNormal() * ExplosionImpulseStrength, NAME_None, true);
		}
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red,
			FString::Printf(TEXT("[Jockey] Explosion @ %s (r=%.0f)"), *Location.ToString(), ExplosionRadius));
	}
}

// =====================================================================
// 近接ベースの攻撃ヒット判定（物理 OnComponentHit に依存しない確定判定）
// =====================================================================
void AJockey::UpdateHeadbangProximityHit()
{
	// 自分が落馬中は攻撃できない
	if (bIsKnockedOut) { return; }

	UWorld* World = GetWorld();
	if (!World) { return; }

	const FVector MyBody = GetBodyWorldLocation();
	const float R2 = HeadbangHitRadius * HeadbangHitRadius;

	// 範囲内で最も近い未落馬の相手ジョッキーへ衝撃を与える
	AJockey* HitTarget = nullptr;
	float NearestSq = R2;
	for (TActorIterator<AJockey> It(World); It; ++It)
	{
		AJockey* Other = *It;
		if (!Other || Other == this || Other->bIsKnockedOut) { continue; }

		const float DSq = FVector::DistSquared(MyBody, Other->GetBodyWorldLocation());
		if (DSq <= NearestSq)
		{
			NearestSq = DSq;
			HitTarget = Other;
		}
	}

	if (HitTarget)
	{
		// 攻撃側→被弾側の方向へ吹っ飛ばす
		const FVector KnockDir = HitTarget->GetBodyWorldLocation() - MyBody;
		HitTarget->ReceiveExternalImpact(HeadbangHitImpact, KnockDir);
		// 1 回のヘッドバング有効ウィンドウにつき 1 ヒット（連続多重ヒットを防止）
		HeadbangActiveTimer = 0.0f;
	}
}

// =====================================================================
// 射出ジョッキーの着地判定（メッシュ速度低下で爆発）
// =====================================================================
void AJockey::UpdateProjectileLanding()
{
	// 空中時間が一定を超えるまでは着地判定しない（射出直後の誤爆防止）
	if (AirborneTime < MinAirborneBeforeLanding) { return; }
	if (!MeshComp || !MeshComp->GetPhysicsAsset()) { return; }

	// メッシュ速度が着地しきい値以下に落ちたら着地とみなす
	const float Speed = MeshComp->GetPhysicsLinearVelocity().Size();
	if (Speed <= LandingSpeedThreshold)
	{
		bLaunchedAsProjectile = false;
		TriggerExplosion(GetBodyWorldLocation());
	}
}

// =====================================================================
// 振り回しモード
// =====================================================================
void AJockey::EnterSwingMode(USceneComponent* InAnchorComp, FName InAnchorSocket)
{
	if (bIsSwinging) { return; }

	// 騎乗中ならソケットから detach（手綱グリップは維持するため、
	// DetachFromHorse は使わず最低限の処理に留める）
	AHorseCharacter* HorseRef = OwningHorse;
	if (bIsRiding)
	{
		// 着座拘束を破断・破棄（引きずりへ移行）
		BreakSeatConstraint();

		// 部分ラグドール解除（フル物理に切り替える前に必須）
		ClearRidingPhysicsBlend();

		const FDetachmentTransformRules Rules(EDetachmentRule::KeepWorld, true);
		DetachFromActor(Rules);

		if (CapsuleComp)
		{
			CapsuleComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		bIsRiding = false;
	}

	// 既存の Reins (Skeletal) は切る (Rope の HandGrip は維持)
	if (HorseRef && HorseRef->GetCurrentReins())
	{
		HorseRef->GetCurrentReins()->DetachFromJockey();
	}

	// メッシュ全体を物理シミュレーションへ（KnockoffImpulse は与えない＝騎乗位置からそのまま）
	if (MeshComp && MeshComp->GetPhysicsAsset())
	{
		MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
		MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		MeshComp->SetSimulatePhysics(true);
		MeshComp->SetAllBodiesSimulatePhysics(true);
		MeshComp->WakeAllRigidBodies();
	}

	SwingAnchorComp = InAnchorComp;
	SwingAnchorSocket = InAnchorSocket;
	bIsSwinging = true;

	// アンカー世界速度算出用の前フレームキャッシュを初期化
	PrevSwingAnchorWorld = ResolveSwingAnchorWorld();
	bSwingAnchorPrevValid = true;

	// Stretch 成長レート制限用キャッシュを初期化
	PrevSwingStretch = 0.0f;
	bSwingPrevStretchValid = false;

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
			TEXT("[Jockey] EnterSwingMode"));
	}
}

void AJockey::ExitSwingMode()
{
	if (!bIsSwinging) { return; }

	bIsSwinging = false;
	SwingAnchorComp = nullptr;
	SwingAnchorSocket = NAME_None;
	bSwingAnchorPrevValid = false;
	PrevSwingAnchorWorld = FVector::ZeroVector;
	bSwingPrevStretchValid = false;
	PrevSwingStretch = 0.0f;

	// 通常の KnockedOut 状態として扱う（ピックアップで復帰可能）
	bIsKnockedOut = true;

	// 縄のハンドグリップを解除（落ちる）
	if (OwningHorse)
	{
		TInlineComponentArray<UChildActorComponent*> CACs(OwningHorse);
		for (UChildActorComponent* CAC : CACs)
		{
			if (!CAC) { continue; }
			if (ARopeSimulationSpline* Rope = Cast<ARopeSimulationSpline>(CAC->GetChildActor()))
			{
				Rope->DetachHandGrip();
				break;
			}
		}
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
			TEXT("[Jockey] ExitSwingMode"));
	}
}

// =====================================================================
// アンカー世界位置の解決 (ソケット優先)
// =====================================================================
FVector AJockey::ResolveSwingAnchorWorld() const
{
	if (!SwingAnchorComp) { return FVector::ZeroVector; }
	if (!SwingAnchorSocket.IsNone() && SwingAnchorComp->DoesSocketExist(SwingAnchorSocket))
	{
		return SwingAnchorComp->GetSocketLocation(SwingAnchorSocket);
	}
	return SwingAnchorComp->GetComponentLocation();
}

// =====================================================================
// 振り回し拘束 ? 力ベース PD 制御 (バネ-ダンパー + 力上限クランプ)
//
// 設計意図:
//   旧 SetPhysicsLinearVelocity 方式は速度を毎Tick上書きするため物理エンジンが
//   計算した自然な加速度が消去され「もっさり」とした挙動になっていた。
//   本実装は AddForce による力ベース PD 制御に戻し、PhysicsAsset 制約と協調する
//   軽快なフレイル挙動を取り戻す。無限加速は以下3つのクランプで物理的に断つ:
//
//   1. Stretch クランプ (SwingMaxExcess): F = k*Stretch の発散源を頭打ちにする
//   2. 力の絶対上限 (SwingMaxForce):       合成力の大きさを最終クランプ
//   3. アンカー基準の相対速度で減衰:        馬の運動成分で誤減衰せず安定
//
//   また bUseAnchorVelocityTracking 有効時はアンカー速度を相対速度算出に使用し、
//   馬が動いている定常状態でも Damping が暴れない (= 静止系基準では馬の速度が
//   そのまま v_radial に乗ってしまい減衰挙動が破綻する問題を回避)。
// =====================================================================
void AJockey::ApplySwingSpring(float DeltaTime)
{
	if (!MeshComp || !SwingAnchorComp) { return; }
	if (!MeshComp->GetPhysicsAsset()) { return; }
	if (DeltaTime <= KINDA_SMALL_NUMBER) { return; }

	// アンカー世界位置を解決
	const FVector AnchorWorld = ResolveSwingAnchorWorld();

	// アンカー世界速度 (馬の動き)
	FVector AnchorVel = FVector::ZeroVector;
	if (bUseAnchorVelocityTracking && bSwingAnchorPrevValid)
	{
		const FVector AnchorMove = AnchorWorld - PrevSwingAnchorWorld;
		if (AnchorMove.Size() <= SwingMaxAnchorTeleportDist)
		{
			AnchorVel = AnchorMove / DeltaTime;
		}
	}

	// ハンド骨の現在位置・速度
	const FVector HandWorld = MeshComp->GetBoneLocation(SwingHandBoneName, EBoneSpaces::WorldSpace);
	const FVector HandVel   = MeshComp->GetPhysicsLinearVelocity(SwingHandBoneName);

	const FVector Delta = AnchorWorld - HandWorld; // Hand → Anchor 方向 (引き戻し方向)
	const float Dist = Delta.Size();

	// 自然長を超えたときのみバネ力を加える (内側では自由スイング)
	if (Dist > SwingRestLength && Dist > KINDA_SMALL_NUMBER)
	{
		const FVector InwardDir = Delta / Dist; // Hand→Anchor の引き戻し方向

		// Stretch クランプ ? バネ力発散の頭打ち
		const float StretchRaw = Dist - SwingRestLength;
		float Stretch = FMath::Min(StretchRaw, SwingMaxExcess);

		// 【Stretch 成長レート制限】旋回などでアンカーが急動した際の
		// Stretch 跳ね上がりを抑え、バネ力の突発発生を緩和する。
		// 直進・通常スイングでは制限に引っかからないため挙動に影響しない。
		if (bSwingPrevStretchValid)
		{
			const float MaxGrowth = SwingMaxStretchGrowthRate * DeltaTime;
			const float Growth = Stretch - PrevSwingStretch;
			if (Growth > MaxGrowth)
			{
				Stretch = PrevSwingStretch + MaxGrowth;
			}
		}
		PrevSwingStretch = Stretch;
		bSwingPrevStretchValid = true;

		// アンカー基準の相対速度の半径方向成分 (正=内向き、負=外向き)
		// 馬が走っていても AnchorVel を引くことで Damping が誤動作しない
		const FVector RelativeVel = HandVel - AnchorVel;
		const float VelRadial = FVector::DotProduct(RelativeVel, InwardDir);

		// バネ-ダンパー力: F = (k * Stretch - c * v_radial) を Hand→Anchor 方向に
		const float SpringMag  = Stretch  * SwingStiffness;
		const float DampingMag = VelRadial * SwingDamping;
		FVector Force = InwardDir * (SpringMag - DampingMag);

		// 力の絶対上限クランプ ? 無限加速を物理的に断つ最後の砦
		const float ForceMag = Force.Size();
		if (ForceMag > SwingMaxForce && ForceMag > KINDA_SMALL_NUMBER)
		{
			Force *= (SwingMaxForce / ForceMag);
		}

		// AddForce(bAccelChange=false): F は実物理力として加算 (質量考慮)
		MeshComp->AddForce(Force, SwingHandBoneName, false);
	}
	else
	{
		// 拘束圏外 (自由スイング中) は Stretch=0 として記録。
		// 次回拘束突入時に古い値が残るとレート制限が誤動作するためリセットする。
		PrevSwingStretch = 0.0f;
		bSwingPrevStretchValid = true;
	}

	// 前フレームキャッシュ更新 (Dist が自然長以内でも継続して計測)
	PrevSwingAnchorWorld = AnchorWorld;
	bSwingAnchorPrevValid = true;
}

// =====================================================================
// 着座拘束の生成（PhysicsConstraint で pelvis を馬カプセルへ拘束）
//   Linear Lock で鞍位置に固定し、Angular Limited＋Drive で上体をフニャっと許容。
//   Break Threshold 超過の衝撃で自動破断 → 引きずりへ。
// =====================================================================
void AJockey::SetupSeatConstraint()
{
	if (!OwningHorse || !MeshComp) { return; }

	// Physics Asset が無いと pelvis ボディが存在せず拘束できない（最頻の沈黙失敗）
	if (!MeshComp->GetPhysicsAsset())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 6.0f, FColor::Red,
				TEXT("[Jockey] 着座拘束を作成不可: メッシュに Physics Asset が未割当"));
		}
		return;
	}

	UCapsuleComponent* HorseCapsule = OwningHorse->GetCapsuleComponent();
	USkeletalMeshComponent* HorseMesh = OwningHorse->GetMesh();
	if (!HorseCapsule || !HorseMesh) { return; }

	// 既存があれば作り直す
	BreakSeatConstraint();

	SeatConstraint = NewObject<UPhysicsConstraintComponent>(this);
	if (!SeatConstraint) { return; }

	// 鞍ソケットが付いている馬ボーンを取得（アニメーションで動くボーンに追従させる）
	const FName HorseSeatBone = HorseMesh->GetSocketBoneName(SeatAnchorSocketName);

	// 登録 → 馬メッシュの鞍ソケットへアタッチ（アニメーションで動く鞍に追従）
	SeatConstraint->RegisterComponent();
	if (HorseMesh->DoesSocketExist(SeatAnchorSocketName))
	{
		SeatConstraint->AttachToComponent(HorseMesh, FAttachmentTransformRules::KeepRelativeTransform, SeatAnchorSocketName);
	}
	else
	{
		SeatConstraint->AttachToComponent(HorseCapsule, FAttachmentTransformRules::KeepRelativeTransform);
	}

	// 拘束フレームを鞍ソケットの位置・姿勢に合わせる。
	// Linear Lock により pelvis が鞍位置まで引き上げられ「馬の上に座る」。
	// 引き上げ時のスナップ力で誤破断しないよう、既定では破断を無効化している（bSeatBreakable）。
	const FTransform SeatXf = HorseMesh->DoesSocketExist(SeatAnchorSocketName)
		? HorseMesh->GetSocketTransform(SeatAnchorSocketName)
		: HorseMesh->GetComponentTransform();
	SeatConstraint->SetWorldLocationAndRotation(SeatXf.GetLocation(), SeatXf.GetRotation());

	// 並進は完全ロック（鞍から離れない＝すり抜け・脱落防止）
	SeatConstraint->SetLinearXLimit(LCM_Locked, 0.0f);
	SeatConstraint->SetLinearYLimit(LCM_Locked, 0.0f);
	SeatConstraint->SetLinearZLimit(LCM_Locked, 0.0f);

	// 回転は制限付きで許容（上体のフニャっと感）
	SeatConstraint->SetAngularSwing1Limit(ACM_Limited, SeatAngularSwingLimit);
	SeatConstraint->SetAngularSwing2Limit(ACM_Limited, SeatAngularSwingLimit);
	SeatConstraint->SetAngularTwistLimit(ACM_Limited, SeatAngularTwistLimit);

	// 直立へ戻す角度ドライブ（SLERP）
	SeatConstraint->SetAngularDriveMode(EAngularDriveMode::SLERP);
	SeatConstraint->SetOrientationDriveSLERP(true);
	SeatConstraint->SetAngularDriveParams(SeatAngularDriveStiffness, SeatAngularDriveDamping, 0.0f);

	// 物理破断（任意）。既定 OFF＝落馬は衝撃蓄積（ImpactThreshold）で判定し安定させる。
	if (bSeatBreakable)
	{
		SeatConstraint->SetLinearBreakable(true, SeatLinearBreakForce);
		SeatConstraint->SetAngularBreakable(true, SeatAngularBreakTorque);
	}
	else
	{
		SeatConstraint->SetLinearBreakable(false, 0.0f);
		SeatConstraint->SetAngularBreakable(false, 0.0f);
	}

	// 馬メッシュの鞍ボーン（アニメーションで動く）↔ ジョッキー pelvis（シミュ）を拘束。
	// これにより鞍ソケットの上下・揺れにジョッキーが同期する。鞍ボーンが取得できない場合はカプセルにフォールバック。
	if (!HorseSeatBone.IsNone())
	{
		SeatConstraint->SetConstrainedComponents(HorseMesh, HorseSeatBone, MeshComp, SeatBoneName);
	}
	else
	{
		SeatConstraint->SetConstrainedComponents(HorseCapsule, NAME_None, MeshComp, SeatBoneName);
	}

	// 破断通知を購読
	SeatConstraint->OnConstraintBroken.AddDynamic(this, &AJockey::HandleSeatConstraintBroken);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Green,
			TEXT("[Jockey] 着座拘束を作成しました"));
	}
}

// =====================================================================
// 着座拘束の破断・破棄
// =====================================================================
void AJockey::BreakSeatConstraint()
{
	if (SeatConstraint)
	{
		SeatConstraint->OnConstraintBroken.RemoveDynamic(this, &AJockey::HandleSeatConstraintBroken);
		SeatConstraint->BreakConstraint();
		SeatConstraint->DestroyComponent();
		SeatConstraint = nullptr;
	}
}

// =====================================================================
// 騎乗ラグドール → 引きずり状態へ遷移（拘束破断・被弾時）
// =====================================================================
void AJockey::TransitionToDragging()
{
	if (bIsSwinging) { return; }

	// 拘束を破棄してから引きずり（振り回し）へ。手綱グリップは EnterSwingMode 内で維持される。
	BreakSeatConstraint();

	if (OwningHorse && OwningHorse->GetMesh())
	{
		EnterSwingMode(OwningHorse->GetMesh(), SeatAnchorSocketName);
	}
	else
	{
		EnterRagdollState();
	}
}

// =====================================================================
// 着座拘束が物理破断したときのコールバック（強い衝撃で投げ出された）
// =====================================================================
void AJockey::HandleSeatConstraintBroken(int32 ConstraintIndex)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Orange,
			TEXT("[Jockey] 着座拘束が破断（投げ出し）"));
	}
	TransitionToDragging();
}

// =====================================================================
// ヘッドバンキング攻撃: 上体ボーンへ横方向インパルスを与えて振る
// =====================================================================
void AJockey::DoHeadbang(float Direction)
{
	if (!bIsRiding || bIsKnockedOut) { return; }
	if (!MeshComp || !MeshComp->GetPhysicsAsset()) { return; }

	// 馬の右ベクトルを基準に左右へ振る（水平成分のみ）
	FVector Right = OwningHorse ? OwningHorse->GetActorRightVector() : GetActorRightVector();
	Right.Z = 0.0f;
	if (!Right.Normalize()) { return; }

	// 横方向 + わずかに上方向のインパルス（質量考慮: bVelChange=false）
	const float Sign = (Direction >= 0.0f) ? 1.0f : -1.0f;
	const FVector Impulse = Right * (Sign * HeadbangImpulse) + FVector(0.0f, 0.0f, HeadbangImpulse * 0.15f);
	MeshComp->AddImpulse(Impulse, HeadbangBoneName, false);

	// 自分のジョッキーへも少量の衝撃を蓄積（振りすぎると自滅）
	AddImpact(HeadbangSelfImpact);

	// 命中を攻撃として扱う有効時間を起動
	HeadbangActiveTimer = HeadbangActiveWindow;
}

// =====================================================================
// 攻撃判定の有効/無効（馬が強く傾いている間の接触を攻撃として扱う）
// =====================================================================
void AJockey::SetHeadbangActive(bool bActive)
{
	if (bActive)
	{
		// 有効時間を維持し続ける（傾きが続く限り接触を攻撃扱いにする）
		HeadbangActiveTimer = HeadbangActiveWindow;
	}
	// 無効時は Tick の自然減衰に任せる
}

// =====================================================================
// ジョッキー実体（pelvis ボーン）のワールド位置
// =====================================================================
FVector AJockey::GetBodyWorldLocation() const
{
	if (MeshComp)
	{
		if (!SeatBoneName.IsNone() && MeshComp->DoesSocketExist(SeatBoneName))
		{
			return MeshComp->GetBoneLocation(SeatBoneName, EBoneSpaces::WorldSpace);
		}
		return MeshComp->GetComponentLocation();
	}
	return GetActorLocation();
}