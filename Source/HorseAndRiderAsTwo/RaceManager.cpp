#include "RaceManager.h"
#include "TrackActor.h"
#include "HorseCharacter.h"
#include "CheckpointActor.h"
#include "GoalTrigger.h"
#include "GhostPlayer.h"
#include "Components/SplineComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "TimerManager.h"

// =====================================================================
// コンストラクタ
// =====================================================================
ARaceManager::ARaceManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

// =====================================================================
// BeginPlay
// =====================================================================
void ARaceManager::BeginPlay()
{
	Super::BeginPlay();

	// Pregame 初期化：Track と馬リストを構築し、入力ロック状態を保証する
	ResolveTrack();
	CollectHorses();
	SetState(ERaceState::Pregame);
}

// =====================================================================
// カウントダウン開始（外部から呼ぶ）
// =====================================================================
void ARaceManager::StartCountdown()
{
	if (RaceState != ERaceState::Pregame)
	{
		return;
	}

	// StartGridSpawner で後からスポーンされた馬を取りこぼさないよう、
	// Countdown 突入直前に CollectHorses を再実行する。
	ResolveTrack();
	CollectHorses();

	SetState(ERaceState::Countdown);

	if (UWorld* World = GetWorld())
	{
		const float Duration = FMath::Max(0.0f, CountdownDuration);
		World->GetTimerManager().SetTimer(
			CountdownTimerHandle,
			this,
			&ARaceManager::OnCountdownFinished,
			Duration,
			false);
	}
}

// =====================================================================
// カウントダウン完了
// =====================================================================
void ARaceManager::OnCountdownFinished()
{
	StartRace();
}

// =====================================================================
// レース開始（Running 突入専用）
// =====================================================================
void ARaceManager::StartRace()
{
	NextFinishRank = 1;
	RaceStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	// スタート位置で LastInputKey を実位置に初期化するためのスプライン参照
	USplineComponent* SplineComp = Track ? Track->GetSpline() : nullptr;

	for (FRaceEntry& E : Entries)
	{
		// 各 Entry の Lap 計測基点を Running 突入時刻に揃える
		E.LastLapTime = RaceStartTime;

		// LastInputKey をスタート時の実位置で初期化する。
		// CollectHorses では 0.0（Spline 始点）で初期化されるが、StartGridSpawner が
		// 馬を始点より前方へ配置するため、初回フレームで NewKey と LastInputKey(0.0) の
		// 差が大きくなり周回境界を誤判定する恐れがある。実位置で初期化して防止する。
		if (SplineComp && E.Horse)
		{
			E.LastInputKey = SplineComp->FindInputKeyClosestToWorldLocation(E.Horse->GetActorLocation());
		}
	}

	UpdateDistances();
	UpdateRanking();

	// M6: ゴースト記録の開始と、前レースのベストラップゴーストの再生を起動する。
	BeginGhostRecording();
	SpawnAndPlayGhost();

	SetState(ERaceState::Running);
}

// =====================================================================
// 状態遷移
// =====================================================================
void ARaceManager::SetState(ERaceState NewState)
{
	if (RaceState == NewState) { return; }

	RaceState = NewState;
	OnRaceStateChanged.Broadcast(NewState);
}

// =====================================================================
// Tick
// =====================================================================
void ARaceManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (RaceState != ERaceState::Running)
	{
		if (bShowDebugUI)
		{
			DrawDebugUI();
		}
		return;
	}

	UpdateDistances();
	UpdateRanking();

	if (bShowDebugUI)
	{
		DrawDebugUI();
	}
}

// =====================================================================
// Track をレベルから検索
// =====================================================================
void ARaceManager::ResolveTrack()
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
// 馬を全件収集
// =====================================================================
void ARaceManager::CollectHorses()
{
	Entries.Reset();

	// レース開始境界で前 Tick 順位をクリアし、脱退馬の残留による誤発火を防ぐ。
	PrevRankMap.Empty();

	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AHorseCharacter::StaticClass(), Found);

	for (AActor* A : Found)
	{
		AHorseCharacter* Horse = Cast<AHorseCharacter>(A);
		if (!Horse) { continue; }

		FRaceEntry Entry;
		Entry.Horse = Horse;
		Entry.LapsCompleted = 0;
		Entry.LastInputKey = 0.0f;
		Entry.TotalDistance = 0.0f;
		Entry.LastCheckpointIndex = -1;
		Entry.bJustReset = false;
		Entry.LapTimes.Reset();
		Entry.BestLapTime = 0.0f;
		Entry.LastLapTime = 0.0f;
		Entries.Add(Entry);
	}

	// Checkpoint 収集:
	//   1) Track からスポーンされた Checkpoint があれば最優先（既に CheckpointIndex 昇順）
	//   2) 無ければレベル直配置をフォールバックとして収集し CheckpointIndex 昇順に整列
	Checkpoints.Reset();

	TArray<ACheckpointActor*> TrackCheckpoints;
	if (Track)
	{
		TrackCheckpoints = Track->GetSpawnedCheckpoints();
	}

	if (TrackCheckpoints.Num() > 0)
	{
		// Track 由来の Checkpoint は距離昇順で返るため、配列位置で CheckpointIndex を確定する。
		// （OnConstruction 時は ChildActor 未生成で Index 設定が漏れるため、ここで再採番する）
		int32 Idx = 0;
		for (ACheckpointActor* CP : TrackCheckpoints)
		{
			if (!CP) { continue; }
			CP->CheckpointIndex = Idx++;
			Checkpoints.Add(CP);
		}
	}
	else
	{
		TArray<AActor*> FoundCheckpoints;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ACheckpointActor::StaticClass(), FoundCheckpoints);
		for (AActor* A : FoundCheckpoints)
		{
			if (ACheckpointActor* CP = Cast<ACheckpointActor>(A))
			{
				Checkpoints.Add(CP);
			}
		}
		Checkpoints.Sort([](const ACheckpointActor& A, const ACheckpointActor& B)
		{
			return A.CheckpointIndex < B.CheckpointIndex;
		});
	}

	// Track 経由でスポーンされた GoalTrigger の RaceManager 参照を補完
	// （子アクターは BeginPlay 順序が不確定なため、Goal 側の自動検索より早く明示設定する）
	if (Track)
	{
		if (AGoalTrigger* Goal = Track->GetSpawnedGoal())
		{
			Goal->SetRaceManager(this);
		}
		for (ACheckpointActor* CP : Checkpoints)
		{
			if (CP) { CP->SetRaceManager(this); }
		}
	}
}

// =====================================================================
// 各馬のスプライン距離を更新
// =====================================================================
void ARaceManager::UpdateDistances()
{
	if (!Track) { return; }
	USplineComponent* SplineComp = Track->GetSpline();
	if (!SplineComp) { return; }

	const float TrackLength = Track->GetTrackLength();

	for (FRaceEntry& E : Entries)
	{
		if (!E.Horse) { continue; }
		if (E.bFinished) { continue; }

		// ワールド座標 -> スプライン上の最近接点 -> 距離 (FindInputKeyClosestToWorldLocation 経由)
		const FVector WorldLoc = E.Horse->GetActorLocation();
		const float NewKey = SplineComp->FindInputKeyClosestToWorldLocation(WorldLoc);

		// Key (浮動小数点インデックス) -> 距離 へ変換
		E.DistanceAlongSpline = SplineComp->GetDistanceAlongSplineAtSplineInputKey(NewKey);

		// ResetToTrack 直後の 1 フレームはラップ境界判定をスキップし、
		// LastInputKey を新位置で再計算してから次フレームで判定を再開する。
		if (E.bJustReset)
		{
			E.bJustReset = false;
			E.LastInputKey = NewKey;
			const float CalculatedTotalReset = (E.LapsCompleted * TrackLength) + E.DistanceAlongSpline;
			E.TotalDistance = FMath::Max(0.0f, CalculatedTotalReset);
			continue;
		}

		// ラップ加算は AGoalTrigger 通過イベント（OnGoalCrossed）に統合済み。
		// ここでは距離計算のみ行い、ラップ境界判定は行わない。
		E.LastInputKey = NewKey;

		// 累計走行距離の更新（逆方向で一時的にマイナスになっても 0 以下にならないようクランプ）
		float CalculatedTotal = (E.LapsCompleted * TrackLength) + E.DistanceAlongSpline;
		E.TotalDistance = FMath::Max(0.0f, CalculatedTotal);
	}
}

// =====================================================================
// 順位ソート
// =====================================================================
void ARaceManager::UpdateRanking()
{
	Ranking = Entries;

	Ranking.Sort([](const FRaceEntry& A, const FRaceEntry& B)
	{
		// ゴール済みが上位、ゴール前は FinishRank が小さい順
		if (A.bFinished && B.bFinished) { return A.FinishRank < B.FinishRank; }
		if (A.bFinished != B.bFinished) { return A.bFinished; }
		// 未ゴール同士は総走行距離が大きい順
		return A.TotalDistance > B.TotalDistance;
	});

	// ソート確定後に順位変動を通知する。
	BroadcastRankChanges();
}

// =====================================================================
// 順位変動通知（副次：演出補助）
// =====================================================================

void ARaceManager::BroadcastRankChanges()
{
	for (int32 i = 0; i < Ranking.Num(); ++i)
	{
		AHorseCharacter* Horse = Ranking[i].Horse;
		if (!Horse) { continue; }

		const int32 NewRank = i + 1;                  // 1始まり
		const int32* Found = PrevRankMap.Find(Horse);
		const int32 OldRank = Found ? *Found : 0;     // 初回は 0（未登録）

		if (OldRank != NewRank)
		{
			OnRankChanged.Broadcast(Horse, NewRank, OldRank);
		}

		PrevRankMap.Add(Horse, NewRank);              // 現在順位で更新（上書き）
	}
}

// =====================================================================
// ミニマップ位置・LAP% 供給 API
// =====================================================================

FVector2D ARaceManager::GetMiniMapPosition(AHorseCharacter* Horse) const
{
	if (!Horse || !Track) { return FVector2D::ZeroVector; }
	return Track->WorldToNormalizedMiniMap(Horse->GetActorLocation());
}

float ARaceManager::GetNormalizedProgress(AHorseCharacter* Horse) const
{
	if (!Horse || !Track) { return 0.0f; }

	const float TrackLength = Track->GetTrackLength();
	if (TrackLength <= KINDA_SMALL_NUMBER) { return 0.0f; }

	// Entries を線形探索して対象馬の周回内進行距離を取得する（馬数最大 4）。
	for (const FRaceEntry& E : Entries)
	{
		if (E.Horse == Horse)
		{
			const float Normalized = E.DistanceAlongSpline / TrackLength;
			return FMath::Clamp(Normalized, 0.0f, 1.0f);
		}
	}

	return 0.0f;
}

// =====================================================================
// チェックポイント通過通知
// =====================================================================
void ARaceManager::OnCheckpointPassed(AHorseCharacter* Horse, int32 Index)
{
	if (!Horse) { return; }

	for (FRaceEntry& E : Entries)
	{
		if (E.Horse != Horse) { continue; }
		if (E.bFinished) { return; }

		const int32 LastIdx = E.LastCheckpointIndex;

		// 新設計: ラップ加算の権威は Goal 通過イベント(OnGoalCrossed)に統合済み。
		// Goal を踏むと LastCheckpointIndex は -1 にリセットされるため、
		// 「最終 CP -> 開始 CP=0」の特例は不要（Goal 未通過のまま CP0 を再度踏むと
		//  ショートカット扱いで進行を進めない）。
		// 直前 CP +1 と一致した場合のみ進行する単純ロジックに統一。
		if (Index == LastIdx + 1)
		{
			E.LastCheckpointIndex = Index;
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Emerald,
					FString::Printf(TEXT("[CP] %s passed CP %d/%d (advanced)"),
						*Horse->GetName(), Index, Checkpoints.Num() - 1));
			}
		}
		else
		{
			// スキップ / 逆走 / 同じ CP の再踏みは無視（参考ログ）
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Silver,
					FString::Printf(TEXT("[CP] %s hit CP %d but expected %d (ignored)"),
						*Horse->GetName(), Index, LastIdx + 1));
			}
		}
		return;
	}
}

// =====================================================================
// ResetToTrack 通知
// =====================================================================
void ARaceManager::MarkJustReset(AHorseCharacter* Horse)
{
	if (!Horse) { return; }
	if (!Track) { return; }
	USplineComponent* SplineComp = Track->GetSpline();
	if (!SplineComp) { return; }

	for (FRaceEntry& E : Entries)
	{
		if (E.Horse != Horse) { continue; }
		if (E.bFinished) { return; }

		E.bJustReset = true;
		// LastInputKey を Reset 後の新位置で再計算しておく（境界誤判定の即時抑止）
		const FVector WorldLoc = Horse->GetActorLocation();
		E.LastInputKey = SplineComp->FindInputKeyClosestToWorldLocation(WorldLoc);
		return;
	}
}

// =====================================================================
// Goal 通過通知（AGoalTrigger から呼ばれる）
//   - Checkpoint 全通過済みを確認した上で LapsCompleted を加算
//   - 目標周回数に到達したら NotifyHorseFinished でゴール確定処理を行う
// =====================================================================
void ARaceManager::OnGoalCrossed(AHorseCharacter* Horse)
{
	if (!Horse) { return; }

	if (RaceState != ERaceState::Running)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Orange,
				FString::Printf(TEXT("[Race] OnGoalCrossed but RaceState=%d (not Running) - ignored"),
					static_cast<int32>(RaceState)));
		}
		return;
	}

	for (FRaceEntry& E : Entries)
	{
		if (E.Horse != Horse) { continue; }
		if (E.bFinished) { return; }

		// Checkpoint が配置されている場合は全通過済みを必須とする（ショートカット防止）
		const bool bHasCheckpoints = (Checkpoints.Num() > 0);
		const bool bAllCheckpointsPassed = !bHasCheckpoints
			|| (E.LastCheckpointIndex == Checkpoints.Num() - 1);
		if (!bAllCheckpointsPassed)
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Orange,
					FString::Printf(TEXT("[Race] %s reached goal but missed checkpoints (last=%d, need=%d)"),
						*Horse->GetName(), E.LastCheckpointIndex, Checkpoints.Num() - 1));
			}
			return;
		}

		// ラップ加算
		E.LapsCompleted++;
		E.LastCheckpointIndex = -1;

		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		const float ThisLap = Now - E.LastLapTime;
		if (ThisLap > 0.0f)
		{
			E.LapTimes.Add(ThisLap);
			if (E.BestLapTime <= 0.0f || ThisLap < E.BestLapTime)
			{
				E.BestLapTime = ThisLap;
				// M6: ベストラップ更新時のみ、現在の Recorder バッファを BestLapFrames へ確定する。
				CommitBestLapGhost(Horse);
			}
		}
		E.LastLapTime = Now;

		// M6: 記録対象馬は次ラップ用に Recorder をリセット＆再記録開始する。
		if (Horse == RecordingHorse)
		{
			if (UGhostRecorder* Rec = Horse->FindComponentByClass<UGhostRecorder>())
			{
				Rec->StartRecord();
			}
		}

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan,
				FString::Printf(TEXT("[Race] %s Lap=%d/%d (%.2fs)"),
					*Horse->GetName(), E.LapsCompleted, TargetLaps, ThisLap));
		}

		// 目標周回到達でゴール確定
		if (E.LapsCompleted >= TargetLaps)
		{
			NotifyHorseFinished(Horse);
		}
		return;
	}
}

void ARaceManager::NotifyHorseFinished(AHorseCharacter* Horse)
{
	if (!Horse) { return; }

	for (FRaceEntry& E : Entries)
	{
		if (E.Horse != Horse) { continue; }
		if (E.bFinished) { return; } // 二重ゴール防止

		// 目標周回数を満たしているか確認
		if (E.LapsCompleted < TargetLaps) { return; }

		E.bFinished = true;
		E.FinishRank = NextFinishRank++;
		E.FinishTime = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f) - RaceStartTime;

		if (GEngine)
		{
			const FString Msg = FString::Printf(
				TEXT("[Race] %s GOAL! Rank=%d Time=%.2fs"),
				*Horse->GetName(), E.FinishRank, E.FinishTime);
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, Msg);
		}

		UpdateRanking();

		// 全 Entry がゴール済みになったら Finished へ遷移
		bool bAllFinished = (Entries.Num() > 0);
		for (const FRaceEntry& Check : Entries)
		{
			if (!Check.bFinished) { bAllFinished = false; break; }
		}
		if (bAllFinished)
		{
			SetState(ERaceState::Finished);
		}
		return;
	}
}

// =====================================================================
// 指定馬の順位を取得
// =====================================================================
int32 ARaceManager::GetRankOf(AHorseCharacter* Horse) const
{
	if (!Horse) { return 0; }
	for (int32 i = 0; i < Ranking.Num(); ++i)
	{
		if (Ranking[i].Horse == Horse)
		{
			return i + 1;
		}
	}
	return 0;
}

// =====================================================================
// デバッグ UI
// =====================================================================
void ARaceManager::DrawDebugUI() const
{
	if (!GEngine) { return; }

	const int32 BaseKey = static_cast<int32>(GetUniqueID()) + 1000;

	// 状態名
	const TCHAR* StateStr = TEXT("?");
	switch (RaceState)
	{
	case ERaceState::Pregame:   StateStr = TEXT("Pregame");   break;
	case ERaceState::Countdown: StateStr = TEXT("Countdown"); break;
	case ERaceState::Running:   StateStr = TEXT("Running");   break;
	case ERaceState::Finished:  StateStr = TEXT("Finished");  break;
	}

	// ヘッダ
	GEngine->AddOnScreenDebugMessage(BaseKey, 0.0f, FColor::White,
		FString::Printf(TEXT("[Race] State=%s Entries=%d TrackLength=%.0f TargetLaps=%d"),
			StateStr,
			Ranking.Num(),
			Track ? Track->GetTrackLength() : 0.0f,
			TargetLaps));

	// 各馬の順位を表示
	for (int32 i = 0; i < Ranking.Num(); ++i)
	{
		const FRaceEntry& E = Ranking[i];
		if (!E.Horse) { continue; }

		const FColor Col = E.bFinished ? FColor::Yellow : FColor::White;
		FString Line;
		if (E.bFinished)
		{
			Line = FString::Printf(TEXT("  #%d %s [GOAL %.2fs] Laps=%d Best=%.2fs"),
				i + 1, *E.Horse->GetName(), E.FinishTime, E.LapsCompleted, E.BestLapTime);
		}
		else
		{
			// 逆走検知フラグがある場合は警告を追加
			FString WrongWayStr = E.Horse->IsWrongWay() ? TEXT(" (WRONG WAY!)") : TEXT("");

			Line = FString::Printf(TEXT("  #%d %s  Lap %d/%d (Total: %.0f cm) Laps#=%d Best=%.2fs%s"),
				i + 1, *E.Horse->GetName(), E.LapsCompleted + 1, TargetLaps, E.TotalDistance,
				E.LapTimes.Num(), E.BestLapTime, *WrongWayStr);
		}
		GEngine->AddOnScreenDebugMessage(BaseKey + 1 + i, 0.0f, Col, Line);
	}
}

// =====================================================================
// M6: ゴースト記録・再生
// =====================================================================

void ARaceManager::BeginGhostRecording()
{
	// Entries を走査し、最初の非 AI 馬を記録対象とする。
	RecordingHorse = nullptr;
	for (const FRaceEntry& E : Entries)
	{
		if (E.Horse && !E.Horse->IsAIControlled())
		{
			RecordingHorse = E.Horse;
			break;
		}
	}

	if (!RecordingHorse) { return; }

	if (UGhostRecorder* Rec = RecordingHorse->FindComponentByClass<UGhostRecorder>())
	{
		Rec->StartRecord();
	}
}

void ARaceManager::CommitBestLapGhost(AHorseCharacter* Horse)
{
	// 記録対象のラップのみ確定する。
	if (!Horse || Horse != RecordingHorse) { return; }

	if (UGhostRecorder* Rec = Horse->FindComponentByClass<UGhostRecorder>())
	{
		BestLapFrames = Rec->GetFrames();
	}
}

void ARaceManager::SpawnAndPlayGhost()
{
	// 前レースのベストが無ければ再生しない。
	if (BestLapFrames.Num() < 2) { return; }

	UWorld* World = GetWorld();
	if (!World) { return; }

	// 前回ゴーストを破棄してから 1 体だけ生成する（多重生成防止）。
	if (ActiveGhost)
	{
		ActiveGhost->Destroy();
		ActiveGhost = nullptr;
	}

	TSubclassOf<AGhostPlayer> Cls = GhostPlayerClass;
	if (!Cls)
	{
		Cls = AGhostPlayer::StaticClass();
	}
	const FVector Loc = BestLapFrames[0].Location;
	const FRotator Rot = BestLapFrames[0].Rotation;

	ActiveGhost = World->SpawnActor<AGhostPlayer>(Cls, Loc, Rot);
	if (ActiveGhost)
	{
		ActiveGhost->PlayFrames(BestLapFrames);
	}
}
