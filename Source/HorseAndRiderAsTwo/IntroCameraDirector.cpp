#include "IntroCameraDirector.h"
#include "Camera/CameraComponent.h"
#include "Components/SplineComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "RaceManager.h"
#include "TrackActor.h"
#include "HorseCharacter.h"

// =====================================================================
// コンストラクタ
// =====================================================================
AIntroCameraDirector::AIntroCameraDirector()
{
	PrimaryActorTick.bCanEverTick = true;

	IntroCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("IntroCamera"));
	RootComponent = IntroCamera;
}

// =====================================================================
// 演出開始
// =====================================================================
void AIntroCameraDirector::StartIntro(ARaceManager* InManager)
{
	Manager = InManager;
	if (!Manager)
	{
		// 起動できない場合は何もせず破棄（呼び出し側がカウントダウンを別途行う想定）
		Destroy();
		return;
	}

	Track = Manager->GetTrack();

	// グリッド基準が取れない場合は演出をスキップして即カウントダウンへ
	if (!ComputeGridReference())
	{
		FinishIntro();
		return;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC || NumShots() == 0)
	{
		FinishIntro();
		return;
	}

	// 最初のカット位置を確定してから、ブレンドせず即座にビューを切り替える
	ShotIndex = 0;
	Elapsed = 0.0f;

	FVector Loc;
	FRotator Rot;
	EvaluateCamera(Loc, Rot);
	SetActorLocationAndRotation(Loc, Rot);

	// ブレンド 0 = 即カット。レースシーン移行時の初期ビューがそのまま演出カメラになる。
	PC->SetViewTargetWithBlend(this, 0.0f);

	bActive = true;
}

// =====================================================================
// グリッド基準（コース前方・右ベクトル、主要馬位置）の算出
// =====================================================================
bool AIntroCameraDirector::ComputeGridReference()
{
	TArray<AActor*> FoundHorses;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AHorseCharacter::StaticClass(), FoundHorses);
	if (FoundHorses.Num() == 0)
	{
		return false;
	}

	// コース前方ベクトル: スプライン始点の向き（無ければ先頭馬の前方）
	USplineComponent* Spline = Track ? Track->GetSpline() : nullptr;
	if (Spline)
	{
		TrackForward = Spline->GetDirectionAtDistanceAlongSpline(0.0f, ESplineCoordinateSpace::World);
	}
	else
	{
		TrackForward = FoundHorses[0]->GetActorForwardVector();
	}
	TrackForward.Z = 0.0f;
	if (!TrackForward.Normalize())
	{
		TrackForward = FVector::ForwardVector;
	}
	// 右ベクトル = Up × Forward（UE 左手系で前方の右側）
	TrackRight = FVector::CrossProduct(FVector::UpVector, TrackForward).GetSafeNormal();

	// グリッド中心
	GridCenter = FVector::ZeroVector;
	for (AActor* A : FoundHorses)
	{
		GridCenter += A->GetActorLocation();
	}
	GridCenter /= static_cast<float>(FoundHorses.Num());

	// 前方/右への射影で 先頭/最後尾・右/左 を判定
	float MaxFwd = -TNumericLimits<float>::Max(), MinFwd = TNumericLimits<float>::Max();
	float MaxRight = -TNumericLimits<float>::Max(), MinRight = TNumericLimits<float>::Max();
	LeadHorseLoc = LastHorseLoc = RightHorseLoc = LeftHorseLoc = GridCenter;
	for (AActor* A : FoundHorses)
	{
		const FVector Rel = A->GetActorLocation() - GridCenter;
		const float Fwd = FVector::DotProduct(Rel, TrackForward);
		const float Rgt = FVector::DotProduct(Rel, TrackRight);
		if (Fwd > MaxFwd) { MaxFwd = Fwd; LeadHorseLoc = A->GetActorLocation(); }
		if (Fwd < MinFwd) { MinFwd = Fwd; LastHorseLoc = A->GetActorLocation(); }
		if (Rgt > MaxRight) { MaxRight = Rgt; RightHorseLoc = A->GetActorLocation(); }
		if (Rgt < MinRight) { MinRight = Rgt; LeftHorseLoc = A->GetActorLocation(); }
	}

	return true;
}

// =====================================================================
// カット列ヘルパ
// =====================================================================
int32 AIntroCameraDirector::NumShots() const
{
	return Track ? Track->GetIntroCameraShots().Num() : 0;
}

float AIntroCameraDirector::CurrentShotDuration() const
{
	if (!Track) { return 2.5f; }
	const TArray<ECameraIntroShot>& Shots = Track->GetIntroCameraShots();
	if (!Shots.IsValidIndex(ShotIndex)) { return 2.5f; }

	// 側面カットは側面パン秒、正面カットは正面移動秒
	return (Shots[ShotIndex] == ECameraIntroShot::Front)
		? Track->GetIntroFrontMoveDuration()
		: Track->GetIntroSidePanDuration();
}

// =====================================================================
// 側面パン（最後尾→先頭）
// =====================================================================
void AIntroCameraDirector::EvalSidePan(float InSideSign, float Alpha, FVector& OutLoc, FVector& OutLook) const
{
	const float SideDist = Track ? Track->GetIntroSideDistance() : 700.0f;
	const float Height = Track ? Track->GetIntroCameraHeight() : 200.0f;
	const float LookH = Track ? Track->GetIntroLookHeight() : 100.0f;

	const FVector Base = FMath::Lerp(LastHorseLoc, LeadHorseLoc, Alpha);
	OutLoc = Base + TrackRight * (InSideSign * SideDist) + FVector::UpVector * Height;
	OutLook = Base + FVector::UpVector * LookH;
}

// =====================================================================
// 正面移動（水平 or 引き）
// =====================================================================
void AIntroCameraDirector::EvalFront(float Alpha, FVector& OutLoc, FVector& OutLook) const
{
	const float FrontDist = Track ? Track->GetIntroFrontDistance() : 600.0f;
	const float Height = Track ? Track->GetIntroCameraHeight() : 200.0f;
	const float LookH = Track ? Track->GetIntroLookHeight() : 100.0f;
	const ECameraIntroFrontMove Move = Track ? Track->GetIntroFrontMove() : ECameraIntroFrontMove::HorizontalRightToLeft;

	if (Move == ECameraIntroFrontMove::PullBackFromRight)
	{
		// 右の馬付近から、グリッド中心へ寄りつつ後方へ引いていく
		const float PullBack = Track ? Track->GetIntroPullBackDistance() : 800.0f;
		const FVector Look = FMath::Lerp(RightHorseLoc, GridCenter, Alpha);
		const float Dist = FrontDist + PullBack * Alpha;
		OutLoc = Look + TrackForward * Dist + FVector::UpVector * Height;
		OutLook = Look + FVector::UpVector * LookH;
	}
	else
	{
		// 右の馬 → 左の馬へ水平移動（カメラはグリッド前方に固定距離）
		const FVector Base = FMath::Lerp(RightHorseLoc, LeftHorseLoc, Alpha);
		OutLoc = Base + TrackForward * FrontDist + FVector::UpVector * Height;
		OutLook = Base + FVector::UpVector * LookH;
	}
}

// =====================================================================
// 現在の Elapsed/ShotIndex からカメラ Transform を評価
// =====================================================================
void AIntroCameraDirector::EvaluateCamera(FVector& OutLocation, FRotator& OutRotation) const
{
	const float Dur = CurrentShotDuration();
	const float Alpha = FMath::Clamp(Elapsed / FMath::Max(0.01f, Dur), 0.0f, 1.0f);

	// 現在のカット種別
	ECameraIntroShot Shot = ECameraIntroShot::Front;
	if (Track)
	{
		const TArray<ECameraIntroShot>& Shots = Track->GetIntroCameraShots();
		if (Shots.IsValidIndex(ShotIndex)) { Shot = Shots[ShotIndex]; }
	}

	FVector Loc, Look;
	switch (Shot)
	{
	case ECameraIntroShot::LeftSide:
		EvalSidePan(-1.0f, Alpha, Loc, Look);
		break;
	case ECameraIntroShot::RightSide:
		EvalSidePan(1.0f, Alpha, Loc, Look);
		break;
	case ECameraIntroShot::Front:
	default:
		EvalFront(Alpha, Loc, Look);
		break;
	}

	OutLocation = Loc;
	OutRotation = (Look - Loc).Rotation();
}

// =====================================================================
// Tick — 演出の進行とフェーズ遷移
// =====================================================================
void AIntroCameraDirector::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bActive) { return; }

	Elapsed += DeltaTime;

	FVector Loc;
	FRotator Rot;
	EvaluateCamera(Loc, Rot);
	SetActorLocationAndRotation(Loc, Rot);

	if (Elapsed >= CurrentShotDuration())
	{
		// 次のカットへ。位置を直接更新するだけなのでブレンドせず即カットになる。
		if (ShotIndex + 1 < NumShots())
		{
			++ShotIndex;
			Elapsed = 0.0f;
		}
		else
		{
			FinishIntro();
		}
	}
}

// =====================================================================
// 演出終了 → ビューをプレイヤー馬へ戻しカウントダウン開始
// =====================================================================
void AIntroCameraDirector::FinishIntro()
{
	bActive = false;

	// 演出終了。ブレンドせず即座にプレイヤー馬のカメラへカットする。
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (APawn* PlayerPawn = PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(PlayerPawn, 0.0f);
		}
	}

	if (Manager)
	{
		Manager->StartCountdown();
	}

	// ビュー切替後に破棄
	SetLifeSpan(0.5f);
}
