#include "GhostRecorder.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

UGhostRecorder::UGhostRecorder()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UGhostRecorder::StartRecord()
{
	Frames.Reset();
	bRecording = true;

	const UWorld* World = GetWorld();
	RecordStartTime = World ? World->GetTimeSeconds() : 0.0f;
	TimeSinceLastSample = 0.0f;

	// 開始サンプルを 1 つ即追加（TimeStamp=0, 現在の位置・回転）。再生の始点を確定するため。
	if (const AActor* Owner = GetOwner())
	{
		FGhostFrame StartFrame;
		StartFrame.TimeStamp = 0.0f;
		StartFrame.Location = Owner->GetActorLocation();
		StartFrame.Rotation = Owner->GetActorRotation();
		Frames.Add(StartFrame);
	}
}

void UGhostRecorder::StopRecord()
{
	bRecording = false;
}

void UGhostRecorder::ClearFrames()
{
	Frames.Reset();
}

void UGhostRecorder::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bRecording) { return; }

	TimeSinceLastSample += DeltaTime;

	// SampleInterval を極小に設定された場合のフレーム肥大を避けるため、
	// 1 Tick あたりの追加上限を設けて残差は切り捨てる。
	constexpr int32 MaxSamplesPerTick = 4;
	int32 SamplesAddedThisTick = 0;

	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		// Owner/World 不在時は累積だけ吸収して終了
		while (TimeSinceLastSample >= SampleInterval && SamplesAddedThisTick < MaxSamplesPerTick)
		{
			TimeSinceLastSample -= SampleInterval;
			++SamplesAddedThisTick;
		}
		return;
	}

	while (TimeSinceLastSample >= SampleInterval && SamplesAddedThisTick < MaxSamplesPerTick)
	{
		TimeSinceLastSample -= SampleInterval;

		FGhostFrame Frame;
		Frame.TimeStamp = World->GetTimeSeconds() - RecordStartTime;
		Frame.Location = Owner->GetActorLocation();
		Frame.Rotation = Owner->GetActorRotation();
		Frames.Add(Frame);

		++SamplesAddedThisTick;
	}
}
