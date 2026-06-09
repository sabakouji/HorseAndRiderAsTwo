#include "HorseGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

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
	SetScene(EAppScene::ModeSelect);
}

void UHorseGameInstance::GoToRace()
{
	SetScene(EAppScene::Race);
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

void UHorseGameInstance::ApplyPendingFrontEndScene()
{
	// SetScene は差分のみ通知するため、確実に通知が走るよう一旦内部状態を更新してから発火する。
	CurrentScene = PendingFrontEndScene;
	OnAppSceneChanged.Broadcast(CurrentScene);
}
