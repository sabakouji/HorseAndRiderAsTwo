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

	// UPROPERTY で設定された値を MovementComponent へ反映
	GetCharacterMovement()->RotationRate  = FRotator(0.0f, TurnSpeed, 0.0f);
	GetCharacterMovement()->MaxWalkSpeed  = MoveSpeed;

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

	// プレイヤー馬にのみゴースト記録コンポーネントを動的付与する。
	if (!bIsAI && !GhostRecorder)
	{
		GhostRecorder = NewObject<UGhostRecorder>(this);
		if (GhostRecorder)
		{
			GhostRecorder->RegisterComponent();
		}
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
			// ブレーキ: 押した瞬間に 1 回だけ射出判定
			EnhancedInput->BindAction(BrakeAction, ETriggerEvent::Started,
				this, &AHorseCharacter::BrakePressed);
		}

		if (DebugSwingAction)
		{
			// 振り回し toggle: 押した瞬間に 1 回だけ切替
			EnhancedInput->BindAction(DebugSwingAction, ETriggerEvent::Started,
				this, &AHorseCharacter::ToggleDebugSwing);
		}
	}

	// デバッグ用: Enter キーでレースのカウントダウンを開始する。
	// EnhancedInput の InputAction アセットを別途作成せずに済むよう、
	// レガシーの BindKey を使用する（EnhancedInputComponent でも併用可能）。
	if (PlayerInputComponent)
	{
		PlayerInputComponent->BindKey(EKeys::Enter, IE_Pressed,
			this, &AHorseCharacter::DebugStartRace);
		PlayerInputComponent->BindKey(EKeys::Gamepad_Special_Right, IE_Pressed,
			this, &AHorseCharacter::DebugStartRace);
	}
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

	// --- 前進・後退（W/S） ---
	// 馬自身の前方ベクトルを使うことで S キーでも振り返らない
	if (!FMath::IsNearlyZero(EffectiveForward))
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
	if (!CurrentJockey->IsKnockedOut()) { return; }

	const float Dist = FVector::Dist(GetActorLocation(), CurrentJockey->GetActorLocation());
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
	if (!CurrentJockey || !CurrentJockey->IsKnockedOut()) { return; }

	const float Dist = FVector::Dist(GetActorLocation(), CurrentJockey->GetActorLocation());
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
// ダッシュ
// =====================================================================
void AHorseCharacter::DashStarted()
{
	if (bIsDashing) { return; }
	bIsDashing = true;
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->MaxWalkSpeed = MoveSpeed * DashSpeedMultiplier;
	}
}

void AHorseCharacter::DashCompleted()
{
	bIsDashing = false;
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->MaxWalkSpeed = MoveSpeed;
	}
}

// =====================================================================
// ブレーキ → 射出判定
// =====================================================================
void AHorseCharacter::BrakePressed()
{
	const float Speed = GetVelocity().Size();
	const bool bFastEnough = (Speed >= MinSpeedToEject);

	// 急ブレーキ: 入力をリセット
	CurrentForwardInput = 0.0f;
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->StopMovementImmediately();
	}

	// ダッシュ中かつ十分な速度 → ジョッキー射出
	if (bIsDashing && bFastEnough && CurrentJockey && CurrentJockey->IsRiding())
	{
		EjectJockey();
	}
}

// =====================================================================
// ジョッキー射出
// =====================================================================
void AHorseCharacter::EjectJockey()
{
	if (!CurrentJockey) { return; }

	// 馬の現在前方＋上方向で放物線速度を構築
	const FVector Forward = GetActorForwardVector();
	const FVector Up = FVector::UpVector;
	const FVector Velocity = Forward * EjectForwardSpeed + Up * EjectUpSpeed;

	CurrentJockey->LaunchAsProjectile(Velocity);
}

// =====================================================================
// デバッグ: 振り回しモード toggle
// =====================================================================
void AHorseCharacter::ToggleDebugSwing()
{
	if (!CurrentJockey) { return; }

	if (CurrentJockey->IsSwinging())
	{
		CurrentJockey->ExitSwingMode();
	}
	else
	{
		USkeletalMeshComponent* HorseMesh = GetMesh();
		if (HorseMesh)
		{
			CurrentJockey->EnterSwingMode(HorseMesh, SwingAnchorSocketName);
		}
	}
}

// =====================================================================
// デバッグ: Enter キーでレースのカウントダウンを開始する
// =====================================================================
void AHorseCharacter::DebugStartRace()
{
	// キャッシュが無ければレベルから RaceManager を検索する
	if (!CachedRaceManager)
	{
		TArray<AActor*> FoundManagers;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ARaceManager::StaticClass(), FoundManagers);
		if (FoundManagers.Num() > 0)
		{
			CachedRaceManager = Cast<ARaceManager>(FoundManagers[0]);
		}
	}

	if (CachedRaceManager)
	{
		CachedRaceManager->StartCountdown();

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan,
				TEXT("[Debug] StartCountdown() requested"));
		}
	}
	else if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
			TEXT("[Debug] RaceManager not found"));
	}
}

// =====================================================================
// AI自動走行の入力更新
// =====================================================================
void AHorseCharacter::UpdateAIControl(float DeltaTime)
{
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

	// 少し先の目標距離を計算
	const float SplineLength = Spline->GetSplineLength();
	const bool bClosed = Spline->IsClosedLoop();
	const float TargetDistance = NormalizeSplineDistance(ClosestDistance + LookaheadDistance, SplineLength, bClosed);

	// 目標距離におけるスプライン上の目標位置を取得
	const FVector TargetLoc = Spline->GetLocationAtDistanceAlongSpline(TargetDistance, ESplineCoordinateSpace::World);

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

		// -------------------------------------------------------------
		// 曲率推定による前進入力スケール（AI 速度制御）
		// Lookahead 目標距離の前後 ±CurvatureSampleStep における
		// スプライン Tangent の向きの差分から曲率を推定し、
		// 曲率が大きいほど前進入力を MinAISpeedRatio まで段階的に絞る。
		// -------------------------------------------------------------
		// この角度差(度)以上の屈曲で最大減速とする閾値（マジックナンバー回避のため名前付き定数化）
		constexpr float MaxBendDegForFullSlow = 45.0f;

		float SpeedRatio = 1.0f;

		const float BackDistance = NormalizeSplineDistance(TargetDistance - CurvatureSampleStep, SplineLength, bClosed);
		const float FwdDistance  = NormalizeSplineDistance(TargetDistance + CurvatureSampleStep, SplineLength, bClosed);

		FVector TangentBack = Spline->GetTangentAtDistanceAlongSpline(BackDistance, ESplineCoordinateSpace::World);
		FVector TangentFwd  = Spline->GetTangentAtDistanceAlongSpline(FwdDistance,  ESplineCoordinateSpace::World);
		TangentBack.Z = 0.0f;
		TangentFwd.Z  = 0.0f;

		// Tangent が正常に正規化できた場合のみ曲率減速を適用（ゼロ長時は減速なしでフォールバック）
		if (TangentBack.Normalize() && TangentFwd.Normalize())
		{
			const float CosBend = FVector::DotProduct(TangentBack, TangentFwd);
			const float BendDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(CosBend, -1.0f, 1.0f)));

			// 曲率係数（0=直線 〜 1=最大屈曲）
			const float CurvatureAlpha = FMath::Clamp(BendDeg / MaxBendDegForFullSlow, 0.0f, 1.0f);

			// 曲率が大きいほど前進入力を 1.0 → MinAISpeedRatio へ線形に低下
			SpeedRatio = FMath::Lerp(1.0f, MinAISpeedRatio, CurvatureAlpha);
		}

		// 前進入力は曲率に応じてスケールした値
		CurrentForwardInput = SpeedRatio;

		// -------------------------------------------------------------
		// 他馬回避（近傍検索＋旋回オフセット）
		// 自馬中心から AvoidanceRadius 内の最近接他馬を検索し、
		// 相手と反対方向へ CurrentRightInput に回避オフセットを加算する。
		// -------------------------------------------------------------
		TArray<AActor*> OtherHorses;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AHorseCharacter::StaticClass(), OtherHorses);

		AHorseCharacter* NearestOther = nullptr;
		float NearestDist = AvoidanceRadius;

		for (AActor* Actor : OtherHorses)
		{
			AHorseCharacter* OtherHorse = Cast<AHorseCharacter>(Actor);
			if (!OtherHorse || OtherHorse == this) { continue; }

			const float Dist = FVector::Dist2D(CurrentLoc, OtherHorse->GetActorLocation());
			if (Dist < NearestDist)
			{
				NearestDist = Dist;
				NearestOther = OtherHorse;
			}
		}

		if (NearestOther)
		{
			// 自馬から相手への水平方向ベクトル
			FVector ToOther = NearestOther->GetActorLocation() - CurrentLoc;
			ToOther.Z = 0.0f;

			// 自馬の右ベクトル（水平）
			FVector RightVec = GetActorRightVector();
			RightVec.Z = 0.0f;

			if (ToOther.Normalize() && RightVec.Normalize())
			{
				// 相手が右側(+)か左側(-)か。反対方向へ避けるため符号を反転して使用
				const float SideDot = FVector::DotProduct(ToOther, RightVec);

				// 近いほど強く避ける（0〜1）
				const float Proximity = 1.0f - FMath::Clamp(NearestDist / AvoidanceRadius, 0.0f, 1.0f);

				// 正面接近（SideDot≈0）で回避方向が不定になり膠着するのを防ぐため、
				// その場合は固定で右方向へ微小バイアスを与える
				constexpr float HeadOnThreshold = 0.05f;
				float SideSign = (FMath::Abs(SideDot) < HeadOnThreshold)
					? 1.0f                       // 正面 → 右へ避ける固定バイアス
					: -FMath::Sign(SideDot);     // 相手と反対方向

				const float AvoidOffset = SideSign * Proximity * AvoidanceWeight;
				CurrentRightInput = FMath::Clamp(CurrentRightInput + AvoidOffset, -1.0f, 1.0f);
			}
		}
	}
	else
	{
		CurrentForwardInput = 0.0f;
		CurrentRightInput = 0.0f;
	}
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
		SetInputLocked(true);
		break;
	default:
		break;
	}
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
