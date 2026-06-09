#include "StartGridSpawner.h"
#include "TrackActor.h"
#include "HorseCharacter.h"
#include "Components/SplineComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

// =====================================================================
// コンストラクタ
// =====================================================================
AStartGridSpawner::AStartGridSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
}

// =====================================================================
// BeginPlay
// =====================================================================
void AStartGridSpawner::BeginPlay()
{
	Super::BeginPlay();

	ResolveTrack();
	SpawnGrid();
}

// =====================================================================
// Track 自動取得
// =====================================================================
void AStartGridSpawner::ResolveTrack()
{
	if (Track) { return; }

	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATrackActor::StaticClass(), Found);
	if (Found.Num() > 0)
	{
		Track = Cast<ATrackActor>(Found[0]);
	}
}

// =====================================================================
// グリッド配置
// =====================================================================
void AStartGridSpawner::SpawnGrid()
{
	if (!Track)
	{
		ResolveTrack();
	}
	if (!Track) { return; }

	USplineComponent* Spline = Track->GetSpline();
	if (!Spline) { return; }

	const float SplineLength = Spline->GetSplineLength();
	if (SplineLength <= 0.0f) { return; }

	// 既存馬の取得（再配置モード時に i 番目を順番に流し込む）
	TArray<AHorseCharacter*> ExistingHorses;
	if (bRespawnExisting)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AHorseCharacter::StaticClass(), Found);
		ExistingHorses.Reserve(Found.Num());
		for (AActor* A : Found)
		{
			if (AHorseCharacter* H = Cast<AHorseCharacter>(A))
			{
				ExistingHorses.Add(H);
			}
		}
	}

	UWorld* World = GetWorld();
	if (!World) { return; }

	// トレースで無視するアクター一覧（全既存馬 + スポーナ自身）。
	// 馬同士の上に着地する誤判定を防ぐため、配置対象以外の馬も無視する。
	TArray<AActor*> IgnoredActors;
	IgnoredActors.Add(const_cast<AStartGridSpawner*>(this));
	for (AHorseCharacter* H : ExistingHorses)
	{
		if (H) { IgnoredActors.Add(H); }
	}

	// 新規スポーンモードのカプセル半高は CDO から取得（全馬共通前提）
	float NewSpawnCapsuleHalfHeight = 0.0f;
	if (!bRespawnExisting && HorseClass)
	{
		if (const AHorseCharacter* CDO = HorseClass->GetDefaultObject<AHorseCharacter>())
		{
			if (const UCapsuleComponent* Capsule = CDO->GetCapsuleComponent())
			{
				NewSpawnCapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
			}
		}
	}

	for (int32 i = 0; i < GridCount; ++i)
	{
		// スプライン上の距離（短距離トラックでは末尾を超えないようクランプ）
		const float RawDistance = static_cast<float>(i) * GridSpacing;
		const float Distance = FMath::Clamp(RawDistance, 0.0f, FMath::Max(0.0f, SplineLength - 1.0f));

		const FVector Origin = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		FVector Tangent = Spline->GetTangentAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		if (!Tangent.Normalize())
		{
			Tangent = FVector::ForwardVector;
		}

		// 左右ベクトルは UpVector × Tangent
		FVector RightVec = FVector::CrossProduct(FVector::UpVector, Tangent);
		if (!RightVec.Normalize())
		{
			RightVec = FVector::RightVector;
		}

		const float LateralSign = ((i % 2) == 0) ? +1.0f : -1.0f;
		const FVector SpawnLocXY = Origin + RightVec * LateralSign * LateralOffset;
		const FRotator SpawnRot = Tangent.Rotation();

		if (bRespawnExisting)
		{
			if (i < ExistingHorses.Num() && ExistingHorses[i])
			{
				AHorseCharacter* Horse = ExistingHorses[i];

				// カプセル半高を既存馬から取得
				float HalfHeight = 0.0f;
				if (const UCapsuleComponent* Capsule = Horse->GetCapsuleComponent())
				{
					HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
				}

				const FVector FinalLoc = ResolveSpawnLocationOnGround(SpawnLocXY, HalfHeight, IgnoredActors);
				Horse->SetActorLocationAndRotation(FinalLoc, SpawnRot);
			}
			// 既存馬が足りない場合はスキップ（残り i はそのまま終了）
		}
		else
		{
			if (HorseClass)
			{
				const FVector FinalLoc = ResolveSpawnLocationOnGround(SpawnLocXY, NewSpawnCapsuleHalfHeight, IgnoredActors);

				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
				World->SpawnActor<AHorseCharacter>(HorseClass, FinalLoc, SpawnRot, Params);
			}
		}
	}
}

// =====================================================================
// スプライン位置を路面に着地させた最終位置に変換
// =====================================================================
FVector AStartGridSpawner::ResolveSpawnLocationOnGround(const FVector& SpawnLoc, float CapsuleHalfHeight,
	const TArray<AActor*>& IgnoredActors) const
{
	// 着地無効時はスプライン高さ + 固定オフセット
	if (!bSnapToGroundOnSpawn)
	{
		return SpawnLoc + FVector(0.0f, 0.0f, SpawnHeightOffset);
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return SpawnLoc + FVector(0.0f, 0.0f, SpawnHeightOffset);
	}

	const FVector TraceStart = SpawnLoc + FVector(0.0f, 0.0f, GroundTraceUpHeight);
	const FVector TraceEnd   = SpawnLoc - FVector(0.0f, 0.0f, GroundTraceDownDepth);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(StartGridGroundTrace), /*bTraceComplex=*/false);
	Params.AddIgnoredActors(IgnoredActors);

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params);

	if (bHit)
	{
		// カプセル下端が路面 + 余白に乗るよう中心 Z を決定
		FVector Result = SpawnLoc;
		Result.Z = Hit.ImpactPoint.Z + CapsuleHalfHeight + GroundClearanceMargin;
		return Result;
	}

	// 路面が見つからなければフォールバック
	return SpawnLoc + FVector(0.0f, 0.0f, SpawnHeightOffset);
}
