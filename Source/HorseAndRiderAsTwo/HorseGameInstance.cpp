#include "HorseGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"

UHorseGameInstance::UHorseGameInstance()
{
}

void UHorseGameInstance::SetScene(EAppScene NewScene)
{
	if (CurrentScene == NewScene)
	{
		return;
	}

	CurrentScene = NewScene;
	OnAppSceneChanged.Broadcast(CurrentScene);
}

bool UHorseGameInstance::IsOnFrontEndLevel() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	// 現在のマップ名（接頭辞 UEDPIE_0_ 等を除去した純粋名）と FrontEndLevelName の短縮名を比較する。
	const FString CurrentMapName = World->GetMapName();
	const FString FrontEndShort = FPackageName::GetShortName(FrontEndLevelName.ToString());
	return CurrentMapName.Contains(FrontEndShort);
}

void UHorseGameInstance::GoToTitle()
{
	if (IsOnFrontEndLevel())
	{
		// 既に FrontEnd Level 上（ModeSelect 等）なら、ウィジェット差し替えのみ。
		SetScene(EAppScene::Title);
		return;
	}

	// レース Level など別 Level からの場合は FrontEnd を読み込み、読み込み後に Title を表示する。
	ReturnToTitle();
}

void UHorseGameInstance::GoToModeSelect()
{
	// ソロ／マルチ分岐画面。同一 Level 内の始点変更＋ウィジェット差し替えのみ。
	SetScene(EAppScene::ModeSelect);
}

void UHorseGameInstance::GoToSolo()
{
	// ソロは部屋を作らず即レース開始。
	bIsMultiplayer = false;
	bIsHost = false;
	GoToRace();
}

void UHorseGameInstance::GoToMultiRoom()
{
	// マルチ部屋（ホスト作成＋パスワード）。セッション本体は M7、ここでは入力窓と遷移のみ。
	bIsMultiplayer = true;
	bIsHost = true;
	SetScene(EAppScene::MultiRoom);
}

void UHorseGameInstance::GoToRace()
{
	SetScene(EAppScene::Race);

	// メニューの UIOnly 入力モードは GameViewportClient に残存し、
	// レースレベルでもゲーム入力が無視されたままになるため、出る前に GameOnly へ戻す。
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}

	UGameplayStatics::OpenLevel(this, RaceLevelName);
}

void UHorseGameInstance::GoToResult()
{
	SetScene(EAppScene::Result);
}

void UHorseGameInstance::ReturnToTitle()
{
	// FrontEnd Level 読み込み後に Title を表示させるため、先に意図を仕込む。
	PendingFrontEndScene = EAppScene::Title;
	UGameplayStatics::OpenLevel(this, FrontEndLevelName);
}

void UHorseGameInstance::ReturnToModeSelect()
{
	// FrontEnd Level 読み込み後に ModeSelect を表示させるため、先に意図を仕込む。
	PendingFrontEndScene = EAppScene::ModeSelect;
	UGameplayStatics::OpenLevel(this, FrontEndLevelName);
}

void UHorseGameInstance::ApplyPendingFrontEndScene()
{
	// CurrentScene は同期で確定する（Widget の Construct で GetCurrentScene() が正しく読めるように）。
	CurrentScene = PendingFrontEndScene;

	// 通知は次フレームへ遅延する。
	// Level 読み込み直後（GameMode::BeginPlay）の時点では FrontEnd の WBP_FrontEndRoot が
	// まだ生成・バインドされておらず、即時 Broadcast だと取りこぼす
	// （Title のまま残り ModeSelect が出ない）。次フレームなら確実に届く。
	if (UWorld* World = GetWorld())
	{
		TWeakObjectPtr<UHorseGameInstance> WeakThis(this);
		World->GetTimerManager().SetTimerForNextTick([WeakThis]()
		{
			if (UHorseGameInstance* Self = WeakThis.Get())
			{
				Self->OnAppSceneChanged.Broadcast(Self->CurrentScene);
			}
		});
	}
	else
	{
		OnAppSceneChanged.Broadcast(CurrentScene);
	}
}
