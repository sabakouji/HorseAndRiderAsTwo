#include "ResultCameraDirector.h"
#include "Camera/CameraComponent.h"
#include "HorseCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

AResultCameraDirector::AResultCameraDirector()
{
	PrimaryActorTick.bCanEverTick = true;

	ResultCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ResultCamera"));
	SetRootComponent(ResultCamera);
	// アスペクト比固定による黒帯を避ける。
	ResultCamera->bConstrainAspectRatio = false;
}

bool AResultCameraDirector::ComputeDesiredTransform(FVector& OutLocation, FRotator& OutRotation) const
{
	if (!TargetHorse)
	{
		return false;
	}

	const FVector HorseLoc = TargetHorse->GetActorLocation();
	const FVector Forward = TargetHorse->GetActorForwardVector();
	const FVector Right = TargetHorse->GetActorRightVector();
	const FVector Up = FVector::UpVector;

	// 注視点＝馬の顔（前方・上方にオフセットした点）。
	const FVector FacePoint = HorseLoc + Forward * FaceForward + Up * FaceHeight;

	// カメラ位置＝馬の前方・左右・上方へオフセット（CameraSide が負で左斜め）。
	OutLocation = HorseLoc + Forward * CameraFront + Right * CameraSide + Up * CameraHeight;

	// 顔の方を向く。
	OutRotation = (FacePoint - OutLocation).Rotation();
	return true;
}

void AResultCameraDirector::StartResultCamera(AHorseCharacter* Target)
{
	if (!Target)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Result] 追従対象の馬が指定されていません。"));
		return;
	}

	TargetHorse = Target;

	// 始点へ即配置してからブレンド開始（ブレンドが移動・回転をシームレスにつなぐ）。
	FVector DesiredLoc;
	FRotator DesiredRot;
	if (ComputeDesiredTransform(DesiredLoc, DesiredRot))
	{
		SetActorLocationAndRotation(DesiredLoc, DesiredRot);
	}

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->SetViewTargetWithBlend(this, BlendTime, EViewTargetBlendFunction::VTBlend_Cubic, 2.0f);
	}

	bActive = true;
}

void AResultCameraDirector::StopResultCamera()
{
	bActive = false;
}

void AResultCameraDirector::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bActive || !TargetHorse)
	{
		return;
	}

	FVector DesiredLoc;
	FRotator DesiredRot;
	if (!ComputeDesiredTransform(DesiredLoc, DesiredRot))
	{
		return;
	}

	// 位置・向きとも滑らかに追従（馬が走り続けても画が揺れすぎないよう補間）。
	const FVector NewLoc = FMath::VInterpTo(GetActorLocation(), DesiredLoc, DeltaTime, FollowSpeed);
	const FRotator NewRot = FMath::RInterpTo(GetActorRotation(), DesiredRot, DeltaTime, FollowSpeed);
	SetActorLocationAndRotation(NewLoc, NewRot);
}
