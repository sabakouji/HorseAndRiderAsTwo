#include "TrackActor.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "GoalTrigger.h"
#include "CheckpointActor.h"

// =====================================================================
// コンストラクタ
// =====================================================================
ATrackActor::ATrackActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	RootComponent = Spline;

	// デフォルトでループしないコース
	Spline->SetClosedLoop(false);
}

// =====================================================================
// OnConstruction — Spline 編集時に SplineMesh を再構築
// =====================================================================
void ATrackActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SyncPointWidthsToSpline();
	ClearSplineMeshes();
	BuildSplineMeshes();

	ClearAutoSpawnedTriggers();
	if (bAutoSpawnGoalAndCheckpoints)
	{
		BuildAutoSpawnedTriggers();
	}
}

// =====================================================================
// PointWidths を Spline ポイント数に同期
//   既存要素は保持し、不足分のみ DefaultWidth で初期化する
// =====================================================================
void ATrackActor::SyncPointWidthsToSpline()
{
	if (!Spline) { return; }

	const int32 NumPoints = Spline->GetNumberOfSplinePoints();
	const int32 PrevNum = PointWidths.Num();

	if (PrevNum < NumPoints)
	{
		// 不足分を末尾に追加し、新規要素のみ DefaultWidth で埋める
		PointWidths.SetNum(NumPoints);
		for (int32 i = PrevNum; i < NumPoints; ++i)
		{
			PointWidths[i] = DefaultWidth;
		}
	}
	else if (PrevNum > NumPoints)
	{
		// ポイント削減時は末尾を切り詰める（先頭側の値は保持）
		PointWidths.SetNum(NumPoints);
	}
}

// =====================================================================
// 指定スプライン距離における許容幅を線形補間で取得
// =====================================================================
float ATrackActor::GetWidthAtDistance(float Distance) const
{
	// Spline 未設定 / PointWidths 未設定時は既定値
	if (!Spline || PointWidths.Num() == 0)
	{
		return DefaultWidth;
	}

	const int32 NumPoints = Spline->GetNumberOfSplinePoints();

	// 同期前など要素数不一致時は安全側として既定値を返す
	if (PointWidths.Num() != NumPoints || NumPoints < 1)
	{
		return DefaultWidth;
	}

	// ポイントが 1 つのみなら補間不要
	if (NumPoints == 1)
	{
		return PointWidths[0];
	}

	const float SplineLen = Spline->GetSplineLength();
	if (SplineLen <= KINDA_SMALL_NUMBER)
	{
		return PointWidths[0];
	}

	const bool bClosed = Spline->IsClosedLoop();

	// Distance を有効範囲に正規化
	if (bClosed)
	{
		Distance = FMath::Fmod(Distance, SplineLen);
		if (Distance < 0.0f)
		{
			Distance += SplineLen;
		}
	}
	else
	{
		Distance = FMath::Clamp(Distance, 0.0f, SplineLen);
	}

	// ループ時は最終ポイント→最初のポイント区間も対象に含める
	const int32 NumSegments = bClosed ? NumPoints : NumPoints - 1;

	for (int32 i = 0; i < NumSegments; ++i)
	{
		const float D0 = Spline->GetDistanceAlongSplineAtSplinePoint(i);
		const bool bLastClosedSeg = (bClosed && i == NumPoints - 1);
		const float D1 = bLastClosedSeg
			? SplineLen
			: Spline->GetDistanceAlongSplineAtSplinePoint(i + 1);

		if (Distance >= D0 && Distance <= D1)
		{
			const float Alpha = (Distance - D0) / FMath::Max(D1 - D0, KINDA_SMALL_NUMBER);
			const int32 EndIndex = bLastClosedSeg ? 0 : i + 1;
			return FMath::Lerp(PointWidths[i], PointWidths[EndIndex], Alpha);
		}
	}

	// 数値誤差等でセグメントに収まらなかった場合は末尾値を返す
	return PointWidths.Last();
}

// =====================================================================
// スプラインの全長
// =====================================================================
float ATrackActor::GetTrackLength() const
{
	return Spline ? Spline->GetSplineLength() : 0.0f;
}

// =====================================================================
// ミニマップ形状供給 API（正規化2D点列）
// =====================================================================

void ATrackActor::ComputeTrackBounds2D(FVector2D& OutMin, FVector2D& OutSize) const
{
	// フォールバック既定値（Spline 未設定でも 0 除算しない安全値）
	OutMin = FVector2D::ZeroVector;
	OutSize = FVector2D(1.0f, 1.0f);

	if (!Spline) { return; }

	const float Length = Spline->GetSplineLength();
	if (Length <= KINDA_SMALL_NUMBER) { return; }

	// 固定サンプル数でスプライン形状をなぞり、ワールド XY の min/max を求める。
	// ClosedLoop は終点が始点へ戻るため最終点を含めず、Open は端点を含める。
	const int32 kBoundsSamples = 64;
	const bool bClosed = Spline->IsClosedLoop();

	FVector2D Min(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
	FVector2D Max(TNumericLimits<float>::Lowest(), TNumericLimits<float>::Lowest());

	for (int32 i = 0; i < kBoundsSamples; ++i)
	{
		float Dist = 0.0f;
		if (bClosed)
		{
			// 0 〜 (N-1)/N * Length（始点復帰の最終点は含めない）
			Dist = (static_cast<float>(i) / static_cast<float>(kBoundsSamples)) * Length;
		}
		else
		{
			// 0 〜 Length を (N-1) 区間で等分（端点を含む）
			Dist = (static_cast<float>(i) / static_cast<float>(kBoundsSamples - 1)) * Length;
		}

		const FVector World = Spline->GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World);
		Min.X = FMath::Min(Min.X, static_cast<float>(World.X));
		Min.Y = FMath::Min(Min.Y, static_cast<float>(World.Y));
		Max.X = FMath::Max(Max.X, static_cast<float>(World.X));
		Max.Y = FMath::Max(Max.Y, static_cast<float>(World.Y));
	}

	const float SpanX = Max.X - Min.X;
	const float SpanY = Max.Y - Min.Y;

	// 長辺を両軸共通の正規化分母とすることでアスペクト比を維持する（ゼロガード）。
	const float LongSide = FMath::Max3(SpanX, SpanY, KINDA_SMALL_NUMBER);

	// 短辺方向の中央寄せ: 原点を短辺側へ (LongSide - 短辺) * 0.5 だけ手前にずらすことで、
	// 正規化後に短辺方向のコンテンツが [0,1] の中央に配置される。
	OutMin.X = Min.X - (LongSide - SpanX) * 0.5f;
	OutMin.Y = Min.Y - (LongSide - SpanY) * 0.5f;
	OutSize = FVector2D(LongSide, LongSide);
}

FVector2D ATrackActor::WorldToNormalizedMiniMap(const FVector& WorldLocation) const
{
	FVector2D Min, Size;
	ComputeTrackBounds2D(Min, Size); // Size は長辺（両軸同値）

	const float Nx = (static_cast<float>(WorldLocation.X) - Min.X) / Size.X;
	const float Ny = (static_cast<float>(WorldLocation.Y) - Min.Y) / Size.Y;
	return FVector2D(FMath::Clamp(Nx, 0.0f, 1.0f), FMath::Clamp(Ny, 0.0f, 1.0f));
}

TArray<FVector2D> ATrackActor::GetTrackShape2D(int32 NumSamples) const
{
	TArray<FVector2D> Out;
	if (!Spline) { return Out; }

	const float Length = Spline->GetSplineLength();
	if (Length <= KINDA_SMALL_NUMBER) { return Out; }

	// 最低 2 点を保証する。
	NumSamples = FMath::Max(NumSamples, 2);

	// WorldToNormalizedMiniMap と同一基準（ComputeTrackBounds2D）を共有して座標系を完全一致させる。
	FVector2D Min, Size;
	ComputeTrackBounds2D(Min, Size);

	const bool bClosed = Spline->IsClosedLoop();
	Out.Reserve(NumSamples);

	for (int32 i = 0; i < NumSamples; ++i)
	{
		float Dist = 0.0f;
		if (bClosed)
		{
			// ClosedLoop: 0 〜 (N-1)/N * Length（始点へ戻る最終点は描画側で線を閉じるため含めない）
			Dist = (static_cast<float>(i) / static_cast<float>(NumSamples)) * Length;
		}
		else
		{
			// Open: 0 〜 Length を (N-1) 区間で等分し終点を含める
			Dist = (static_cast<float>(i) / static_cast<float>(NumSamples - 1)) * Length;
		}

		const FVector World = Spline->GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World);
		// WorldToNormalizedMiniMap と同一の正規化式を用いる。
		const float Nx = (static_cast<float>(World.X) - Min.X) / Size.X;
		const float Ny = (static_cast<float>(World.Y) - Min.Y) / Size.Y;
		Out.Add(FVector2D(FMath::Clamp(Nx, 0.0f, 1.0f), FMath::Clamp(Ny, 0.0f, 1.0f)));
	}

	return Out;
}

float ATrackActor::GetNormalizedTrackWidth(int32 NumSamples) const
{
	// Spline 未設定時は破綻させず 0 を返す。
	if (!Spline) { return 0.0f; }

	// スプライン長 0（ポイント未設定等）でもゼロ除算しないようガードする。
	const float Length = Spline->GetSplineLength();
	if (Length <= KINDA_SMALL_NUMBER) { return 0.0f; }

	// 最低 1 サンプルを保証する（平均の分母のゼロ除算回避）。
	NumSamples = FMath::Max(NumSamples, 1);

	// NumSamples 点で中心からの片側半幅(cm)を取得し平均する。
	float SumHalfWidth = 0.0f;
	for (int32 i = 0; i < NumSamples; ++i)
	{
		const float Dist = (static_cast<float>(i) / static_cast<float>(NumSamples)) * Length;
		SumHalfWidth += GetWidthAtDistance(Dist); // 中心からの片側半幅(cm)
	}
	const float AvgHalfWidth = SumHalfWidth / static_cast<float>(NumSamples);

	// コース全幅(cm) = 平均半幅 × 2。
	const float FullWidth = AvgHalfWidth * 2.0f;

	// GetTrackShape2D / WorldToNormalizedMiniMap と同一の長辺 Size.X を分母にして
	// ミニマップ座標系へ正規化する（線とコース形状で同一スケールを保証）。
	FVector2D Min, Size;
	ComputeTrackBounds2D(Min, Size); // Size.X = 長辺(cm、両軸同値)
	if (Size.X <= KINDA_SMALL_NUMBER) { return 0.0f; } // 0除算ガード

	const float Normalized = FullWidth / Size.X;
	// 太さなので 1.0 を超えてもよいが、負値だけは防ぐ。
	return FMath::Max(Normalized, 0.0f);
}

// =====================================================================
// 既存 SplineMesh の破棄
// =====================================================================
void ATrackActor::ClearSplineMeshes()
{
	for (USplineMeshComponent* SM : SplineMeshes)
	{
		if (SM)
		{
			SM->DestroyComponent();
		}
	}
	SplineMeshes.Empty();
}

// =====================================================================
// SplineMesh の生成（各 Spline ポイント間に1本ずつ配置）
// =====================================================================
void ATrackActor::BuildSplineMeshes()
{
	if (!Spline || !RoadMesh) { return; }

	const int32 NumPoints = Spline->GetNumberOfSplinePoints();
	if (NumPoints < 2) { return; }

	// ループ時は最終ポイント→最初のポイントも繋ぐ
	const int32 NumSegments = Spline->IsClosedLoop() ? NumPoints : NumPoints - 1;

	for (int32 i = 0; i < NumSegments; ++i)
	{
		const int32 NextIndex = (i + 1) % NumPoints;

		USplineMeshComponent* SM = NewObject<USplineMeshComponent>(this, USplineMeshComponent::StaticClass());
		if (!SM) { continue; }

		SM->SetMobility(EComponentMobility::Movable);
		SM->CreationMethod = EComponentCreationMethod::UserConstructionScript;
		SM->RegisterComponentWithWorld(GetWorld());
		SM->AttachToComponent(Spline, FAttachmentTransformRules::KeepRelativeTransform);

		SM->SetStaticMesh(RoadMesh);
		SM->SetForwardAxis(ForwardAxis);
		SM->SetStartScale(MeshScale);
		SM->SetEndScale(MeshScale);

		// コリジョン設定: プロファイル適用は CollisionEnabled を上書きするため、
		// プロファイル名を先に適用してから明示的に有効/無効を設定する。
		if (bGenerateCollision)
		{
			SM->SetCollisionProfileName(CollisionProfileName);
			SM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
		else
		{
			SM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}

		const FVector StartPos = Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
		const FVector StartTan = Spline->GetTangentAtSplinePoint(i, ESplineCoordinateSpace::Local);
		const FVector EndPos   = Spline->GetLocationAtSplinePoint(NextIndex, ESplineCoordinateSpace::Local);
		const FVector EndTan   = Spline->GetTangentAtSplinePoint(NextIndex, ESplineCoordinateSpace::Local);

		SM->SetStartAndEnd(StartPos, StartTan, EndPos, EndTan, true);

		SplineMeshes.Add(SM);
	}
}

// =====================================================================
// Goal / Checkpoint 自動配置: 既存子アクターコンポーネントを破棄
// =====================================================================
void ATrackActor::ClearAutoSpawnedTriggers()
{
	if (SpawnedGoalChild)
	{
		SpawnedGoalChild->DestroyComponent();
		SpawnedGoalChild = nullptr;
	}

	for (UChildActorComponent* C : SpawnedCheckpointChildren)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	SpawnedCheckpointChildren.Empty();
}

// =====================================================================
// Goal / Checkpoint 自動配置: スプライン上に子アクターを生成
// =====================================================================
void ATrackActor::BuildAutoSpawnedTriggers()
{
	if (!Spline) { return; }

	const float SplineLen = Spline->GetSplineLength();
	if (SplineLen <= KINDA_SMALL_NUMBER) { return; }

	const TSubclassOf<AGoalTrigger> GoalClass =
		GoalTriggerClass ? GoalTriggerClass : TSubclassOf<AGoalTrigger>(AGoalTrigger::StaticClass());
	const TSubclassOf<ACheckpointActor> CPClass =
		CheckpointClass ? CheckpointClass : TSubclassOf<ACheckpointActor>(ACheckpointActor::StaticClass());

	auto LocateOnSpline = [this, SplineLen](float Distance, FVector& OutLoc, FRotator& OutRot)
	{
		const float Clamped = FMath::Clamp(Distance, 0.0f, SplineLen);
		OutLoc = Spline->GetLocationAtDistanceAlongSpline(Clamped, ESplineCoordinateSpace::World);
		OutRot = Spline->GetRotationAtDistanceAlongSpline(Clamped, ESplineCoordinateSpace::World);
	};

	// --- Goal ---
	{
		UChildActorComponent* Comp = NewObject<UChildActorComponent>(this, UChildActorComponent::StaticClass(),
			TEXT("AutoSpawnedGoal"));
		if (Comp)
		{
			Comp->CreationMethod = EComponentCreationMethod::UserConstructionScript;
			Comp->SetupAttachment(Spline);
			Comp->SetChildActorClass(GoalClass);
			Comp->RegisterComponentWithWorld(GetWorld());

			FVector Loc; FRotator Rot;
			LocateOnSpline(GoalDistance, Loc, Rot);
			Comp->SetWorldLocationAndRotation(Loc, Rot);

			SpawnedGoalChild = Comp;
		}
	}

	// --- Checkpoints（距離順にソートして CheckpointIndex を採番）---
	TArray<float> SortedDistances = CheckpointDistances;
	SortedDistances.Sort();

	for (int32 i = 0; i < SortedDistances.Num(); ++i)
	{
		const float D = SortedDistances[i];

		UChildActorComponent* Comp = NewObject<UChildActorComponent>(this, UChildActorComponent::StaticClass(),
			*FString::Printf(TEXT("AutoSpawnedCheckpoint_%d"), i));
		if (!Comp) { continue; }

		Comp->CreationMethod = EComponentCreationMethod::UserConstructionScript;
		Comp->SetupAttachment(Spline);
		Comp->SetChildActorClass(CPClass);
		Comp->RegisterComponentWithWorld(GetWorld());

		FVector Loc; FRotator Rot;
		LocateOnSpline(D, Loc, Rot);
		Comp->SetWorldLocationAndRotation(Loc, Rot);

		if (ACheckpointActor* CP = Cast<ACheckpointActor>(Comp->GetChildActor()))
		{
			CP->CheckpointIndex = i;
		}

		SpawnedCheckpointChildren.Add(Comp);
	}

	// 生成結果をログ出力（PIE 開始時に距離配置を確認できるようにする）
	if (GEngine && GetWorld() && GetWorld()->IsGameWorld())
	{
		FString DistStr;
		for (int32 i = 0; i < SortedDistances.Num(); ++i)
		{
			DistStr += FString::Printf(TEXT("CP%d=%.0f "), i, SortedDistances[i]);
		}
		GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Turquoise,
			FString::Printf(TEXT("[Track] Spawned Goal@%.0f, %d Checkpoints: %s (SplineLen=%.0f)"),
				FMath::Clamp(GoalDistance, 0.0f, SplineLen),
				SortedDistances.Num(), *DistStr, SplineLen));
	}
}

// =====================================================================
// 公開アクセサ
// =====================================================================
AGoalTrigger* ATrackActor::GetSpawnedGoal() const
{
	if (SpawnedGoalChild)
	{
		return Cast<AGoalTrigger>(SpawnedGoalChild->GetChildActor());
	}
	return nullptr;
}

TArray<ACheckpointActor*> ATrackActor::GetSpawnedCheckpoints() const
{
	TArray<ACheckpointActor*> Out;
	Out.Reserve(SpawnedCheckpointChildren.Num());
	for (UChildActorComponent* C : SpawnedCheckpointChildren)
	{
		if (!C) { continue; }
		if (ACheckpointActor* CP = Cast<ACheckpointActor>(C->GetChildActor()))
		{
			Out.Add(CP);
		}
	}
	return Out;
}
