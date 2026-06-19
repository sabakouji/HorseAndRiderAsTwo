#include "HorseCharacter.h"
#include "Jockey.h"
#include "Reins.h"
#include "RopeSimulationSpline.h"
#include "ChainSimulationSkeletal.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/ChildActorComponent.h"
#include "TrackActor.h"
#include "RaceManager.h"
#include "GhostRecorder.h"
#include "AnimalStatsDataAsset.h"
#include "Components/SplineComponent.h"
#include "Kismet/GameplayStatics.h"

// =====================================================================
// コンストラクタ
// =====================================================================
AHorseCharacter::AHorseCharacter()
{
	PrimaryActorTick.bCanEverTick = true;  // Tick 内で旋回・移動を処理するため有効化

	// --- SpringArm（カメラブーム） ---
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = CameraArmLength;
	// コントローラー回転ではなく馬アクターの Yaw に追従させる
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritYaw   = true;   // 馬の Yaw を継承 → A/D 旋回でカメラも回る
	SpringArm->bInheritPitch = false;  // 仰角は固定（下記 RelativeRotation で設定）
	SpringArm->bInheritRoll  = false;
	SpringArm->SetRelativeRotation(FRotator(-20.0f, 0.0f, 0.0f)); // カメラ俯角

	// --- FollowCamera ---
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// --- DashCameraPoint (BP で位置を編集してダッシュ時のカメラ目標を指定) ---
	DashCameraPoint = CreateDefaultSubobject<USceneComponent>(TEXT("DashCameraPoint"));
	DashCameraPoint->SetupAttachment(RootComponent);
	// 既定値: ややローアングルかつ近寄ったポジション (BP 側で調整想定)
	DashCameraPoint->SetRelativeLocation(FVector(-150.0f, 0.0f, 120.0f));
	DashCameraPoint->SetRelativeRotation(FRotator(-10.0f, 0.0f, 0.0f));

	// --- 回転設定 ---
	// コントローラー回転には一切従わない。
	// 旋回は Tick() 内の AddActorWorldRotation で毎フレーム処理する。
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw   = false;
	bUseControllerRotationRoll  = false;

	// --- CharacterMovement の初期値（後で UPROPERTY の値を BeginPlay で適用） ---
	// bOrientRotationToMovement を false にすることで、
	// 移動方向への自動回転を無効化し、S キー後退時に振り返らない。
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->RotationRate              = FRotator(0.0f, 100.0f, 0.0f);
	GetCharacterMovement()->MaxWalkSpeed              = 800.0f;

	// --- ChildActorComponent: Jockey (馬メッシュの JockeySocket にアタッチ) ---
	JockeyChildActor = CreateDefaultSubobject<UChildActorComponent>(TEXT("JockeyChildActor"));
	JockeyChildActor->SetupAttachment(GetMesh(), TEXT("JockeySocket"));

	// --- ChildActorComponent: Reins (馬メッシュのルートにアタッチ) ---
	// ソケットを指定しないことで、AReins 内の座標が馬メッシュ標準空間
	// (X=前, Y=右, Z=上) で扱えるようになり、JointRelativeLocation の
	// 調整が直感的になる。
	ReinsChildActor = CreateDefaultSubobject<UChildActorComponent>(TEXT("ReinsChildActor"));
	ReinsChildActor->SetupAttachment(GetMesh());

	// --- ChildActorComponent: Rope (Spline 縄手綱) ---
	// 馬メッシュ直下にアタッチ。端点は BridleSocket_L / BridleSocket_R をワールド参照する。
	RopeReinsChildActor = CreateDefaultSubobject<UChildActorComponent>(TEXT("RopeReinsChildActor"));
	RopeReinsChildActor->SetupAttachment(GetMesh());
}

// =====================================================================
// BeginPlay
// =====================================================================
void AHorseCharacter::BeginPlay()
{
	Super::BeginPlay();

	// レベル内の ATrackActor を検索してキャッシュ
	if (!TrackActorRef)
	{
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATrackActor::StaticClass(), FoundActors);
		if (FoundActors.Num() > 0)
		{
			TrackActorRef = Cast<ATrackActor>(FoundActors[0]);
		}
	}

	// レベル内の ARaceManager をキャッシュ（ResetToTrack 通知用）
	if (!CachedRaceManager)
	{
		TArray<AActor*> FoundManagers;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ARaceManager::StaticClass(), FoundManagers);
		if (FoundManagers.Num() > 0)
		{
			CachedRaceManager = Cast<ARaceManager>(FoundManagers[0]);
		}
	}

	// レース状態変化を購読し、Countdown 中は入力ロックを有効化する
	if (CachedRaceManager)
	{
		CachedRaceManager->OnRaceStateChanged.AddDynamic(this, &AHorseCharacter::HandleRaceStateChanged);
		// 現在の状態でロック初期値を同期（Pregame/Countdown ならロック ON）
		HandleRaceStateChanged(CachedRaceManager->RaceState);
	}
	else
	{
		// RaceManager 未配置レベルでは入力ロックを掛けない（自由走行モード）
		bInputLocked = false;
	}

	// AnimalStats（割当時）を各フィールドへ反映してから MovementComponent へ適用
	ApplyAnimalStats();

	// SpringArm の既定トランスフォームをキャッシュ（BP で編集された値を尊重）
	if (SpringArm)
	{
		SpringArm->TargetArmLength = CameraArmLength;
		DefaultSpringArmRelLoc = SpringArm->GetRelativeLocation();
		DefaultSpringArmRelRot = SpringArm->GetRelativeRotation();
		DefaultSpringArmLength = SpringArm->TargetArmLength;
	}

	// --- EnhancedInput: Mapping Context を登録 ---
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}

	// --- Physics: TABS 風グニャグニャ挙動の設定 ---
	// Physics Asset が割り当て済みの場合のみ有効
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		// ヘッドバンキング時の馬ロールのベースとなる既定相対回転をキャッシュ
		DefaultMeshRelRot = MeshComp->GetRelativeRotation();

		if (MeshComp->GetPhysicsAsset())
		{
			// SimulateBelowBoneName で指定したボーン以下を物理演算に切り替える
			MeshComp->SetAllBodiesBelowSimulatePhysics(SimulateBelowBoneName, true, true);

			// アニメーションと物理のブレンド比率を設定
			// 0.0 = アニメーション完全優先 / 1.0 = 物理完全優先
			MeshComp->SetAllBodiesBelowPhysicsBlendWeight(SimulateBelowBoneName, PhysicsBlendWeight);

			// ブレンドを有効化（bBlendPhysics フラグを立てる）
			MeshComp->SetEnablePhysicsBlending(true);
		}

		// 馬メッシュの衝突 → ジョッキーへ伝搬
		MeshComp->SetNotifyRigidBodyCollision(true);
		MeshComp->OnComponentHit.AddDynamic(this, &AHorseCharacter::OnHorseHit);
	}

	// カプセル衝突も伝搬対象にする
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetNotifyRigidBodyCollision(true);
		Capsule->OnComponentHit.AddDynamic(this, &AHorseCharacter::OnHorseHit);
	}

	// --- Jockey / Reins 初期化（ChildActorComponent から取得） ---
	// Reins を先に初期化し、その後 Jockey の SetupAsChildActor から
	// Reins->AttachToJockey が呼ばれる順序にする。
	InitializeReinsFromChildActor();
	InitializeRopeReinsFromChildActor();
	InitializeJockeyFromChildActor();

	// レベルに直接配置された AI 馬（スポーナを介さない場合）はここで個体差を確定する。
	// スポーナ経由の馬は SpawnGrid 内で bIsAI=true 設定後に InitializeAIProfile が呼ばれる。
	if (bIsAI)
	{
		InitializeAIProfile();
	}
}

// =====================================================================
// Jockey 初期化（ChildActorComponent 経由）
// =====================================================================
void AHorseCharacter::InitializeJockeyFromChildActor()
{
	if (!JockeyChildActor) { return; }

	AJockey* ChildJockey = Cast<AJockey>(JockeyChildActor->GetChildActor());
	if (!ChildJockey)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow,
				TEXT("[Horse] JockeyChildActor: Child Actor Class が未設定 (BP_Jockey を割当)"));
		}
		return;
	}

	CurrentJockey = ChildJockey;

	// 騎乗状態セットアップ（既にソケットへアタッチ済みなので Attach はスキップ）
	ChildJockey->SetupAsChildActor(this);

	// 馬⇔ジョッキーの相互コリジョン無視（IgnoreActorWhenMoving）
	if (UCapsuleComponent* HorseCapsule = GetCapsuleComponent())
	{
		HorseCapsule->IgnoreActorWhenMoving(ChildJockey, true);
	}
	if (USkeletalMeshComponent* HorseMesh = GetMesh())
	{
		HorseMesh->IgnoreActorWhenMoving(ChildJockey, true);
	}
	TInlineComponentArray<UPrimitiveComponent*> JockeyPrims;
	ChildJockey->GetComponents<UPrimitiveComponent>(JockeyPrims);
	for (UPrimitiveComponent* Prim : JockeyPrims)
	{
		if (Prim) { Prim->IgnoreActorWhenMoving(this, true); }
	}
}

// =====================================================================
// Reins 初期化（ChildActorComponent 経由）
// =====================================================================
void AHorseCharacter::InitializeReinsFromChildActor()
{
	if (!ReinsChildActor) { return; }

	AReins* ChildReins = Cast<AReins>(ReinsChildActor->GetChildActor());
	if (!ChildReins)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow,
				TEXT("[Horse] ReinsChildActor: Child Actor Class が未設定 (BP_Reins を割当)"));
		}
		return;
	}

	CurrentReins = ChildReins;

	// 両ケーブルを馬ブライドルへ接続（共通処理）
	LinkReinsToHorse();
}

// =====================================================================
// Rope (Spline) 手綱
// 端点は BP テンプレート側で Start/End Socket Name を設定する。
// Rope 側で StartActor/EndActor が未指定なら自動で親アクター (この馬) を使う。
// したがってここでは確認ログのみ。
// =====================================================================
void AHorseCharacter::InitializeRopeReinsFromChildActor()
{
	if (!RopeReinsChildActor) { return; }

	AActor* Child = RopeReinsChildActor->GetChildActor();

	// (A) Spline 縄手綱
	if (ARopeSimulationSpline* Rope = Cast<ARopeSimulationSpline>(Child))
	{
		// ソケット名は BP テンプレート側を尊重し、Actor 参照だけを馬 (this) に差し込む。
		// (Rope の GetParentActor() フォールバックが ChildActor 構築タイミングで
		//  空を返すケースがあるため、ここで確実に差し込む)
		Rope->SetEndpointActors(this, this);
		Rope->RefreshLayout();
		return;
	}

	// (B) Skeletal 鎖（フレイル鎖）
	if (AChainSimulationSkeletal* Chain = Cast<AChainSimulationSkeletal>(Child))
	{
		// アクター参照だけ this (HorseCharacter) に差し込む（ソケットの自動解決はC++の全メッシュ走査で行われます）。
		Chain->SetEndpointActors(this, this);
		Chain->RefreshLayout();
		return;
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow,
			TEXT("[Horse] RopeReinsChildActor: Child Actor Class が未設定 / 未対応クラス"));
	}
}

// =====================================================================
// Reins の両ケーブルを馬メッシュへ接続（OnConstruction & BeginPlay 共通）
// =====================================================================
void AHorseCharacter::LinkReinsToHorse()
{
	if (!ReinsChildActor) { return; }

	AReins* ChildReins = Cast<AReins>(ReinsChildActor->GetChildActor());
	if (!ChildReins) { return; }

	USkeletalMeshComponent* HorseMesh = GetMesh();
	if (!HorseMesh) { return; }

	ChildReins->Initialize(HorseMesh);
}

// =====================================================================
// OnConstruction — エディタプレビュー時の手綱接続
// =====================================================================
void AHorseCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// エディタプレビュー時は物理を起動せず、始端・終端を BridleSocket_L/R に
	// 合わせて見た目だけ整える（物理 Constraint 接続は BeginPlay 側に集約）。
	if (ReinsChildActor)
	{
		if (AReins* ChildReins = Cast<AReins>(ReinsChildActor->GetChildActor()))
		{
			if (USkeletalMeshComponent* HorseMesh = GetMesh())
			{
				ChildReins->PreviewLayoutAtBridleSockets(HorseMesh);
			}
		}
	}

	// Spline 縄手綱の端点もエディタプレビュー時に反映
	InitializeRopeReinsFromChildActor();
}

// =====================================================================
// 馬の衝突 → ジョッキーへ伝搬
// =====================================================================
void AHorseCharacter::OnHorseHit(UPrimitiveComponent* HitComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	if (!CurrentJockey || !CurrentJockey->IsRiding()) { return; }

	const float Magnitude = NormalImpulse.Size();
	if (Magnitude > 0.0f)
	{
		CurrentJockey->ReceiveExternalImpact(Magnitude * ImpactToJockeyRatio);
	}
}

// =====================================================================
// SetupPlayerInputComponent
// =====================================================================
void AHorseCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			// 押している間: 入力値をメンバ変数に保持
			EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered,
				this, &AHorseCharacter::Move);

			// キーを離したとき: 入力値を 0 にリセット
			EnhancedInput->BindAction(MoveAction, ETriggerEvent::Completed,
				this, &AHorseCharacter::MoveCompleted);
		}

		if (PickupAction)
		{
			// E キー押下時: ピックアップ試行 (Started = 押した瞬間に 1 回だけ)
			EnhancedInput->BindAction(PickupAction, ETriggerEvent::Started,
				this, &AHorseCharacter::TryPickupJockey);
		}

		if (DebugRagdollAction)
		{
			// デバッグ: 強制ラグドール (Started = 1 回だけ)
			EnhancedInput->BindAction(DebugRagdollAction, ETriggerEvent::Started,
				this, &AHorseCharacter::DebugForceJockeyRagdoll);
		}

		if (DashAction)
		{
			EnhancedInput->BindAction(DashAction, ETriggerEvent::Triggered,
				this, &AHorseCharacter::DashStarted);
			EnhancedInput->BindAction(DashAction, ETriggerEvent::Completed,
				this, &AHorseCharacter::DashCompleted);
		}

		if (BrakeAction)
		{
			// ブレーキ: 押した瞬間に射出判定＋ブレーキ開始、離すまで減速を継続
			EnhancedInput->BindAction(BrakeAction, ETriggerEvent::Started,
				this, &AHorseCharacter::BrakePressed);
			EnhancedInput->BindAction(BrakeAction, ETriggerEvent::Completed,
				this, &AHorseCharacter::BrakeReleased);
		}

		if (TiltAction)
		{
			// 傾け: 押している間 Axis1D 値で馬メッシュをロールさせる
			EnhancedInput->BindAction(TiltAction, ETriggerEvent::Triggered,
				this, &AHorseCharacter::TiltTriggered);
			EnhancedInput->BindAction(TiltAction, ETriggerEvent::Completed,
				this, &AHorseCharacter::TiltReleased);
		}
	}

	// レース開始は RaceManager の演出カメラ完了後に自動でカウントダウンへ移行する
	// （旧 Enter キー手動開始は廃止）。
}

// =====================================================================
// Tick — 毎フレーム処理（旋回・移動）
// =====================================================================

/**
 * Tick
 * 入力コールバックで保持した CurrentForwardInput / CurrentRightInput を
 * 毎フレーム参照して移動・旋回を適用する。
 *
 * ■ 旋回を Tick に移した理由
 *   Input コールバックは「入力イベントが来たとき」しか呼ばれないため、
 *   DeltaTime を掛けた旋回量が不安定になる場合がある。
 *   Tick 内で毎フレーム処理することで滑らかかつ確実な旋回を実現する。
 */
void AHorseCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// --- AI制御の更新 ---
	if (bIsAI)
	{
		UpdateAIControl(DeltaTime);
	}

	// --- コースルールの監視 ---
	CheckCourseRules(DeltaTime);

	// --- スタミナの更新（ダッシュ消費・回復・枯渇でダッシュ終了） ---
	UpdateStamina(DeltaTime);

	// --- 傾け（攻撃）入力を馬メッシュのロールへ反映 ---
	UpdateTilt(DeltaTime);

	// --- 入力ロックのシャドウ ---
	// bInputLocked == true の間はプレイヤー入力・AI 入力いずれの場合も 0 化する。
	// CurrentForwardInput/RightInput そのものは書き換えないため、AI 側の状態は破壊しない。
	const float EffectiveForward = bInputLocked ? 0.0f : CurrentForwardInput;
	const float EffectiveRight   = bInputLocked ? 0.0f : CurrentRightInput;

	// --- 旋回（A/D） ---
	// EffectiveRight: D=+1.0 / A=-1.0
	if (!FMath::IsNearlyZero(EffectiveRight))
	{
		const float DeltaYaw = EffectiveRight * TurnSpeed * DeltaTime;
		AddActorWorldRotation(FRotator(0.0f, DeltaYaw, 0.0f));
	}

	// --- ブレーキ減速 ---
	// ブレーキ中は前進入力を抑制し、BrakingDeceleration で水平速度を直接減衰させる。
	// 入力ロック中は減速しない（Pregame/Countdown/Finished）。
	if (bBraking && !bInputLocked)
	{
		if (UCharacterMovementComponent* CMC = GetCharacterMovement())
		{
			FVector Vel = CMC->Velocity;
			FVector HorizVel(Vel.X, Vel.Y, 0.0f);
			const float HorizSpeed = HorizVel.Size();
			if (HorizSpeed > KINDA_SMALL_NUMBER)
			{
				const float NewSpeed = FMath::Max(0.0f, HorizSpeed - BrakingDeceleration * DeltaTime);
				const FVector NewHoriz = HorizVel.GetSafeNormal() * NewSpeed;
				CMC->Velocity = FVector(NewHoriz.X, NewHoriz.Y, Vel.Z);
			}
		}
	}

	// --- 前進・後退（W/S） ---
	// 馬自身の前方ベクトルを使うことで S キーでも振り返らない
	// ブレーキ中は前進ドライブを止め、純粋に減速させる
	if (!bBraking && !FMath::IsNearlyZero(EffectiveForward))
	{
		AddMovementInput(GetActorForwardVector(), EffectiveForward);
	}

	// --- ピックアップヒント表示 ---
	DrawPickupHint();

	// --- ダッシュ中のカメラ補間 ---
	UpdateDashCameraBlend(DeltaTime);
}

// =====================================================================
// ダッシュカメラ補間
// =====================================================================
void AHorseCharacter::UpdateDashCameraBlend(float DeltaTime)
{
	if (!SpringArm) { return; }

	// 目標値の決定
	FVector  TargetLoc;
	FRotator TargetRot;
	float    TargetArmLen;

	if (bIsDashing && DashCameraPoint)
	{
		TargetLoc    = DashCameraPoint->GetRelativeLocation();
		TargetRot    = DashCameraPoint->GetRelativeRotation();
		TargetArmLen = DashCameraArmLength;
	}
	else
	{
		TargetLoc    = DefaultSpringArmRelLoc;
		TargetRot    = DefaultSpringArmRelRot;
		TargetArmLen = DefaultSpringArmLength;
	}

	// 補間（BlendSpeed=0 のときはスナップ）
	const FVector  NewLoc = (CameraBlendSpeed > 0.0f)
		? FMath::VInterpTo(SpringArm->GetRelativeLocation(), TargetLoc, DeltaTime, CameraBlendSpeed)
		: TargetLoc;
	const FRotator NewRot = (CameraBlendSpeed > 0.0f)
		? FMath::RInterpTo(SpringArm->GetRelativeRotation(), TargetRot, DeltaTime, CameraBlendSpeed)
		: TargetRot;
	const float    NewLen = (CameraBlendSpeed > 0.0f)
		? FMath::FInterpTo(SpringArm->TargetArmLength, TargetArmLen, DeltaTime, CameraBlendSpeed)
		: TargetArmLen;

	SpringArm->SetRelativeLocation(NewLoc);
	SpringArm->SetRelativeRotation(NewRot);
	SpringArm->TargetArmLength = NewLen;
}

// =====================================================================
// ピックアップ
// =====================================================================
void AHorseCharacter::TryPickupJockey()
{
	if (!CurrentJockey) { return; }
	// 落馬（KnockedOut）または引きずり（Swinging）中のジョッキーを対象にする
	if (!CurrentJockey->IsKnockedOut() && !CurrentJockey->IsSwinging()) { return; }

	// 距離はジョッキー実体（pelvis）位置で判定（ラグドールはアクタールートに追従しないため）
	const float Dist = FVector::Dist(GetActorLocation(), CurrentJockey->GetBodyWorldLocation());
	if (Dist <= PickupRadius)
	{
		CurrentJockey->WakeUpAndRide(this);
	}
}

// =====================================================================
// デバッグ: 強制ラグドール（BP入力から呼び出す）
// =====================================================================
void AHorseCharacter::DebugForceJockeyRagdoll()
{
	if (CurrentJockey)
	{
		CurrentJockey->ForceRagdoll();
	}
	else if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red,
			TEXT("[Horse][Debug] No CurrentJockey to ragdoll"));
	}
}

void AHorseCharacter::DrawPickupHint()
{
	if (!GEngine) { return; }
	if (!CurrentJockey || (!CurrentJockey->IsKnockedOut() && !CurrentJockey->IsSwinging())) { return; }

	const float Dist = FVector::Dist(GetActorLocation(), CurrentJockey->GetBodyWorldLocation());
	if (Dist > PickupRadius) { return; }

	GEngine->AddOnScreenDebugMessage(
		static_cast<int32>(GetUniqueID()) + 1,
		0.0f,
		FColor::Cyan,
		TEXT("[E] Pickup Jockey"));
}

// =====================================================================
// 入力ハンドラ
// =====================================================================

/**
 * Move（Triggered）
 * Value: Axis2D  X = 前後（W=+1 / S=-1）  Y = 左右（D=+1 / A=-1）
 * 入力値をメンバ変数に保持するだけ。実際の処理は Tick で行う。
 */
void AHorseCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();
	CurrentForwardInput = MovementVector.X;
	CurrentRightInput   = MovementVector.Y;
}

/**
 * MoveCompleted（Completed）
 * すべてのキーが離されたときに呼ばれる。
 * 入力値を 0 にリセットして Tick での移動・旋回を止める。
 */
void AHorseCharacter::MoveCompleted(const FInputActionValue& Value)
{
	CurrentForwardInput = 0.0f;
	CurrentRightInput   = 0.0f;
}

// =====================================================================
// 動物パラメータ適用（DataAsset → 各フィールド / CharacterMovement）
// =====================================================================
void AHorseCharacter::ApplyAnimalStats()
{
	// DataAsset が割り当てられていれば各フィールドへ反映（未割当時は既定値を維持）
	if (AnimalStats)
	{
		MoveSpeed                  = AnimalStats->MaxMoveSpeed;
		MaxAcceleration            = AnimalStats->MaxAcceleration;
		TurnSpeed                  = AnimalStats->TurnSpeed;
		DashSpeedMultiplier        = AnimalStats->DashSpeedMultiplier;
		DashAccelerationMultiplier = AnimalStats->DashAccelerationMultiplier;
		BrakingDeceleration        = AnimalStats->BrakingDeceleration;
		EjectForwardSpeed          = AnimalStats->EjectForwardSpeed;
		EjectUpSpeed               = AnimalStats->EjectUpSpeed;
		StaminaMax                 = AnimalStats->StaminaMax;
		StaminaDrainPerSec         = AnimalStats->StaminaDrainPerSec;
		StaminaRegenPerSec         = AnimalStats->StaminaRegenPerSec;
		StaminaRegenDelay          = AnimalStats->StaminaRegenDelay;
	}

	// スタミナを満タンに初期化
	CurrentStamina = StaminaMax;

	// CharacterMovement へ反映（ダッシュ中は倍率を維持）
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->RotationRate               = FRotator(0.0f, TurnSpeed, 0.0f);
		CMC->MaxWalkSpeed               = bIsDashing ? MoveSpeed * DashSpeedMultiplier : MoveSpeed;
		CMC->MaxAcceleration            = bIsDashing ? MaxAcceleration * DashAccelerationMultiplier : MaxAcceleration;
		CMC->BrakingDecelerationWalking = BrakingDeceleration;
	}
}

// =====================================================================
// ダッシュ
// =====================================================================
void AHorseCharacter::DashStarted()
{
	if (bIsDashing) { return; }

	// スタミナが残っていなければダッシュ不可
	if (CurrentStamina <= 0.0f) { return; }

	bIsDashing = true;
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->MaxWalkSpeed    = MoveSpeed * DashSpeedMultiplier;
		CMC->MaxAcceleration = MaxAcceleration * DashAccelerationMultiplier;
	}
}

void AHorseCharacter::DashCompleted()
{
	bIsDashing = false;
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->MaxWalkSpeed    = MoveSpeed;
		CMC->MaxAcceleration = MaxAcceleration;
	}
}

// =====================================================================
// スタミナ更新（ダッシュ消費 / 非ダッシュ回復 / 枯渇でダッシュ強制終了）
// =====================================================================
void AHorseCharacter::UpdateStamina(float DeltaTime)
{
	if (bIsDashing)
	{
		// ダッシュ中は消費し、回復猶予をリセット
		CurrentStamina = FMath::Max(0.0f, CurrentStamina - StaminaDrainPerSec * DeltaTime);
		StaminaRegenElapsed = 0.0f;

		// 枯渇したらダッシュを強制終了
		if (CurrentStamina <= 0.0f)
		{
			DashCompleted();
		}
	}
	else
	{
		// 非ダッシュ時は猶予経過後に回復
		StaminaRegenElapsed += DeltaTime;
		if (StaminaRegenElapsed >= StaminaRegenDelay)
		{
			CurrentStamina = FMath::Min(StaminaMax, CurrentStamina + StaminaRegenPerSec * DeltaTime);
		}
	}

	// デバッグ表示
	if (bShowStaminaDebug && GEngine)
	{
		const float Ratio = (StaminaMax > 0.0f) ? (CurrentStamina / StaminaMax) : 0.0f;
		const FColor BarColor = (Ratio > 0.5f) ? FColor::Green : (Ratio > 0.2f ? FColor::Yellow : FColor::Red);
		GEngine->AddOnScreenDebugMessage(
			static_cast<uint64>(GetUniqueID()), 0.0f, BarColor,
			FString::Printf(TEXT("Stamina: %.0f / %.0f"), CurrentStamina, StaminaMax));
	}
}

// =====================================================================
// ブレーキ → 射出判定（押下時）/ ブレーキ減速の開始・終了
// =====================================================================
void AHorseCharacter::BrakePressed()
{
	const float Speed = GetVelocity().Size();
	const bool bFastEnough = (Speed >= MinSpeedToEject);

	// ブレーキ減速を開始（離すまで Tick で BrakingDeceleration による減速を適用）
	bBraking = true;

	// ダッシュ中かつ十分な速度 → ジョッキー射出
	if (bIsDashing && bFastEnough && CurrentJockey && CurrentJockey->IsRiding())
	{
		EjectJockey();
	}
}

void AHorseCharacter::BrakeReleased()
{
	bBraking = false;
}

// =====================================================================
// ジョッキー射出
// =====================================================================
void AHorseCharacter::EjectJockey()
{
	// 既定は馬の現在前方へ射出（プレイヤー入力・既存呼び出しの互換）
	EjectJockey(GetActorForwardVector());
}

void AHorseCharacter::EjectJockey(const FVector& WorldAimDir)
{
	if (!CurrentJockey) { return; }

	// 指定方向の水平成分を狙いに使う。無効ならアクター前方へフォールバック
	FVector AimDir = WorldAimDir;
	AimDir.Z = 0.0f;
	if (!AimDir.Normalize())
	{
		AimDir = GetActorForwardVector();
		AimDir.Z = 0.0f;
		if (!AimDir.Normalize()) { return; }
	}

	// 狙い方向＋上方向で放物線速度を構築
	const FVector Velocity = AimDir * EjectForwardSpeed + FVector::UpVector * EjectUpSpeed;

	CurrentJockey->LaunchAsProjectile(Velocity);
}

// =====================================================================
// 傾け入力（攻撃）: Axis1D 値を保持
// =====================================================================
void AHorseCharacter::TiltTriggered(const FInputActionValue& Value)
{
	// マウス移動量はそのまま受け取る（移動量に比例して傾けるためクランプしない）
	TiltInput = Value.Get<float>();
}

void AHorseCharacter::TiltReleased()
{
	TiltInput = 0.0f;
}

// =====================================================================
// 傾け入力の角速度を監視し、素早い傾けでヘッドバンキング攻撃を発動する
// =====================================================================
void AHorseCharacter::UpdateTilt(float DeltaTime)
{
	// 入力ロック中はマウス移動を無視
	const float Input = bInputLocked ? 0.0f : TiltInput;
	TiltInput = 0.0f; // この1フレーム分のマウス移動量を消費

	// マウス移動量に比例して傾きを更新（移動量が大きいほど大きく傾く）。最大角でクランプ。
	// 符号を反転し、マウス右で右へ傾くようにする。
	CurrentHorseRoll = FMath::Clamp(
		CurrentHorseRoll - Input * HorseTiltSensitivity,
		-HeadbangHorseRollAngle, HeadbangHorseRollAngle);

	// 入力が無いフレームは中心(0)へ徐々に戻す（スプリングバック）
	if (FMath::IsNearlyZero(Input))
	{
		CurrentHorseRoll = FMath::FInterpTo(CurrentHorseRoll, 0.0f, DeltaTime, HorseTiltReturnSpeed);
	}

	// 馬メッシュを「既定相対回転 ＋ アクター前方軸まわりのロール」で傾ける。
	// 鞍ボーンも一緒に傾くため、着座拘束されたジョッキーが横へ振り出される。
	if (USkeletalMeshComponent* HorseMesh = GetMesh())
	{
		const FQuat BaseQ = DefaultMeshRelRot.Quaternion();
		const FQuat RollQ = FQuat(FVector::ForwardVector, FMath::DegreesToRadians(CurrentHorseRoll));
		HorseMesh->SetRelativeRotation(RollQ * BaseQ);
	}

	// 傾きが攻撃しきい値以上の間、ジョッキーの接触を攻撃として扱う
	if (CurrentJockey)
	{
		CurrentJockey->SetHeadbangActive(FMath::Abs(CurrentHorseRoll) >= HorseAttackRollThreshold);
	}
}


// =====================================================================
// AI自動走行の入力更新
// =====================================================================
void AHorseCharacter::UpdateAIControl(float DeltaTime)
{
	// 射出クールダウンの経過
	if (AIEjectCooldownTimer > 0.0f)
	{
		AIEjectCooldownTimer = FMath::Max(0.0f, AIEjectCooldownTimer - DeltaTime);
	}

	// 自ジョッキーが落馬中なら最優先で回収に向かう（通常レース挙動より優先）
	if (UpdateAIRecoverJockey(DeltaTime))
	{
		return;
	}

	if (!TrackActorRef)
	{
		// 見つからない場合は再検索を試みる
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATrackActor::StaticClass(), FoundActors);
		if (FoundActors.Num() > 0)
		{
			TrackActorRef = Cast<ATrackActor>(FoundActors[0]);
		}
	}

	if (!TrackActorRef)
	{
		CurrentForwardInput = 0.0f;
		CurrentRightInput = 0.0f;
		return;
	}

	USplineComponent* Spline = TrackActorRef->GetSpline();
	if (!Spline) { return; }

	// 馬の現在位置
	const FVector CurrentLoc = GetActorLocation();

	// スプライン上の最近接点での入力キーを取得
	const float ClosestKey = Spline->FindInputKeyClosestToWorldLocation(CurrentLoc);
	// 入力キーから現在のスプライン上の距離を取得
	const float ClosestDistance = Spline->GetDistanceAlongSplineAtSplineInputKey(ClosestKey);

	// 少し先の目標距離を計算（個体ごとの Lookahead ジッタを加算）
	const float SplineLength = Spline->GetSplineLength();
	const bool bClosed = Spline->IsClosedLoop();
	const float EffLookahead = FMath::Max(50.0f, LookaheadDistance + AILookaheadOffset);
	const float TargetDistance = NormalizeSplineDistance(ClosestDistance + EffLookahead, SplineLength, bClosed);

	// -------------------------------------------------------------
	// 曲率推定（速度制御・ライン取り・ダッシュ判定で共有）
	// Lookahead 目標距離の前後 ±CurvatureSampleStep の Tangent 差から
	// 曲率係数 CurvatureAlpha(0=直線〜1=最大屈曲) と曲がる向き InsideSign を求める。
	// -------------------------------------------------------------
	constexpr float MaxBendDegForFullSlow = 45.0f;
	float CurvatureAlpha = 0.0f;
	float InsideSign = 0.0f; // +1=右が内側 / -1=左が内側

	const float BackDistance = NormalizeSplineDistance(TargetDistance - CurvatureSampleStep, SplineLength, bClosed);
	const float FwdDistance  = NormalizeSplineDistance(TargetDistance + CurvatureSampleStep, SplineLength, bClosed);
	FVector TangentBack = Spline->GetTangentAtDistanceAlongSpline(BackDistance, ESplineCoordinateSpace::World);
	FVector TangentFwd  = Spline->GetTangentAtDistanceAlongSpline(FwdDistance,  ESplineCoordinateSpace::World);
	TangentBack.Z = 0.0f;
	TangentFwd.Z  = 0.0f;
	if (TangentBack.Normalize() && TangentFwd.Normalize())
	{
		const float CosBend = FVector::DotProduct(TangentBack, TangentFwd);
		const float BendDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(CosBend, -1.0f, 1.0f)));
		CurvatureAlpha = FMath::Clamp(BendDeg / MaxBendDegForFullSlow, 0.0f, 1.0f);

		// Tangent の回転方向（Z>0 で左旋回）。内側は旋回方向と同じ側。
		const float TurnZ = FVector::CrossProduct(TangentBack, TangentFwd).Z;
		InsideSign = (TurnZ > 0.0f) ? -1.0f : 1.0f; // 左旋回→内側は左(-) / 右旋回→内側は右(+)
	}

	// -------------------------------------------------------------
	// 位置取り（ライン取り）: 目標点をコース幅内で横方向にオフセットする。
	// 個体レーン嗜好＋コーナー内側バイアスを合成し、コース端余白でクランプ。
	// -------------------------------------------------------------
	FVector TargetLoc = Spline->GetLocationAtDistanceAlongSpline(TargetDistance, ESplineCoordinateSpace::World);

	float LaneRatio = AILanePreference * AILaneSpreadMax + InsideSign * CurvatureAlpha * AICornerCut;
	LaneRatio = FMath::Clamp(LaneRatio, -1.0f, 1.0f);

	const float HalfWidth = TrackActorRef->GetWidthAtDistance(TargetDistance);
	const float UsableHalf = FMath::Max(0.0f, HalfWidth - AILaneEdgeMargin);
	if (UsableHalf > KINDA_SMALL_NUMBER)
	{
		FVector TargetTangent = Spline->GetTangentAtDistanceAlongSpline(TargetDistance, ESplineCoordinateSpace::World);
		TargetTangent.Z = 0.0f;
		if (TargetTangent.Normalize())
		{
			FVector RightAtTarget = FVector::CrossProduct(FVector::UpVector, TargetTangent);
			if (RightAtTarget.Normalize())
			{
				TargetLoc += RightAtTarget * (LaneRatio * UsableHalf);
			}
		}
	}

	// 目標位置への方向ベクトル（水平面のみ）を計算
	FVector ToTarget = TargetLoc - CurrentLoc;
	ToTarget.Z = 0.0f;
	if (ToTarget.Normalize())
	{
		// 馬の現在前方方向
		FVector Forward = GetActorForwardVector();
		Forward.Z = 0.0f;
		Forward.Normalize();

		// 前方ベクトルと目標へのベクトルのドット積と外積（Z成分）から角度差を計算
		const float Dot = FVector::DotProduct(Forward, ToTarget);
		const FVector Cross = FVector::CrossProduct(Forward, ToTarget);

		// 角度偏差（ラジアン）を求める
		float AngleDiff = FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f));
		if (Cross.Z < 0.0f)
		{
			AngleDiff = -AngleDiff;
		}

		// ラジアンから度数へ変換
		const float AngleDiffDeg = FMath::RadiansToDegrees(AngleDiff);

		// 偏差に応じて CurrentRightInput（旋回量 -1.0 〜 1.0）を設定
		// ここでは偏差30度以上で最大入力（1.0 / -1.0）になるように設定
		CurrentRightInput = FMath::Clamp(AngleDiffDeg / 30.0f, -1.0f, 1.0f);

		// 曲率が大きいほど前進入力を 1.0 → MinAISpeedRatio へ線形に低下
		CurrentForwardInput = FMath::Lerp(1.0f, MinAISpeedRatio, CurvatureAlpha);

		// -------------------------------------------------------------
		// 他馬の一括取得（回避・攻撃・ダッシュ前方判定で共有）
		// -------------------------------------------------------------
		TArray<AActor*> FoundHorses;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AHorseCharacter::StaticClass(), FoundHorses);
		TArray<AHorseCharacter*> OtherHorses;
		OtherHorses.Reserve(FoundHorses.Num());
		for (AActor* Actor : FoundHorses)
		{
			AHorseCharacter* OtherHorse = Cast<AHorseCharacter>(Actor);
			if (OtherHorse && OtherHorse != this)
			{
				OtherHorses.Add(OtherHorse);
			}
		}

		// -------------------------------------------------------------
		// 攻撃AI: 近接した相手ジョッキーへヘッドバングを狙う。
		// 交戦中は寄せる操舵バイアスを加え、通常の回避を抑制する。
		// -------------------------------------------------------------
		float AttackSteerBias = 0.0f;
		const bool bAttacking = UpdateAIAttack(OtherHorses, AttackSteerBias);

		if (bAttacking)
		{
			// 攻撃対象へわずかに寄せて接触を作る（回避はしない）
			CurrentRightInput = FMath::Clamp(CurrentRightInput + AttackSteerBias, -1.0f, 1.0f);
		}
		else
		{
			// -------------------------------------------------------------
			// 他馬回避（近傍検索＋旋回オフセット）
			// -------------------------------------------------------------
			AHorseCharacter* NearestOther = nullptr;
			float NearestDist = AvoidanceRadius;
			for (AHorseCharacter* OtherHorse : OtherHorses)
			{
				const float Dist = FVector::Dist2D(CurrentLoc, OtherHorse->GetActorLocation());
				if (Dist < NearestDist)
				{
					NearestDist = Dist;
					NearestOther = OtherHorse;
				}
			}

			if (NearestOther)
			{
				FVector ToOther = NearestOther->GetActorLocation() - CurrentLoc;
				ToOther.Z = 0.0f;
				FVector RightVec = GetActorRightVector();
				RightVec.Z = 0.0f;
				if (ToOther.Normalize() && RightVec.Normalize())
				{
					const float SideDot = FVector::DotProduct(ToOther, RightVec);
					const float Proximity = 1.0f - FMath::Clamp(NearestDist / AvoidanceRadius, 0.0f, 1.0f);
					constexpr float HeadOnThreshold = 0.05f;
					float SideSign = (FMath::Abs(SideDot) < HeadOnThreshold)
						? 1.0f
						: -FMath::Sign(SideDot);
					const float AvoidOffset = SideSign * Proximity * AvoidanceWeight;
					CurrentRightInput = FMath::Clamp(CurrentRightInput + AvoidOffset, -1.0f, 1.0f);
				}
			}
		}

		// -------------------------------------------------------------
		// ダッシュAI: 直線・前方クリア・スタミナ・積極性で加速判断
		// -------------------------------------------------------------
		UpdateAIDash(CurvatureAlpha, OtherHorses);

		// -------------------------------------------------------------
		// 射出AI: 前方の騎乗中の相手へジョッキーを射出する（攻撃的な個体のみ）
		// -------------------------------------------------------------
		UpdateAIEject(OtherHorses);
	}
	else
	{
		CurrentForwardInput = 0.0f;
		CurrentRightInput = 0.0f;
	}
}

// =====================================================================
// AI個体差プロファイルの確定（個体値範囲内で乱数決定）
// =====================================================================
void AHorseCharacter::InitializeAIProfile()
{
	if (bAIProfileInitialized) { return; }
	bAIProfileInitialized = true;

	// レーン嗜好は左端〜右端の連続値。個体ごとの走行ラインの違いを生む。
	AILanePreference = FMath::FRandRange(-1.0f, 1.0f);

	// コーナーのイン取り量（0〜AICornerCutMax）。
	AICornerCut = FMath::FRandRange(0.0f, FMath::Max(0.0f, AICornerCutMax));

	// 積極性（ダッシュ・攻撃の発火しやすさ）。
	const float AggLo = FMath::Min(AIAggressionMin, AIAggressionMax);
	const float AggHi = FMath::Max(AIAggressionMin, AIAggressionMax);
	AIAggression = FMath::FRandRange(AggLo, AggHi);

	// Lookahead の個体ジッタ（±AILookaheadJitter）。
	AILookaheadOffset = FMath::FRandRange(-AILookaheadJitter, AILookaheadJitter);

	// 速度スケールを MoveSpeed に反映して top speed を個体化する。
	const float ScaleLo = FMath::Min(AISpeedScaleMin, AISpeedScaleMax);
	const float ScaleHi = FMath::Max(AISpeedScaleMin, AISpeedScaleMax);
	const float SpeedScale = FMath::FRandRange(ScaleLo, ScaleHi);
	MoveSpeed *= SpeedScale;

	// CharacterMovement の最大速度へ即時反映（ダッシュ中なら倍率を維持）。
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->MaxWalkSpeed = bIsDashing ? MoveSpeed * DashSpeedMultiplier : MoveSpeed;
	}
}

// =====================================================================
// AIダッシュ判断
// =====================================================================
void AHorseCharacter::UpdateAIDash(float CurvatureAlpha, const TArray<AHorseCharacter*>& OtherHorses)
{
	// リザルト演出中・入力ロック中・消極的な個体はダッシュしない
	if (bExhibitionMode || bInputLocked || AIAggression < AIDashAggressionThreshold)
	{
		if (bIsDashing) { DashCompleted(); }
		return;
	}

	// 直線条件
	const bool bStraight = (CurvatureAlpha < AIDashCurvatureMax);

	// スタミナ条件
	const float StaminaRatio = (StaminaMax > 0.0f) ? (CurrentStamina / StaminaMax) : 0.0f;
	const bool bStaminaOK = (StaminaRatio > AIDashMinStaminaRatio);

	// 前方クリア条件: 前方コーン内の他馬が AIDashForwardClearDist 以内に居れば不可
	bool bForwardClear = true;
	const FVector CurrentLoc = GetActorLocation();
	FVector Forward = GetActorForwardVector();
	Forward.Z = 0.0f;
	if (Forward.Normalize())
	{
		for (AHorseCharacter* OtherHorse : OtherHorses)
		{
			FVector ToOther = OtherHorse->GetActorLocation() - CurrentLoc;
			ToOther.Z = 0.0f;
			const float Dist = ToOther.Size();
			if (Dist > AIDashForwardClearDist) { continue; }
			if (Dist <= KINDA_SMALL_NUMBER) { bForwardClear = false; break; }
			// 前方コーン内（dot>0.5 ≒ ±60度）に居れば塞がれている
			if (FVector::DotProduct(Forward, ToOther / Dist) > 0.5f)
			{
				bForwardClear = false;
				break;
			}
		}
	}

	const bool bShouldDash = bStraight && bStaminaOK && bForwardClear;
	if (bShouldDash && !bIsDashing)
	{
		DashStarted();
	}
	else if (!bShouldDash && bIsDashing)
	{
		DashCompleted();
	}
}

// =====================================================================
// AI攻撃判断（ヘッドバングで相手ジョッキーの落馬を狙う）
// =====================================================================
bool AHorseCharacter::UpdateAIAttack(const TArray<AHorseCharacter*>& OtherHorses, float& OutSteerBias)
{
	OutSteerBias = 0.0f;

	// リザルト演出中・入力ロック中・消極的な個体・自分が騎乗していない場合は攻撃しない
	if (bExhibitionMode || bInputLocked || AIAggression < AIAttackAggressionThreshold)
	{
		return false;
	}
	if (!CurrentJockey || !CurrentJockey->IsRiding() || CurrentJockey->IsKnockedOut())
	{
		return false;
	}

	const FVector CurrentLoc = GetActorLocation();
	FVector Forward = GetActorForwardVector();
	Forward.Z = 0.0f;
	FVector RightVec = GetActorRightVector();
	RightVec.Z = 0.0f;
	if (!Forward.Normalize() || !RightVec.Normalize())
	{
		return false;
	}

	// 攻撃可能な最近接の相手（騎乗中・未落馬のジョッキーを持つ馬）を探す
	AHorseCharacter* Target = nullptr;
	float NearestDist = AIAttackRadius;
	float TargetSideDot = 0.0f;
	for (AHorseCharacter* OtherHorse : OtherHorses)
	{
		AJockey* OtherJockey = OtherHorse->GetCurrentJockey();
		if (!OtherJockey || !OtherJockey->IsRiding() || OtherJockey->IsKnockedOut())
		{
			continue;
		}

		FVector ToOther = OtherHorse->GetActorLocation() - CurrentLoc;
		ToOther.Z = 0.0f;
		const float Dist = ToOther.Size();
		if (Dist >= NearestDist || Dist <= KINDA_SMALL_NUMBER) { continue; }

		const FVector Dir = ToOther / Dist;
		// 相手が自馬の横〜前方に居るか（後方の相手は攻撃しない）
		if (FVector::DotProduct(Forward, Dir) < AIAttackForwardDot) { continue; }

		NearestDist = Dist;
		Target = OtherHorse;
		TargetSideDot = FVector::DotProduct(Dir, RightVec);
	}

	if (!Target)
	{
		return false;
	}

	// 相手の居る側へ馬を倒す。UpdateTilt の規約は「Input>0(マウス右)で右へ傾く」。
	// 相手が右側(SideSign=+1)なら正の Input、左側なら負の Input を与える。
	// 正面付近(side≈0)は右へ倒す固定バイアスで膠着を防ぐ。
	constexpr float SideThreshold = 0.05f;
	const float SideSign = (FMath::Abs(TargetSideDot) < SideThreshold) ? 1.0f : FMath::Sign(TargetSideDot);

	TiltInput = SideSign * AIAttackTiltDrive;

	// 攻撃対象へわずかに寄せる操舵バイアス（接触を作る）
	OutSteerBias = SideSign * 0.3f;
	return true;
}

// =====================================================================
// AI自ジョッキー回収（落馬したジョッキーへ走行してピックアップ）
// =====================================================================
bool AHorseCharacter::UpdateAIRecoverJockey(float DeltaTime)
{
	// 自ジョッキーが落馬中（KnockedOut）でなければ回収モードに入らない
	if (!CurrentJockey || !CurrentJockey->IsKnockedOut())
	{
		// 復帰済み → 待機状態をリセット
		bAIWaitingToRecover = false;
		AIRecoverDelayTimer = 0.0f;
		return false;
	}

	// 攻撃の傾けはしない（回収優先）
	TiltInput = 0.0f;

	// 落馬を検出した最初のフレームで待機タイマーを開始する。
	// 即回収だと人間が落馬に気づけないため、AIRecoverDelay 秒の「間」を作る。
	if (!bAIWaitingToRecover)
	{
		bAIWaitingToRecover = true;
		AIRecoverDelayTimer = AIRecoverDelay;
	}

	// 待機中はその場で停止（落馬を視認させる）。回収はまだ行わない。
	if (AIRecoverDelayTimer > 0.0f)
	{
		AIRecoverDelayTimer = FMath::Max(0.0f, AIRecoverDelayTimer - DeltaTime);
		CurrentForwardInput = 0.0f;
		CurrentRightInput = 0.0f;
		return true;
	}

	const FVector CurrentLoc = GetActorLocation();
	const FVector JockeyLoc = CurrentJockey->GetBodyWorldLocation();

	// ジョッキーへ向けて操舵
	FVector ToJockey = JockeyLoc - CurrentLoc;
	ToJockey.Z = 0.0f;
	if (ToJockey.Normalize())
	{
		FVector Forward = GetActorForwardVector();
		Forward.Z = 0.0f;
		Forward.Normalize();

		const float Dot = FVector::DotProduct(Forward, ToJockey);
		const FVector Cross = FVector::CrossProduct(Forward, ToJockey);
		float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f)));
		if (Cross.Z < 0.0f) { AngleDeg = -AngleDeg; }

		CurrentRightInput = FMath::Clamp(AngleDeg / 30.0f, -1.0f, 1.0f);

		// 近づいたら減速して行き過ぎを防ぐ
		const float Dist = FVector::Dist2D(CurrentLoc, JockeyLoc);
		CurrentForwardInput = (Dist > PickupRadius * 1.5f) ? 1.0f : 0.4f;
	}
	else
	{
		CurrentForwardInput = 0.0f;
		CurrentRightInput = 0.0f;
	}

	// 範囲内なら拾い上げる（TryPickupJockey が内部で距離・状態を再判定する）
	TryPickupJockey();
	return true;
}

// =====================================================================
// AI射出（前方の騎乗中の相手へジョッキーを射出して落馬を狙う高リスク技）
// =====================================================================
void AHorseCharacter::UpdateAIEject(const TArray<AHorseCharacter*>& OtherHorses)
{
	// リザルト演出中・入力ロック中・消極的な個体・クールダウン中・未騎乗なら射出しない
	if (bExhibitionMode || bInputLocked || AIAggression < AIEjectAggressionThreshold)
	{
		return;
	}
	if (AIEjectCooldownTimer > 0.0f) { return; }
	if (!CurrentJockey || !CurrentJockey->IsRiding() || CurrentJockey->IsKnockedOut())
	{
		return;
	}

	// 順位情報が無ければ「奥の手＝先頭馬狙い」を判定できないので射出しない
	if (!CachedRaceManager) { return; }

	// 順位ゲート: 先頭(1) または未登録(0) は射出しない。2位以下の追走個体のみが奥の手を使う。
	const int32 SelfRank = CachedRaceManager->GetRankOf(this);
	if (SelfRank <= 1) { return; }

	// 先頭馬を取得（GetRanking は順位順ソート済みのため先頭が [0]）
	const TArray<FRaceEntry> Ranking = CachedRaceManager->GetRanking();
	if (Ranking.Num() == 0) { return; }
	AHorseCharacter* Leader = Ranking[0].Horse;
	if (!Leader || Leader == this) { return; }

	// 先頭馬のジョッキーが騎乗中でなければ射出しても落馬させられないので狙わない
	AJockey* LeaderJockey = Leader->GetCurrentJockey();
	if (!LeaderJockey || !LeaderJockey->IsRiding() || LeaderJockey->IsKnockedOut())
	{
		return;
	}

	// 先頭馬との水平距離が射程内のときのみ実行（物理的に届く時だけ）
	const FVector CurrentLoc = GetActorLocation();
	FVector ToLeader = Leader->GetActorLocation() - CurrentLoc;
	ToLeader.Z = 0.0f;
	const float Dist = ToLeader.Size();
	if (Dist > AIEjectRange || Dist <= KINDA_SMALL_NUMBER) { return; }
	const FVector AimDir = ToLeader / Dist;

	// 向き補正: 前方ベクトルと先頭馬方向の外積Z符号から、先頭馬側へ操舵バイアスを加える
	FVector Forward = GetActorForwardVector();
	Forward.Z = 0.0f;
	if (Forward.Normalize())
	{
		const float CrossZ = FVector::CrossProduct(Forward, AimDir).Z;
		if (!FMath::IsNearlyZero(CrossZ))
		{
			CurrentRightInput = FMath::Clamp(
				CurrentRightInput + FMath::Sign(CrossZ) * AIEjectAimSteer, -1.0f, 1.0f);
		}
	}

	// 先頭馬方向へ射出。以降は自ジョッキーが落馬するため UpdateAIRecoverJockey が回収に移行する。
	EjectJockey(AimDir);
	AIEjectCooldownTimer = AIEjectCooldown;
}

// =====================================================================
// スプライン距離の正規化（ClosedLoop=Fmod / 非Loop=Clamp）
// =====================================================================
float AHorseCharacter::NormalizeSplineDistance(float Distance, float SplineLength, bool bClosed) const
{
	if (SplineLength <= 0.0f)
	{
		return 0.0f;
	}

	if (bClosed)
	{
		// ループコースの場合、距離を一周の範囲に収める
		float Normalized = FMath::Fmod(Distance, SplineLength);
		if (Normalized < 0.0f)
		{
			Normalized += SplineLength;
		}
		return Normalized;
	}

	// ループでない場合、終端でクランプ
	return FMath::Clamp(Distance, 0.0f, SplineLength);
}

// =====================================================================
// Possess された瞬間に GhostRecorder を付与する
// =====================================================================
void AHorseCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	APlayerController* PC = Cast<APlayerController>(NewController);
	if (!PC) { return; }

	// EnhancedInput の MappingContext を登録する。
	// BeginPlay 時点ではまだ Possess されていないため、ここで行うのが確実。
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		if (DefaultMappingContext)
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}

	// PlayerController に Possess された馬にのみ GhostRecorder を付与する。
	if (!GhostRecorder)
	{
		GhostRecorder = NewObject<UGhostRecorder>(this);
		if (GhostRecorder)
		{
			GhostRecorder->RegisterComponent();
		}
	}
}

// =====================================================================
// レース状態変化ハンドラ
// =====================================================================
void AHorseCharacter::HandleRaceStateChanged(ERaceState NewState)
{
	// Pregame / Countdown では入力ロック、Running 解除、Finished は再ロック
	switch (NewState)
	{
	case ERaceState::Pregame:
	case ERaceState::Countdown:
		SetInputLocked(true);
		break;
	case ERaceState::Running:
		SetInputLocked(false);
		break;
	case ERaceState::Finished:
		// エキシビション中はリザルト演出のため再ロックしない（走り続ける）
		if (!bExhibitionMode)
		{
			SetInputLocked(true);
		}
		break;
	default:
		break;
	}
}

// =====================================================================
// エキシビション走行開始（リザルト演出用）
// =====================================================================
void AHorseCharacter::StartExhibitionRun()
{
	// AI 自律走行を有効化し、入力ロックを解除して走り続けさせる。
	bExhibitionMode = true;
	bIsAI = true;
	SetInputLocked(false);
}

// =====================================================================
// 逆走・コースアウトの監視
// =====================================================================
void AHorseCharacter::CheckCourseRules(float DeltaTime)
{
	if (!TrackActorRef) { return; }
	USplineComponent* Spline = TrackActorRef->GetSpline();
	if (!Spline) { return; }

	const FVector CurrentLoc = GetActorLocation();

	// スプライン上の最近接点での入力キーを取得
	const float ClosestKey = Spline->FindInputKeyClosestToWorldLocation(CurrentLoc);
	// 入力キーから現在のスプライン上の距離を取得
	const float ClosestDistance = Spline->GetDistanceAlongSplineAtSplineInputKey(ClosestKey);

	// 1. 逆走検知 (Wrong Way)
	FVector Forward = GetActorForwardVector();
	Forward.Z = 0.0f;
	Forward.Normalize();

	FVector SplineDirection = Spline->GetDirectionAtDistanceAlongSpline(ClosestDistance, ESplineCoordinateSpace::World);
	SplineDirection.Z = 0.0f;
	SplineDirection.Normalize();

	const float DirectionDot = FVector::DotProduct(Forward, SplineDirection);

	// 内積が -0.5f 以下の場合は逆走とみなす（およそ120度以上ずれている）
	bWrongWay = (DirectionDot < -0.5f);

	if (bWrongWay && !bIsAI)
	{
		// プレイヤーの場合は画面に警告を表示
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				static_cast<int32>(GetUniqueID()) + 100,
				0.0f,
				FColor::Red,
				TEXT("WRONG WAY!"),
				true,
				FVector2D(2.0f, 2.0f)
			);
		}
	}

	// 2. コースアウト監視 (OutOfCourse)
	const FVector ClosestSplineLoc = Spline->GetLocationAtDistanceAlongSpline(ClosestDistance, ESplineCoordinateSpace::World);
	
	FVector DistVec = CurrentLoc - ClosestSplineLoc;
	const float DistanceFromSpline = DistVec.Size();

	// M5: コース幅可変化。固定値ではなく TrackActor が距離ごとに保持する許容幅を使う
	const float AllowedWidth = TrackActorRef->GetWidthAtDistance(ClosestDistance);

	if (DistanceFromSpline > AllowedWidth)
	{
		OutOfCourseTimer += DeltaTime;
		if (OutOfCourseTimer >= OutOfCourseTimeout)
		{
			ResetToTrack();
		}
		
		if (!bIsAI && GEngine)
		{
			const float RemainingTime = FMath::Max(0.0f, OutOfCourseTimeout - OutOfCourseTimer);
			GEngine->AddOnScreenDebugMessage(
				static_cast<int32>(GetUniqueID()) + 101,
				0.0f,
				FColor::Orange,
				FString::Printf(TEXT("OUT OF COURSE! Reseting in %.1fs (Allowed=%.0f)"), RemainingTime, AllowedWidth)
			);
		}
	}
	else
	{
		OutOfCourseTimer = 0.0f;
	}
}

// =====================================================================
// 自動復帰処理
// =====================================================================
void AHorseCharacter::ResetToTrack()
{
	if (!TrackActorRef) { return; }
	USplineComponent* Spline = TrackActorRef->GetSpline();
	if (!Spline) { return; }

	const FVector CurrentLoc = GetActorLocation();

	// スプライン上の最近接点を見つける
	const float ClosestKey = Spline->FindInputKeyClosestToWorldLocation(CurrentLoc);
	const float ClosestDistance = Spline->GetDistanceAlongSplineAtSplineInputKey(ClosestKey);

	// 復帰座標（スプラインの真上あたり、Zを少し浮かせる）
	FVector ResetLoc = Spline->GetLocationAtDistanceAlongSpline(ClosestDistance, ESplineCoordinateSpace::World);
	ResetLoc.Z += 100.0f; // 少し浮かせて地面埋まりを避ける

	// 復帰角度（スプラインの進行方向を向く）
	const FRotator ResetRot = Spline->GetRotationAtDistanceAlongSpline(ClosestDistance, ESplineCoordinateSpace::World);

	// 速度のクリア
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->StopMovementImmediately();
	}

	// テレポート
	const FRotator TargetRot = FRotator(0.0f, ResetRot.Yaw, 0.0f);
	TeleportTo(ResetLoc, TargetRot);

	// ジョッキーが落馬している場合、強制的に再騎乗
	if (CurrentJockey && CurrentJockey->IsKnockedOut())
	{
		CurrentJockey->WakeUpAndRide(this);
	}

	// タイマーと入力をリセット
	OutOfCourseTimer = 0.0f;
	CurrentForwardInput = 0.0f;
	CurrentRightInput = 0.0f;

	// RaceManager に Reset 直後通知（次フレームのラップ境界判定をスキップさせる）
	if (CachedRaceManager)
	{
		CachedRaceManager->MarkJustReset(this);
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan,
			FString::Printf(TEXT("[Race] %s reset to track"), *GetName()));
	}
}
