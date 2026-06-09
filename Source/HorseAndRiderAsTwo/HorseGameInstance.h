#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "HorseGameInstance.generated.h"

/**
 * EAppScene
 * アプリ全体のシーン状態。
 *   Title      : タイトル画面（FrontEnd Level 上のウィジェット）
 *   ModeSelect : ゲームモード選択＝ネットワークルーム画面（FrontEnd Level 上のウィジェット）
 *   Race       : メインゲーム（レース Level / Main.umap）
 *   Result     : レースリザルト（レース Level 上のウィジェット）
 *
 * Title / ModeSelect は同一 FrontEnd Level 上でウィジェットを差し替えるだけ（レベルロード無し）。
 * Race への遷移は OpenLevel を伴う。Result はレース Level 上に重ねて表示する。
 */
UENUM(BlueprintType)
enum class EAppScene : uint8
{
	Title      UMETA(DisplayName = "Title"),
	ModeSelect UMETA(DisplayName = "ModeSelect"),
	Race       UMETA(DisplayName = "Race"),
	Result     UMETA(DisplayName = "Result")
};

/** シーン状態が変化したときの通知。FrontEnd 側 BP がウィジェット差し替えに使用する */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAppSceneChanged, EAppScene, NewScene);

/**
 * UHorseGameInstance
 * シーン間で持ち越すデータの器と、全シーン遷移の入口 API を提供する GameInstance。
 *
 * - レベルロードをまたいで生存するため、選択人数・ルーム情報（パスワード等）を保持する。
 * - GoTo* 系 API がシーン遷移の唯一の入口。BP のメニューボタンはここを呼ぶ。
 * - 同一 Level 内の遷移（Title <-> ModeSelect, Race -> Result）は OnAppSceneChanged の発火のみ、
 *   Level をまたぐ遷移（-> Race, -> Title）は OpenLevel を行う。
 *
 * 注: ネットワークのセッション作成・パスワード認証・接続本体は M7 で実装する。
 *     本クラスは今回その入力データ（RoomPassword 等）を保持する窓口までを担う。
 */
UCLASS()
class HORSEANDRIDERASTWO_API UHorseGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UHorseGameInstance();

	// =====================================================================
	// シーン遷移 API
	// =====================================================================

	/**
	 * タイトルへ遷移する。
	 * Race / Result（レース Level）から呼ばれた場合は FrontEnd Level を OpenLevel してから
	 * Title を表示する。FrontEnd Level 内（ModeSelect）からの場合はシーン通知のみ行う。
	 */
	UFUNCTION(BlueprintCallable, Category = "Scene")
	void GoToTitle();

	/** モード選択（ネットワークルーム）画面へ遷移する。FrontEnd Level 内のウィジェット差し替え */
	UFUNCTION(BlueprintCallable, Category = "Scene")
	void GoToModeSelect();

	/** レース Level（RaceLevelName）を OpenLevel し、シーンを Race にする */
	UFUNCTION(BlueprintCallable, Category = "Scene")
	void GoToRace();

	/** レース Level 上でリザルトを表示する（シーン通知のみ、Level ロード無し） */
	UFUNCTION(BlueprintCallable, Category = "Scene")
	void GoToResult();

	/** リザルト等からタイトルへ戻る（FrontEnd Level を OpenLevel し、シーンを Title にする） */
	UFUNCTION(BlueprintCallable, Category = "Scene")
	void ReturnToTitle();

	/** 現在のシーンを取得する */
	UFUNCTION(BlueprintCallable, Category = "Scene")
	EAppScene GetCurrentScene() const { return CurrentScene; }

	/** シーン変更通知。FrontEnd / レース Level の BP が購読してウィジェットを差し替える */
	UPROPERTY(BlueprintAssignable, Category = "Scene")
	FOnAppSceneChanged OnAppSceneChanged;

	// =====================================================================
	// 永続データ（シーン間で保持）
	// =====================================================================

	/** 参加人数（仮 4 人。将来 16 人まで拡張予定）。M7 で spawn 数へ反映する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room", meta = (ClampMin = "1", ClampMax = "16"))
	int32 SelectedPlayerCount = 4;

	/** ルームのパスワード（ホスト作成／参加時に入力）。認証本体は M7 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room")
	FString RoomPassword;

	/** 自分がホストか（ルーム作成＝true、参加＝false）。接続本体は M7 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room")
	bool bIsHost = false;

	/** FrontEnd（タイトル・モード選択・リザルト遷移元）Level のパス */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	FName FrontEndLevelName = FName(TEXT("/Game/FrontEnd"));

	/** レース（メインゲーム）Level のパス */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene")
	FName RaceLevelName = FName(TEXT("/Game/Main"));

	/**
	 * FrontEnd Level 読み込み直後に表示したいシーン。
	 * ReturnToTitle / GoToTitle が OpenLevel する際に Title を仕込み、
	 * FrontEnd 側 GameMode が読み込み後にこの値で初期シーンを確定する。
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Scene")
	EAppScene PendingFrontEndScene = EAppScene::Title;

	/**
	 * FrontEnd Level の GameMode から BeginPlay 時に呼ばれ、
	 * PendingFrontEndScene を現在シーンとして確定・通知する。
	 */
	UFUNCTION(BlueprintCallable, Category = "Scene")
	void ApplyPendingFrontEndScene();

private:
	/** 現在のシーン */
	UPROPERTY()
	EAppScene CurrentScene = EAppScene::Title;

	/** 現在のワールドが FrontEnd Level かどうかを Level 名で判定する */
	bool IsOnFrontEndLevel() const;

	/** CurrentScene を更新し、変化した場合のみ OnAppSceneChanged を発火する */
	void SetScene(EAppScene NewScene);
};
