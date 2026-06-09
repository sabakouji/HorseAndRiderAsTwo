#include "GhostPlayer.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"

AGhostPlayer::AGhostPlayer()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	GhostMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GhostMesh"));
	GhostMesh->SetupAttachment(RootComponent);
	// ゴーストはプレイヤー馬の物理に干渉しない見た目専用。
	GhostMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AGhostPlayer::PlayFrames(const TArray<FGhostFrame>& InFrames)
{
	Frames = InFrames;
	PlaybackTime = 0.0f;
	LastFrameIndex = 0;

	if (Frames.Num() < 2)
	{
		bPlaying = false;
		if (Frames.Num() == 1)
		{
			SetActorLocationAndRotation(Frames[0].Location, Frames[0].Rotation,
				false, nullptr, ETeleportType::TeleportPhysics);
		}
		return;
	}

	// 先頭フレームへ瞬間移動して再生開始
	SetActorLocationAndRotation(Frames[0].Location, Frames[0].Rotation,
		false, nullptr, ETeleportType::TeleportPhysics);
	bPlaying = true;
}

void AGhostPlayer::StopPlayback()
{
	bPlaying = false;
}

void AGhostPlayer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bPlaying) { return; }
	if (Frames.Num() < 2) { bPlaying = false; return; }

	PlaybackTime += DeltaTime;

	const float Duration = Frames.Last().TimeStamp;
	if (Duration <= 0.0f)
	{
		bPlaying = false;
		return;
	}

	if (PlaybackTime > Duration)
	{
		if (bLoop)
		{
			PlaybackTime = FMath::Fmod(PlaybackTime, Duration);
			LastFrameIndex = 0;
		}
		else
		{
			PlaybackTime = Duration;
			ApplyInterpolatedFrame();
			bPlaying = false;
			return;
		}
	}

	ApplyInterpolatedFrame();
}

void AGhostPlayer::ApplyInterpolatedFrame()
{
	const int32 NumFrames = Frames.Num();
	if (NumFrames < 2) { return; }

	// LastFrameIndex から前進探索する。ループでリセット済み or 後退が必要なら 0 から探す。
	if (LastFrameIndex < 0 || LastFrameIndex >= NumFrames - 1
		|| Frames[LastFrameIndex].TimeStamp > PlaybackTime)
	{
		LastFrameIndex = 0;
	}

	while (LastFrameIndex < NumFrames - 2
		&& Frames[LastFrameIndex + 1].TimeStamp <= PlaybackTime)
	{
		++LastFrameIndex;
	}

	const FGhostFrame& A = Frames[LastFrameIndex];
	const FGhostFrame& B = Frames[LastFrameIndex + 1];

	const float Denom = B.TimeStamp - A.TimeStamp;
	float Alpha = 0.0f;
	if (Denom > KINDA_SMALL_NUMBER)
	{
		Alpha = FMath::Clamp((PlaybackTime - A.TimeStamp) / Denom, 0.0f, 1.0f);
	}

	const FVector Location = FMath::Lerp(A.Location, B.Location, Alpha);
	const FRotator Rotation = FQuat::Slerp(A.Rotation.Quaternion(), B.Rotation.Quaternion(), Alpha).Rotator();

	SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
}
