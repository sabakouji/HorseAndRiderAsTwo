#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "GhostRecorder.h"
#include "RaceManager.generated.h"

class ATrackActor;
class AHorseCharacter;
class ACheckpointActor;
class AGhostPlayer;

/**
 * ERaceState
 * レース進行状態
 *   Pregame   : 配置待ち / 入力ロック中。Tick での距離更新は行わない
 *   Countdown : カウントダウン中。入力ロック継続。CountdownDuration 秒経過で Running へ
 *   Running   : 走行中。距離更新・順位計算・ラップ計測が走る
 *   Finished  : 全馬ゴール後。Tick は停止し、Ranking のみ参照可能
 */
UENUM(BlueprintType)
enum class ERaceState : uint8
{
	Pregame    UMETA(DisplayName = "Pregame"),
	Countdown  UMETA(DisplayName = "Countdown"),
	Running    UMETA(DisplayName = "Running"),
	Finished   UMETA(DisplayName = "Finished")
};

/** レース状態遷移の汎用通知。HorseCharacter / UMG 側で購読 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRaceStateChanged, ERaceState, NewState);

/** 順位変動通知。Horse の順位が OldRank から NewRank（いずれも1始まり）へ変化したときに発火 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnRankChanged, AHorseCharacter*, Horse, int32, NewRank, int32, OldRank);

/**
 * FRaceEntry
 * レース参加馬のランタイム情報
 */
USTRUCT(BlueprintType)
struct FRaceEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<AHorseCharacter> Horse = nullptr;

	/** スプライン上の進行距離(m) */
	UPROPERTY(BlueprintReadOnly)
	float DistanceAlongSpline = 0.0f;

	/** ゴール済みか */
	UPROPERTY(BlueprintReadOnly)
	bool bFinished = false;

	/** ゴール時刻(GetTimeSeconds 基準) */
	UPROPERTY(BlueprintReadOnly)
	float FinishTime = 0.0f;

	/** ゴール順位(1始まり、未ゴールは 0) */
	UPROPERTY(BlueprintReadOnly)
	int32 FinishRank = 0;

	/** 周回数 */
	UPROPERTY(BlueprintReadOnly)
	int32 LapsCompleted = 0;

	/** 前フレームのスプライン入力キー */
	UPROPERTY(BlueprintReadOnly)
	float LastInputKey = 0.0f;

	/** 累計走行距離 */
	UPROPERTY(BlueprintReadOnly)
	float TotalDistance = 0.0f;

	/** 最後に通過したチェックポイント Index（-1 は未通過、CollectHorses/StartRace でリセット） */
	UPROPERTY(BlueprintReadOnly)
	int32 LastCheckpointIndex = -1;

	/** 直前フレームに ResetToTrack が呼ばれたか（UpdateDistances 1 回分のラップ境界判定をスキップするためのフラグ） */
	UPROPERTY(BlueprintReadOnly)
	bool bJustReset = false;

	/** 各 Lap の所要秒（Lap 完了順に追加） */
	UPROPERTY(BlueprintReadOnly)
	TArray<float> LapTimes;

	/** 最速 Lap 秒（0.0 = 未計測） */
	UPROPERTY(BlueprintReadOnly)
	float BestLapTime = 0.0f;

	/** 直前 Lap 開始時刻（ワールド秒、Lap 加算時に更新） */
	UPROPERTY(BlueprintReadOnly)
	float LastLapTime = 0.0f;
};

/**
 * ARaceManager
 * レベルに配置するレース管理アクター。
 *
 * - BeginPlay で AHorseCharacter を全自動収集
 * - Tick 毎にスプライン距離を更新し、現在順位を計算
 * - ゴール通知 (NotifyHorseFinished) を AGoalTrigger から受け取り、ゴール順位を確定
 */
UCLASS(Blueprintable)
class HORSEANDRIDERASTWO_API ARaceManager : public AInfo
{
	GENERATED_BODY()

public:
	ARaceManager();
	virtual void Tick(float DeltaTime) override;

	/**
	 * ゴール通知（旧 API、後方互換のため残置）。
	 * 周回数チェックを行わず即ゴール扱いするため、通常は OnGoalCrossed を使うこと。
	 */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void NotifyHorseFinished(AHorseCharacter* Horse);

	/**
	 * Goal ライン通過通知（AGoalTrigger から呼ばれる）。
	 * - 全 Checkpoint を順番通り通過済みであれば LapsCompleted を加算しラップ計測を行う
	 * - 加算後の LapsCompleted >= TargetLaps であればゴール確定処理を行う
	 */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void OnGoalCrossed(AHorseCharacter* Horse);

	/** 順位順にソートした参加馬一覧を取得 */
	UFUNCTION(BlueprintCallable, Category = "Race")
	TArray<FRaceEntry> GetRanking() const { return Ranking; }

	/** 指定の馬の現在順位（1始まり、見つからなければ 0） */
	UFUNCTION(BlueprintCallable, Category = "Race")
	int32 GetRankOf(AHorseCharacter* Horse) const;

	/**
	 * 指定馬のミニマップ上の正規化位置を返す（GetTrackShape2D と同一座標系）。
	 * 馬の現在ワールド位置を Track->WorldToNormalizedMiniMap() で変換する。
	 * Horse / Track が null のときは FVector2D::ZeroVector を返す。
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|UI")
	FVector2D GetMiniMapPosition(AHorseCharacter* Horse) const;

	/**
	 * 指定馬の現在周回内のスプライン進行度を 0.0〜1.0 で返す（HUD の LAP% 表示用）。
	 * DistanceAlongSpline / TrackLength。Track 未設定・馬未登録時は 0.0 を返す。
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|UI")
	float GetNormalizedProgress(AHorseCharacter* Horse) const;

	/**
	 * レース開始（Running 状態への遷移専用）。
	 * Phase 5 以降は通常 StartCountdown() 経由で呼ばれる。
	 * BP からカウントダウンをスキップして即 Running 突入させたい場合のみ直接呼ぶ。
	 */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void StartRace();

	/**
	 * カウントダウンを開始し、CountdownDuration 秒後に自動で Running へ遷移する。
	 * Pregame 状態でのみ受理する（多重呼び出しガード）。
	 */
	UFUNCTION(BlueprintCallable, Category = "Race|State")
	void StartCountdown();

	/** 現在のレース状態 */
	UPROPERTY(BlueprintReadOnly, Category = "Race|State")
	ERaceState RaceState = ERaceState::Pregame;

	/** カウントダウン秒数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|State", meta = (ClampMin = "0.0"))
	float CountdownDuration = 3.0f;

	/** 状態遷移通知（BP / UMG / HorseCharacter から購読） */
	UPROPERTY(BlueprintAssignable, Category = "Race|State")
	FOnRaceStateChanged OnRaceStateChanged;

	/** 順位変動通知（BP / HUD から購読し「順位アップ／ダウン」演出に使う副次 API） */
	UPROPERTY(BlueprintAssignable, Category = "Race|UI")
	FOnRankChanged OnRankChanged;

	/**
	 * チェックポイント通過通知（ACheckpointActor から呼ばれる）。
	 * 直前 CP +1 と一致した場合のみ LastCheckpointIndex を更新する。
	 * Index == 0 かつ LastCheckpointIndex == Checkpoints.Num()-1 の場合は
	 * 周回完了の AND 条件として UpdateDistances 側で利用される。
	 */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void OnCheckpointPassed(AHorseCharacter* Horse, int32 Index);

	/**
	 * ResetToTrack 直後に AHorseCharacter から呼ばれる通知。
	 * 次の UpdateDistances 1 回分のラップ境界判定をスキップし、
	 * LastInputKey を新位置で再計算してから境界判定を再開する。
	 */
	UFUNCTION(BlueprintCallable, Category = "Race")
	void MarkJustReset(AHorseCharacter* Horse);

protected:
	virtual void BeginPlay() override;

	/** 対象とする Track。未指定ならレベルから1個自動検索 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	TObjectPtr<ATrackActor> Track;

	/** 目標周回数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race")
	int32 TargetLaps = 3;

	/** デバッグUIに順位を表示する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Debug")
	bool bShowDebugUI = true;

	/** ラップ境界判定の上側閾値（MaxKey に対する比率、デフォルト 0.7） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Rules", meta = (ClampMin = "0.5", ClampMax = "1.0"))
	float LapBoundaryThresholdHigh = 0.7f;

	/** ラップ境界判定の下側閾値（MaxKey に対する比率、デフォルト 0.3） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Rules", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float LapBoundaryThresholdLow = 0.3f;

	/** 生成するゴースト馬の BP クラス（半透明メッシュをアサインした BP_GhostPlayer を指定） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Race|Ghost")
	TSubclassOf<AGhostPlayer> GhostPlayerClass;

private:
	/** 参加馬エントリ */
	UPROPERTY()
	TArray<FRaceEntry> Entries;

	/** レベル内のチェックポイント一覧（BeginPlay 時に CheckpointIndex 順にソート済み） */
	UPROPERTY()
	TArray<TObjectPtr<ACheckpointActor>> Checkpoints;

	/** 順位順にソートしたコピー（Tick 毎更新） */
	UPROPERTY()
	TArray<FRaceEntry> Ranking;

	/** 前 Tick の各馬順位（1始まり）。UpdateRanking 後に現在順位と比較して OnRankChanged を発火する */
	UPROPERTY()
	TMap<TObjectPtr<AHorseCharacter>, int32> PrevRankMap;

	/** 次のゴール順位（1から増加） */
	int32 NextFinishRank = 1;

	float RaceStartTime = 0.0f;

	/** Track をレベルから自動検索 */
	void ResolveTrack();

	/** AHorseCharacter を全収集して Entries を構築 */
	void CollectHorses();

	/** 各馬のスプライン距離を更新 */
	void UpdateDistances();

	/** Entries を距離順にソートして Ranking を更新 */
	void UpdateRanking();

	/** Ranking を走査し、順位が変化した馬について OnRankChanged を発火する */
	void BroadcastRankChanges();

	void DrawDebugUI() const;

	/** 状態遷移本体（旧状態と異なる場合のみ OnRaceStateChanged をブロードキャスト） */
	void SetState(ERaceState NewState);

	/** カウントダウン完了タイマーのハンドル */
	FTimerHandle CountdownTimerHandle;

	/** カウントダウン完了時の内部コールバック（StartRace を呼ぶ） */
	void OnCountdownFinished();

	// =====================================================================
	// ゴースト記録・再生（M6）
	// =====================================================================

	/** ベストラップ 1 周分の確定フレーム列（レースをまたいで保持。CollectHorses でクリアしない） */
	UPROPERTY()
	TArray<FGhostFrame> BestLapFrames;

	/** 現在 BestLapFrames を記録中／提供した馬（記録対象 Recorder の取得に使用） */
	UPROPERTY()
	TObjectPtr<AHorseCharacter> RecordingHorse;

	/** 再生中のゴーストインスタンス（次レース開始時に前回分を破棄してから 1 体生成） */
	UPROPERTY()
	TObjectPtr<AGhostPlayer> ActiveGhost;

	/** 記録対象馬の Recorder に StartRecord を発行する（Running 突入時に呼ぶ） */
	void BeginGhostRecording();

	/** ベストラップ更新時に RecordingHorse の Recorder バッファを BestLapFrames へ確定する */
	void CommitBestLapGhost(AHorseCharacter* Horse);

	/** BestLapFrames が非空なら AGhostPlayer を生成し PlayFrames する（StartRace から呼ぶ） */
	void SpawnAndPlayGhost();
};
