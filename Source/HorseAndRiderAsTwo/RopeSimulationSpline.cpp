#include "RopeSimulationSpline.h"

#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/BodyInstance.h"

ARopeSimulationSpline::ARopeSimulationSpline()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	SplineComp = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	SplineComp->SetupAttachment(SceneRoot);
	SplineComp->SetMobility(EComponentMobility::Movable);
}

void ARopeSimulationSpline::BeginPlay()
{
	Super::BeginPlay();
	InitializeParticlesAlongLine();
	RebuildSplineMeshes();
}

void ARopeSimulationSpline::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 初回 or セグメント数変更時のみ rest 長を再決定
	const int32 RequiredNum = FMath::Max(2, NumSegments + 1);
	if (!bRopeInitialized || Particles.Num() != RequiredNum)
	{
		InitializeParticlesAlongLine();
	}
	else
	{
		// 端点だけ新しい始点・終点に合わせる。
		// 物理プレビュー OFF でも端点位置を即時反映するため、
		// 中間粒子も新端点を結ぶ懸垂線形状に再配置する。
		FVector StartWorld, EndWorld;
		ResolveBothEndpointsWithInset(StartWorld, EndWorld);

		const FVector StartEmergeW = ResolveEmergeWorldOffset(StartActor, StartEmergeOffset);
		const FVector EndEmergeW   = ResolveEmergeWorldOffset(EndActor,   EndEmergeOffset);
		const FVector PinnedSecond = StartWorld + StartEmergeW;
		const FVector PinnedPrev   = EndWorld   + EndEmergeW;

		const float StraightDist = FVector::Dist(StartWorld, EndWorld);
		// 縄全長より直線距離が長い場合は張力で直線、短い場合は余剰分だけたわむ
		const float Sag = FMath::Max(0.0f, TotalRopeLength - StraightDist) * 0.5f;

		for (int32 i = 0; i < Particles.Num(); ++i)
		{
			const float T = static_cast<float>(i) / static_cast<float>(Particles.Num() - 1);
			FVector P = FMath::Lerp(StartWorld, EndWorld, T);
			P.Z -= Sag * 4.0f * T * (1.0f - T);
			Particles[i].Pos = P;
			Particles[i].PrevPos = P;
		}
		// emerge カーブによるピン留め
		const int32 N = Particles.Num();
		const int32 SmoothK = FMath::Clamp(EmergeSmoothCount, 0, FMath::Max(0, N / 2 - 1));
		if (SmoothK > 0)
		{
			auto NaturalAt = [&](int32 idx) {
				const float Tn = static_cast<float>(idx) / static_cast<float>(N - 1);
				FVector P = FMath::Lerp(StartWorld, EndWorld, Tn);
				P.Z -= Sag * 4.0f * Tn * (1.0f - Tn);
				return P;
			};
			PinEmergeCurve(StartWorld, StartEmergeW, NaturalAt(SmoothK + 1), 1, +1, SmoothK);
			PinEmergeCurve(EndWorld,   EndEmergeW,   NaturalAt(N - 2 - SmoothK), N - 2, -1, SmoothK);
		}
	}

	RebuildSplineMeshes();
	UpdateSplineFromParticles();
}

void ARopeSimulationSpline::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const bool bRuntime = (GetWorld() && GetWorld()->IsGameWorld());
	if (!bSimulatePhysics)
	{
		return;
	}
	if (!bRuntime && !bPreviewInEditor)
	{
		return;
	}

	StepSimulation(DeltaTime);
}

void ARopeSimulationSpline::SetEndpoints(AActor* InStartActor, FName InStartSocket,
                                        AActor* InEndActor, FName InEndSocket)
{
	StartActor = InStartActor;
	StartSocketName = InStartSocket;
	EndActor = InEndActor;
	EndSocketName = InEndSocket;

	// 初回のみ rest 長を確定。以後の SetEndpoints では長さを変えない。
	if (!bRopeInitialized || Particles.Num() != FMath::Max(2, NumSegments + 1))
	{
		InitializeParticlesAlongLine();
		RebuildSplineMeshes();
	}
	else
	{
		// 端点座標を更新し、現在の rest 長を保ったまま懸垂線形状に再配置
		FVector StartWorld, EndWorld;
		ResolveBothEndpointsWithInset(StartWorld, EndWorld);
		const float StraightDist = FVector::Dist(StartWorld, EndWorld);
		const float Sag = FMath::Max(0.0f, TotalRopeLength - StraightDist) * 0.5f;
		for (int32 i = 0; i < Particles.Num(); ++i)
		{
			const float T = static_cast<float>(i) / static_cast<float>(Particles.Num() - 1);
			FVector P = FMath::Lerp(StartWorld, EndWorld, T);
			P.Z -= Sag * 4.0f * T * (1.0f - T);
			Particles[i].Pos = P;
			Particles[i].PrevPos = P;
		}
	}
	UpdateSplineFromParticles();
}

void ARopeSimulationSpline::ResetRope()
{
	bRopeInitialized = false;
	bAnchorPrevValid = false;  // 次フレームでアンカー速度の暴発を防ぐ
	InitializeParticlesAlongLine();
	UpdateSplineFromParticles();
}

void ARopeSimulationSpline::SetEndpointActors(AActor* InStartActor, AActor* InEndActor)
{
	if (InStartActor) { StartActor = InStartActor; }
	if (InEndActor)   { EndActor   = InEndActor; }
}

void ARopeSimulationSpline::RefreshLayout()
{
	const int32 RequiredNum = FMath::Max(2, NumSegments + 1);
	if (!bRopeInitialized || Particles.Num() != RequiredNum)
	{
		InitializeParticlesAlongLine();
		RebuildSplineMeshes();
		UpdateSplineFromParticles();
		return;
	}

	// 既に初期化済みなら長さを保持したまま端点だけ追従
	FVector StartWorld, EndWorld;
	ResolveBothEndpointsWithInset(StartWorld, EndWorld);
	const FVector StartEmergeW = ResolveEmergeWorldOffset(StartActor, StartEmergeOffset);
	const FVector EndEmergeW   = ResolveEmergeWorldOffset(EndActor,   EndEmergeOffset);
	const float StraightDist = FVector::Dist(StartWorld, EndWorld);
	const float Sag = FMath::Max(0.0f, TotalRopeLength - StraightDist) * 0.5f;
	for (int32 i = 0; i < Particles.Num(); ++i)
	{
		const float T = static_cast<float>(i) / static_cast<float>(Particles.Num() - 1);
		FVector P = FMath::Lerp(StartWorld, EndWorld, T);
		P.Z -= Sag * 4.0f * T * (1.0f - T);
		Particles[i].Pos = P;
		Particles[i].PrevPos = P;
	}
	const int32 N = Particles.Num();
	const int32 SmoothK = FMath::Clamp(EmergeSmoothCount, 0, FMath::Max(0, N / 2 - 1));
	if (SmoothK > 0)
	{
		auto NaturalAt = [&](int32 idx) {
			const float Tn = static_cast<float>(idx) / static_cast<float>(N - 1);
			FVector P = FMath::Lerp(StartWorld, EndWorld, Tn);
			P.Z -= Sag * 4.0f * Tn * (1.0f - Tn);
			return P;
		};
		PinEmergeCurve(StartWorld, StartEmergeW, NaturalAt(SmoothK + 1), 1, +1, SmoothK);
		PinEmergeCurve(EndWorld,   EndEmergeW,   NaturalAt(N - 2 - SmoothK), N - 2, -1, SmoothK);
	}
	UpdateSplineFromParticles();
}

void ARopeSimulationSpline::StepSimulation(float DeltaTime)
{
	if (!bRopeInitialized || Particles.Num() < 2)
	{
		InitializeParticlesAlongLine();
	}
	if (DeltaTime <= KINDA_SMALL_NUMBER || Particles.Num() < 2)
	{
		UpdateSplineFromParticles();
		return;
	}

	// =====================================================================
	// 【設計: ワールド空間 Verlet + サブステッピング + 径方向減衰】
	//
	// 旧版で残っていた「移動量が大きいと反発ループに入る」現象の原因は 2 つ:
	//   (a) 繋ぎ元が 1 フレームで大きく動くと距離拘束が巨大な補正を一発で加える
	//       → (Pos - PrevPos) が巨大化 → 次フレームに過大速度として再積分 → 振動
	//   (b) PBD の距離拘束は減衰を持たないため、伸び/縮みの振動が
	//       Damping (0.02) では収束しきれず、エネルギーが残留する
	//
	// 対策:
	//   1. サブステッピング: 繋ぎ元の移動量が 1 セグメント長を大きく超えるなら
	//      フレームを分割し、繋ぎ元位置を線形補間しながら段階的に解く
	//   2. 径方向 (縄方向) 速度減衰: 拘束解決後、各粒子の速度のうち
	//      「縄に沿う方向」成分だけを強く減衰し、「縄に直交する方向」成分は残す
	//      → 伸縮振動だけを潰し、フレイル的なスイング (接線方向) は保存される
	// =====================================================================

	FVector StartWorld, EndWorld;
	ResolveBothEndpointsWithInset(StartWorld, EndWorld);

	const int32 N = Particles.Num();
	const int32 LastSeg = N - 2;

	const FVector StartEmergeW = ResolveEmergeWorldOffset(StartActor, StartEmergeOffset);
	const FVector EndEmergeW   = ResolveEmergeWorldOffset(EndActor,   EndEmergeOffset);
	const int32 SmoothK = FMath::Clamp(EmergeSmoothCount, 0, FMath::Max(0, N / 2 - 1));
	const bool bPinStart = !StartEmergeW.IsNearlyZero() && SmoothK > 0;
	const bool bPinEnd   = !EndEmergeW.IsNearlyZero()   && SmoothK > 0;

	FVector HandGripWorld = FVector::ZeroVector;
	bool bHasHandGrip = false;
	int32 HandIdx = HandGripParticleIndex;
	if (HandGripComponent)
	{
		bHasHandGrip = ResolveHandGripWorld(HandGripWorld);
		if (HandIdx <= 0 || HandIdx >= N - 1) { HandIdx = N / 2; }
	}

	auto IsPinned = [&](int32 i) -> bool
	{
		if (bPinStart && i >= 1 && i <= SmoothK) return true;
		if (bPinEnd   && i >= N - 1 - SmoothK && i <= N - 2) return true;
		if (bHasHandGrip && i == HandIdx) return true;
		return false;
	};

	// -----------------------------------------------------------------
	// サブステップ数を「端点の移動量 / 1 セグメント rest 長」から決定
	// 1 セグメント長を超える移動を 1 フレームで処理するとショックが大きい。
	// -----------------------------------------------------------------
	const FVector PrevStart = bAnchorPrevValid ? PrevStartWorld : StartWorld;
	const FVector PrevEnd   = bAnchorPrevValid ? PrevEndWorld   : EndWorld;
	const float StartMotion = static_cast<float>((StartWorld - PrevStart).Size());
	const float EndMotion   = static_cast<float>((EndWorld   - PrevEnd  ).Size());
	const float MaxMotion   = FMath::Max(StartMotion, EndMotion);
	const float SubStepThreshold = FMath::Max(1.0f, SegmentRestLength * 0.75f);
	const int32 SubSteps = FMath::Clamp(FMath::CeilToInt(MaxMotion / SubStepThreshold), 1, 16);

	const float SubDt = DeltaTime / static_cast<float>(SubSteps);
	const float SubDT2 = SubDt * SubDt;
	// 1フレーム合計で同じ減衰効果になるよう、サブステップごとの係数を調整
	const float FrameDamping = FMath::Clamp(1.0f - Damping, 0.0f, 1.0f);
	const float SubDampingFactor = FMath::Pow(FrameDamping, 1.0f / static_cast<float>(SubSteps));

	// 1秒あたり最大速度 → 「サブステップあたり最大変位」(厳しめに 5 m/s = 500 cm/s)
	constexpr double MaxVelPerSec = 500.0;
	const float MaxSubStep = static_cast<float>(MaxVelPerSec) * SubDt;
	const float MaxSubStepSq = MaxSubStep * MaxSubStep;

	// 【強制減衰】サブステップごとの保持率。0=完全停止、1=減衰なし。
	//  - 径方向(縄方向)は伸縮振動なので即座に消す: 0.0
	//  - 接線方向(スイング)も強く減衰させて「勢いの加算」を防ぐ: 0.6
	// 1秒あたり接線方向のエネルギーは 0.6^(SubSteps/SubDt 換算) で急速に減衰
	constexpr float RadialKeep   = 0.0f;
	constexpr float TangentKeep  = 1.0f;

	const float K = FMath::Clamp(Stiffness, 0.0f, 1.0f);
	const float MaxRatio = FMath::Max(1.0f, MaxStretchRatio);

	// 【絶対上限】全セグメントの max 長を合算した「縄全体の絶対最大長」
	// = Σ (rest_i × MaxStretchRatio)。実行前に決定された値で、ランタイムで変動しない。
	float MaxTotalLen = 0.0f;
	for (int32 i = 0; i < N - 1; ++i)
	{
		MaxTotalLen += GetSegmentRestLength(i) * MaxRatio;
	}

	const float StraightDistNow = FVector::Dist(StartWorld, EndWorld);
	const float SagAtN = FMath::Max(0.0f, TotalRopeLength - StraightDistNow) * 0.5f;

	for (int32 Sub = 1; Sub <= SubSteps; ++Sub)
	{
		const float Alpha = static_cast<float>(Sub) / static_cast<float>(SubSteps);
		const FVector SubStartRaw = FMath::Lerp(PrevStart, StartWorld, Alpha);
		const FVector SubEndRaw   = FMath::Lerp(PrevEnd,   EndWorld,   Alpha);

		// 【端点間距離の絶対クランプ】
		// SubStart と SubEnd の直線距離が MaxTotalLen を超えていたら、
		// 縄物理ではどう頑張っても全長を満たせない (端点が外から強引に
		// 引き離されている)。この場合は SubEnd を SubStart に向けて引き戻し、
		// 「縄が実行前に決めた最大値以上に伸びる」ことを原理的に不可能にする。
		// (繋ぎ先アクターの実位置とは乖離するが、縄自体の最大長は厳守される)
		FVector SubStart = SubStartRaw;
		FVector SubEnd = SubEndRaw;
		{
			const FVector EndDelta = SubEnd - SubStart;
			const float EndDist = EndDelta.Size();
			if (EndDist > MaxTotalLen && EndDist > KINDA_SMALL_NUMBER)
			{
				SubEnd = SubStart + EndDelta * (MaxTotalLen / EndDist);
			}
		}

		// HandGripWorld はサブフレーム間で動く可能性があるが、簡略化のため固定値を使う

		// -- 1) 中間粒子の Verlet 統合 (ワールド空間)
		for (int32 i = 1; i < N - 1; ++i)
		{
			if (IsPinned(i)) continue;
			FRopeParticle& P = Particles[i];
			FVector Velocity = (P.Pos - P.PrevPos) * SubDampingFactor;

			if (MaxSubStep > KINDA_SMALL_NUMBER && Velocity.SizeSquared() > MaxSubStepSq)
			{
				Velocity = Velocity.GetSafeNormal() * MaxSubStep;
			}

			const FVector NewPos = P.Pos + Velocity + Gravity * SubDT2;
			P.PrevPos = P.Pos;
			P.Pos = NewPos;
		}

		// -- 2) 境界条件: 端点・emerge ピン・ハンドグリップを位置固定 (PrevPos も同値)
		Particles[0].Pos = SubStart;
		Particles[0].PrevPos = SubStart;
		Particles[N - 1].Pos = SubEnd;
		Particles[N - 1].PrevPos = SubEnd;

		if (bPinStart || bPinEnd)
		{
			auto NaturalAt = [&](int32 idx) {
				const float Tn = static_cast<float>(idx) / static_cast<float>(N - 1);
				FVector Pt = FMath::Lerp(SubStart, SubEnd, Tn);
				Pt.Z -= SagAtN * 4.0f * Tn * (1.0f - Tn);
				return Pt;
			};
			if (bPinStart)
			{
				PinEmergeCurve(SubStart, StartEmergeW, NaturalAt(SmoothK + 1), 1, +1, SmoothK);
			}
			if (bPinEnd)
			{
				PinEmergeCurve(SubEnd, EndEmergeW, NaturalAt(N - 2 - SmoothK), N - 2, -1, SmoothK);
			}
		}
		if (bHasHandGrip)
		{
			Particles[HandIdx].Pos = HandGripWorld;
			Particles[HandIdx].PrevPos = HandGripWorld;
		}

		// -- 3) 距離拘束 (Gauss-Seidel 双方向スイープ)
		// 【重要】補正を Pos と PrevPos の両方に同時適用 (XPBD 流)。
		// こうすると補正そのものは速度に影響せず、「粒子を移動させる」だけになる。
		// 旧来の Pos のみ補正だと、補正方向と逆向きの偽の運動量が注入され
		// 反発・遠心ループを引き起こしていた。
		for (int32 Iter = 0; Iter < ConstraintIterations; ++Iter)
		{
			Particles[0].Pos = SubStart;
			Particles[0].PrevPos = SubStart;
			Particles[N - 1].Pos = SubEnd;
			Particles[N - 1].PrevPos = SubEnd;
			if (bHasHandGrip) { Particles[HandIdx].Pos = HandGripWorld; Particles[HandIdx].PrevPos = HandGripWorld; }

			const bool bForward = (Iter % 2 == 0);
			for (int32 step = 0; step <= LastSeg; ++step)
			{
				const int32 i = bForward ? step : (LastSeg - step);
				FVector& A = Particles[i].Pos;
				FVector& B = Particles[i + 1].Pos;
				FVector Delta = B - A;
				const float Dist = Delta.Size();
				if (Dist <= KINDA_SMALL_NUMBER) continue;

				const float RestLen = GetSegmentRestLength(i);
				float Diff = (Dist - RestLen) / Dist;
				if (Dist < RestLen * 1.05f) { Diff *= K; }

				const FVector Correction = Delta * 0.5f * Diff;
				const bool bFixedA = (i == 0) || IsPinned(i);
				const bool bFixedB = (i + 1 == N - 1) || IsPinned(i + 1);

				if (!bFixedA && !bFixedB)
				{
					A += Correction; Particles[i].PrevPos += Correction;
					B -= Correction; Particles[i + 1].PrevPos -= Correction;
				}
				else if (bFixedA && !bFixedB)
				{
					const FVector C2 = Correction * 2.0f;
					B -= C2; Particles[i + 1].PrevPos -= C2;
				}
				else if (!bFixedA && bFixedB)
				{
					const FVector C2 = Correction * 2.0f;
					A += C2; Particles[i].PrevPos += C2;
				}
			}
		}

		// -- 4) ハードクランプ (双方向スイープを十分回数行い必ず収束させる)
		// 【重要】補正を Pos と PrevPos に同時適用。これにより
		//   「クランプ自体が偽の運動量を粒子に与える」現象を防ぐ。
		// 限界に達した粒子は別途 bWasClamped マーキングして、後段で
		//  非弾性衝突として残留する真の運動エネルギーも散逸させる。
		TArray<bool, TInlineAllocator<64>> bWasClamped;
		bWasClamped.Init(false, N);
		{
			Particles[0].Pos = SubStart;
			Particles[0].PrevPos = SubStart;
			Particles[N - 1].Pos = SubEnd;
			Particles[N - 1].PrevPos = SubEnd;
			if (bHasHandGrip) { Particles[HandIdx].Pos = HandGripWorld; Particles[HandIdx].PrevPos = HandGripWorld; }

			// セグメント数分のパスで端から端まで完全に伝播させる
			const int32 ClampPasses = FMath::Max(4, N);
			for (int32 Pass = 0; Pass < ClampPasses; ++Pass)
			{
				const bool bForward = (Pass % 2 == 0);
				bool bAnyExcess = false;
				for (int32 step = 0; step <= LastSeg; ++step)
				{
					const int32 i = bForward ? step : (LastSeg - step);
					const float RestLen = GetSegmentRestLength(i);
					const float MaxLen = RestLen * MaxRatio;
					FVector& A = Particles[i].Pos;
					FVector& B = Particles[i + 1].Pos;
					FVector Delta = B - A;
					const float Dist = Delta.Size();
					if (Dist <= MaxLen || Dist <= KINDA_SMALL_NUMBER) continue;

					bAnyExcess = true;
					bWasClamped[i] = true;
					bWasClamped[i + 1] = true;

					const FVector Dir = Delta / Dist;
					const float Excess = Dist - MaxLen;
					const bool bFixedA = (i == 0) || IsPinned(i);
					const bool bFixedB = (i + 1 == N - 1) || IsPinned(i + 1);

					if (!bFixedA && !bFixedB)
					{
						const FVector Half = Dir * (Excess * 0.5f);
						A += Half; Particles[i].PrevPos += Half;
						B -= Half; Particles[i + 1].PrevPos -= Half;
					}
					else if (bFixedA && !bFixedB)
					{
						const FVector Full = Dir * Excess;
						B -= Full; Particles[i + 1].PrevPos -= Full;
					}
					else if (!bFixedA && bFixedB)
					{
						const FVector Full = Dir * Excess;
						A += Full; Particles[i].PrevPos += Full;
					}
				}
				if (!bAnyExcess) break;
			}

			// 【全長最終保証パス】
			// 上記反復で個別セグメントを上限以下にしても、合計が MaxTotalLen を
			// 超えるケース(両端が物理的に引き離されているとき等)が残りうる。
			// 折れ線の累積長を測り、超えていれば SubStart 側から比率で再配置する。
			float ActualTotal = 0.0f;
			for (int32 i = 0; i < N - 1; ++i)
			{
				ActualTotal += (Particles[i + 1].Pos - Particles[i].Pos).Size();
			}
			if (ActualTotal > MaxTotalLen && ActualTotal > KINDA_SMALL_NUMBER)
			{
				const float Scale = MaxTotalLen / ActualTotal;
				FVector PrevPosKept = Particles[0].Pos; // = SubStart
				for (int32 i = 1; i < N - 1; ++i)
				{
					if (IsPinned(i)) { PrevPosKept = Particles[i].Pos; continue; }
					const FVector Seg = Particles[i].Pos - PrevPosKept;
					const float SegLen = Seg.Size();
					if (SegLen > KINDA_SMALL_NUMBER)
					{
						const FVector NewPos = PrevPosKept + Seg * Scale;
						const FVector ShiftDelta = NewPos - Particles[i].Pos;
						Particles[i].Pos = NewPos;
						// 補正分を PrevPos にも反映 → 偽の運動量注入を防ぐ
						Particles[i].PrevPos += ShiftDelta;
						// この粒子は限界相当として非弾性散逸対象に追加
						bWasClamped[i] = true;
					}
					PrevPosKept = Particles[i].Pos;
				}
				Particles[N - 1].Pos = SubEnd;
				Particles[N - 1].PrevPos = SubEnd;
			}
		}

		// -- 5a) 【非弾性衝突】限界に達した粒子の全速度を強く削る
		// PBD の hard clamp は「位置を補正するが PrevPos を保持」する性質上、
		// その補正自体が暗黙的速度として残り → 次サブステップで反発 → 再び限界へ、
		// という共振ループを生む。これを断つには、限界を踏んだ粒子の運動エネルギーを
		// 「非弾性衝突」として一気に散逸させるしかない (現実の縄が伸びきった瞬間に
		// 「ガツン」と止まり熱として失うエネルギーに相当)。
		// ImpactKeep が小さいほど「カクッと止まる」挙動になる。
		constexpr float ImpactKeep = 0.1f; // 限界到達時、速度の 10% のみ残す
		for (int32 i = 1; i < N - 1; ++i)
		{
			if (!bWasClamped[i] || IsPinned(i)) continue;
			FRopeParticle& P = Particles[i];
			const FVector Vel = P.Pos - P.PrevPos;
			P.PrevPos = P.Pos - Vel * ImpactKeep;
		}

		// -- 5b) 【常時減衰】限界に達していない粒子にも軽度の方向別減衰
		// 各粒子の速度 (Pos - PrevPos) を縄方向 (radial) と直交方向 (tangential) に分解し
		//   - radial × 0.0 : 伸縮振動を消す
		//   - tangential × 0.6 : 振り回す勢いの累積を防ぐ (スイング自体は残る)
		for (int32 i = 1; i < N - 1; ++i)
		{
			if (IsPinned(i)) continue;
			if (bWasClamped[i]) continue; // 5a で既に散逸済み
			FRopeParticle& P = Particles[i];
			const FVector PrevDir = Particles[i].Pos - Particles[i - 1].Pos;
			const FVector NextDir = Particles[i + 1].Pos - Particles[i].Pos;
			FVector RopeDir = PrevDir + NextDir;
			FVector NewVel;
			const FVector Vel = P.Pos - P.PrevPos;
			if (RopeDir.Normalize())
			{
				const float RadialComp = FVector::DotProduct(Vel, RopeDir);
				const FVector RadialVec = RopeDir * RadialComp;
				const FVector TangentVec = Vel - RadialVec;
				NewVel = TangentVec * TangentKeep + RadialVec * RadialKeep;
			}
			else
			{
				NewVel = Vel * TangentKeep;
			}
			P.PrevPos = P.Pos - NewVel;
		}

		// -- 6) 速度上限の最終クランプ
		if (MaxSubStep > KINDA_SMALL_NUMBER)
		{
			for (int32 i = 1; i < N - 1; ++i)
			{
				if (IsPinned(i)) continue;
				FRopeParticle& P = Particles[i];
				const FVector Step = P.Pos - P.PrevPos;
				const float StepSq = Step.SizeSquared();
				if (StepSq > MaxSubStepSq)
				{
					const FVector Clamped = Step.GetSafeNormal() * MaxSubStep;
					P.PrevPos = P.Pos - Clamped;
				}
			}
		}
	}

	PrevStartWorld = StartWorld;
	PrevEndWorld = EndWorld;
	bAnchorPrevValid = true;

	UpdateSplineFromParticles();
}

void ARopeSimulationSpline::PinEmergeCurve(const FVector& Anchor, const FVector& EmergeW,
                                          const FVector& NaturalEnd, int32 StartIdx, int32 Direction,
                                          int32 Count)
{
	if (EmergeW.IsNearlyZero() || Count <= 0) { return; }
	if (Particles.Num() < 3) { return; }

	if (bUseEmergeBezier)
	{
		// ===== ベジエ曲線モード =====
		// 二次ベジエ: P0=Anchor, P1=Anchor+EmergeW (制御点), P2=NaturalEnd
		const FVector P0 = Anchor;
		const FVector P1 = Anchor + EmergeW;
		const FVector P2 = NaturalEnd;

		// 累積弧長テーブル
		constexpr int32 SampleCount = 64;
		FVector Samples[SampleCount + 1];
		float CumLen[SampleCount + 1];
		Samples[0] = P0;
		CumLen[0]  = 0.0f;
		for (int32 s = 1; s <= SampleCount; ++s)
		{
			const float t = static_cast<float>(s) / static_cast<float>(SampleCount);
			const float OneT = 1.0f - t;
			Samples[s] = OneT * OneT * P0 + 2.0f * OneT * t * P1 + t * t * P2;
			CumLen[s]  = CumLen[s - 1] + (Samples[s] - Samples[s - 1]).Size();
		}
		const float TotalArcLen = CumLen[SampleCount];

		// セグメント rest 長の累積でベジエ上の位置を決定
		float Accum = 0.0f;
		for (int32 k = 0; k < Count; ++k)
		{
			const int32 ParticleIdx = StartIdx + Direction * k;
			if (ParticleIdx < 0 || ParticleIdx >= Particles.Num()) { break; }

			const int32 SegIdx = (Direction > 0) ? k : (NumSegments - 1 - k);
			Accum += GetSegmentRestLength(SegIdx);
			const float Target = FMath::Min(Accum, TotalArcLen);

			int32 s = 0;
			while (s < SampleCount && CumLen[s + 1] < Target) { ++s; }
			const float Span = FMath::Max(KINDA_SMALL_NUMBER, CumLen[s + 1] - CumLen[s]);
			const float Frac = (Target - CumLen[s]) / Span;
			const FVector P = FMath::Lerp(Samples[s], Samples[s + 1], Frac);

			Particles[ParticleIdx].Pos = P;
			Particles[ParticleIdx].PrevPos = P;
		}
		return;
	}

	// ===== 直線モード (デフォルト) =====
	// 1 個目はユーザ指定の Anchor + EmergeW にピッタリ配置
	// 2 個目以降は emerge 方向に沿って rest 長で進む
	const float EmergeMag = EmergeW.Size();
	if (EmergeMag <= KINDA_SMALL_NUMBER) { return; }
	const FVector EmergeDir = EmergeW / EmergeMag;

	float Accum = 0.0f;
	for (int32 k = 0; k < Count; ++k)
	{
		const int32 ParticleIdx = StartIdx + Direction * k;
		if (ParticleIdx < 0 || ParticleIdx >= Particles.Num()) { break; }

		FVector P;
		if (k == 0)
		{
			P = Anchor + EmergeW;
			Accum = EmergeMag;
		}
		else
		{
			const int32 SegIdx = (Direction > 0) ? k : (NumSegments - 1 - k);
			Accum += GetSegmentRestLength(SegIdx);
			P = Anchor + EmergeDir * Accum;
		}

		Particles[ParticleIdx].Pos = P;
		Particles[ParticleIdx].PrevPos = P;
	}
}

float ARopeSimulationSpline::GetSegmentRestLength(int32 SegmentIndex) const
{
	if (SegmentIndex < 0 || SegmentIndex >= NumSegments)
	{
		return SegmentRestLength;
	}
	if (const float* Override = SegmentLengthOverrides.Find(SegmentIndex))
	{
		return FMath::Max(0.01f, *Override);
	}
	return SegmentRestLength;
}

FVector ARopeSimulationSpline::ResolveEmergeWorldOffset(AActor* Actor, const FVector& LocalOffset) const
{
	if (LocalOffset.IsNearlyZero()) { return FVector::ZeroVector; }
	if (!Actor) { Actor = GetParentActor(); }
	if (Actor)
	{
		return Actor->GetActorTransform().TransformVector(LocalOffset);
	}
	return LocalOffset;
}

void ARopeSimulationSpline::ResolveBothEndpointsWithInset(FVector& OutStart, FVector& OutEnd) const
{
	ResolveEndpoint(StartActor, StartSocketName, StartLocalOffset, OutStart);
	ResolveEndpoint(EndActor,   EndSocketName,   EndLocalOffset,   OutEnd);

	if (EndpointInsetDepth > 0.0f)
	{
		const FVector Delta = OutEnd - OutStart;
		const float Dist = Delta.Size();
		if (Dist > KINDA_SMALL_NUMBER)
		{
			const FVector Dir = Delta / Dist;
			// 押し込み距離は端点同士の距離の半分を上限とする (両端が交差しないように)
			const float Inset = FMath::Min(EndpointInsetDepth, Dist * 0.49f);
			OutStart += Dir * Inset;
			OutEnd   -= Dir * Inset;
		}
	}
}

void ARopeSimulationSpline::ResolveEndpoint(AActor* Actor, FName Socket,
                                           const FVector& LocalOffset, FVector& OutWorld) const
{
	// Actor 未指定でソケット名がある場合は、ChildActor の親アクター (馬等) を自動使用する
	if (!Actor && !Socket.IsNone())
	{
		Actor = GetParentActor();
	}

	if (Actor)
	{
		if (!Socket.IsNone())
		{
			// 1. ルートコンポーネント直下を最優先
			if (USceneComponent* RootSC = Actor->GetRootComponent())
			{
				if (RootSC->DoesSocketExist(Socket))
				{
					OutWorld = RootSC->GetSocketLocation(Socket)
						+ RootSC->GetComponentTransform().TransformVector(LocalOffset);
					return;
				}
			}
			// 2. Character 等ではルートがカプセルなのでスケルタルメッシュも検索
			TInlineComponentArray<USkeletalMeshComponent*> SkelMeshes(Actor);
			for (USkeletalMeshComponent* SkMC : SkelMeshes)
			{
				if (SkMC && SkMC->DoesSocketExist(Socket))
				{
					OutWorld = SkMC->GetSocketLocation(Socket)
						+ SkMC->GetComponentTransform().TransformVector(LocalOffset);
					return;
				}
			}
			// 3. 念のため他の Primitive 系コンポーネントも探す
			TInlineComponentArray<UPrimitiveComponent*> Prims(Actor);
			for (UPrimitiveComponent* PC : Prims)
			{
				if (PC && PC->DoesSocketExist(Socket))
				{
					OutWorld = PC->GetSocketLocation(Socket)
						+ PC->GetComponentTransform().TransformVector(LocalOffset);
					return;
				}
			}
		}
		OutWorld = Actor->GetActorTransform().TransformPosition(LocalOffset);
		return;
	}
	OutWorld = GetActorTransform().TransformPosition(LocalOffset);
}

void ARopeSimulationSpline::InitializeParticlesAlongLine()
{
	const int32 NumPoints = FMath::Max(2, NumSegments + 1);
	Particles.SetNum(NumPoints);

	FVector StartWorld, EndWorld;
	ResolveBothEndpointsWithInset(StartWorld, EndWorld);

	const float StraightDist = FVector::Dist(StartWorld, EndWorld);

	// 端点が解決できていない (ソケットがまだ登録前 等) で StraightDist=0 だと
	// TotalRopeLength=0 になりたわみが永久に発生しなくなる。
	// OverrideTotalLength が指定されていない場合は初期化を保留する。
	if (StraightDist < KINDA_SMALL_NUMBER && OverrideTotalLength <= 0.0f)
	{
		// 粒子配置だけは行い、bRopeInitialized は false のままにして次回再試行させる
		for (int32 i = 0; i < NumPoints; ++i)
		{
			Particles[i].Pos = StartWorld;
			Particles[i].PrevPos = StartWorld;
		}
		bRopeInitialized = false;
		TotalRopeLength = 0.0f;
		SegmentRestLength = 0.0f;
		return;
	}

	// 全長は (Override / SlackFactor) で決定。デフォルト rest 長は均等分割。
	// SegmentLengthOverrides に登録されたセグメントのみ個別 rest 長に差し替わる。
	TotalRopeLength = (OverrideTotalLength > 0.0f)
		? OverrideTotalLength
		: StraightDist * FMath::Max(1.0f, SlackFactor);
	SegmentRestLength = TotalRopeLength / FMath::Max(1, NumPoints - 1);
	bRopeInitialized = true;

	// 余剰長分だけ中央を下方向に垂らした初期形状（懸垂線の近似）
	const float Sag = FMath::Max(0.0f, TotalRopeLength - StraightDist) * 0.5f;
	for (int32 i = 0; i < NumPoints; ++i)
	{
		const float T = static_cast<float>(i) / static_cast<float>(NumPoints - 1);
		FVector P = FMath::Lerp(StartWorld, EndWorld, T);
		// 4*T*(1-T) は端で0、中央で1 → 中央を Sag だけ下げる
		P.Z -= Sag * 4.0f * T * (1.0f - T);
		Particles[i].Pos = P;
		Particles[i].PrevPos = P;
	}

	// emerge カーブによるピン留め (始点側 / 終点側)
	const FVector StartEmergeW = ResolveEmergeWorldOffset(StartActor, StartEmergeOffset);
	const FVector EndEmergeW   = ResolveEmergeWorldOffset(EndActor,   EndEmergeOffset);
	const int32 SmoothK = FMath::Clamp(EmergeSmoothCount, 0, FMath::Max(0, NumPoints / 2 - 1));
	if (SmoothK > 0)
	{
		// NaturalEnd: 自然な懸垂ライン上、SmoothK+1 個目の粒子の予測位置
		auto NaturalAt = [&](int32 idx) {
			const float Tn = static_cast<float>(idx) / static_cast<float>(NumPoints - 1);
			FVector P = FMath::Lerp(StartWorld, EndWorld, Tn);
			P.Z -= Sag * 4.0f * Tn * (1.0f - Tn);
			return P;
		};
		PinEmergeCurve(StartWorld, StartEmergeW, NaturalAt(SmoothK + 1), /*Start=*/1, /*Dir=*/+1, SmoothK);
		PinEmergeCurve(EndWorld,   EndEmergeW,   NaturalAt(NumPoints - 2 - SmoothK),
		               /*Start=*/NumPoints - 2, /*Dir=*/-1, SmoothK);
	}
}

void ARopeSimulationSpline::RebuildSplineMeshes()
{
	// 既存を破棄
	for (TObjectPtr<USplineMeshComponent>& SM : SegmentMeshes)
	{
		if (SM)
		{
			SM->DestroyComponent();
		}
	}
	SegmentMeshes.Reset();

	UStaticMesh* Mesh = SegmentMesh.LoadSynchronous();
	const int32 NumPoints = FMath::Max(2, NumSegments + 1);
	const int32 NumMeshes = NumPoints - 1;
	SegmentMeshes.Reserve(NumMeshes);
	for (int32 i = 0; i < NumMeshes; ++i)
	{
		USplineMeshComponent* SM = NewObject<USplineMeshComponent>(this);
		SM->SetMobility(EComponentMobility::Movable);
		SM->SetupAttachment(SceneRoot);
		SM->RegisterComponent();
		if (Mesh)
		{
			SM->SetStaticMesh(Mesh);
		}
		SM->SetStartScale(FVector2D(RopeWidth, RopeWidth));
		SM->SetEndScale(FVector2D(RopeWidth, RopeWidth));
		SM->SetForwardAxis(SegmentForwardAxis);
		SM->SetCastShadow(bSegmentCastShadow);
		SegmentMeshes.Add(SM);
	}
}

void ARopeSimulationSpline::UpdateSplineFromParticles()
{
	if (!SplineComp) return;
	if (Particles.Num() < 2) return;

	// Spline 自体はワールド座標で点列を持つ
	SplineComp->ClearSplinePoints(false);
	for (int32 i = 0; i < Particles.Num(); ++i)
	{
		SplineComp->AddSplineWorldPoint(Particles[i].Pos);
	}
	SplineComp->UpdateSpline();

	if (SegmentMeshes.Num() != Particles.Num() - 1)
	{
		RebuildSplineMeshes();
	}

	// SplineMeshComponent::SetStartAndEnd は「そのコンポーネントの local 空間」を要求する。
	// Spline の local 空間とは必ずしも一致しない (親に scale/回転がある場合に破綻する) ため、
	// ワールド座標から SplineMesh 自身の component transform で逆変換して渡す。
	for (int32 i = 0; i < SegmentMeshes.Num(); ++i)
	{
		USplineMeshComponent* SM = SegmentMeshes[i];
		if (!SM) continue;

		const FTransform& SMXform = SM->GetComponentTransform();

		FVector StartWorld = Particles[i].Pos;
		FVector EndWorld   = Particles[i + 1].Pos;

		// セグメント同士を少し重ねてキャップを隠す
		if (SegmentOverlap > 0.0f)
		{
			const FVector Dir = (EndWorld - StartWorld);
			const FVector PrevDir = (i > 0)
				? (Particles[i].Pos - Particles[i - 1].Pos)
				: Dir;
			const FVector NextDir = (i + 2 < Particles.Num())
				? (Particles[i + 2].Pos - Particles[i + 1].Pos)
				: Dir;
			StartWorld -= PrevDir * SegmentOverlap * 0.5f;
			EndWorld   += NextDir * SegmentOverlap * 0.5f;
		}

		const FVector StartLocal = SMXform.InverseTransformPosition(StartWorld);
		const FVector EndLocal   = SMXform.InverseTransformPosition(EndWorld);

		// 接線は「このセグメントの進行方向」を直接使う。
		// 中心差分 (Catmull-Rom 風) を使うと、隣接セグメント同士の方向が大きく
		// 異なる場合に Hermite 曲線が膨らんで SplineMesh が異常変形する。
		// セグメント方向そのものを使えばまっすぐ繋がる (粒子数が多ければ十分滑らか)。
		const FVector SegDirWorld = Particles[i + 1].Pos - Particles[i].Pos;
		const FVector StartTanWorld = SegDirWorld;
		const FVector EndTanWorld   = SegDirWorld;

		const FVector StartTan = SMXform.InverseTransformVector(StartTanWorld);
		const FVector EndTan   = SMXform.InverseTransformVector(EndTanWorld);

		// プロパティで軸が変更されていれば反映
		if (SM->GetForwardAxis() != SegmentForwardAxis)
		{
			SM->SetForwardAxis(SegmentForwardAxis);
		}
		if (SM->CastShadow != bSegmentCastShadow)
		{
			SM->SetCastShadow(bSegmentCastShadow);
		}

		SM->SetStartAndEnd(StartLocal, StartTan, EndLocal, EndTan, true);
		SM->SetStartScale(FVector2D(RopeWidth, RopeWidth), false);
		SM->SetEndScale(FVector2D(RopeWidth, RopeWidth), true);
	}
}

// =====================================================================
// ハンドグリップ
// =====================================================================
void ARopeSimulationSpline::AttachHandGrip(USceneComponent* InGripComponent, FName InGripSocket, int32 InParticleIndex)
{
	HandGripComponent = InGripComponent;
	HandGripSocket = InGripSocket;
	HandGripParticleIndex = InParticleIndex;
}

void ARopeSimulationSpline::DetachHandGrip()
{
	HandGripComponent = nullptr;
	HandGripSocket = NAME_None;
	HandGripParticleIndex = INDEX_NONE;
}

bool ARopeSimulationSpline::ResolveHandGripWorld(FVector& OutWorld) const
{
	if (!HandGripComponent) { return false; }

	if (!HandGripSocket.IsNone() && HandGripComponent->DoesSocketExist(HandGripSocket))
	{
		OutWorld = HandGripComponent->GetSocketLocation(HandGripSocket);
		return true;
	}
	OutWorld = HandGripComponent->GetComponentLocation();
	return true;
}
