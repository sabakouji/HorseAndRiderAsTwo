#include "FrontEndGameMode.h"
#include "FrontEndPlayerController.h"
#include "HorseGameInstance.h"
#include "GameFramework/SpectatorPawn.h"
#include "Kismet/GameplayStatics.h"

AFrontEndGameMode::AFrontEndGameMode()
{
	// メニュー画面では操作対象キャラクタ（馬）を spawn しない。
	DefaultPawnClass = ASpectatorPawn::StaticClass();
	PlayerControllerClass = AFrontEndPlayerController::StaticClass();
}

void AFrontEndGameMode::BeginPlay()
{
	Super::BeginPlay();

	// FrontEnd Level 読み込み後、GameInstance に仕込まれた初期シーン（Title 等）を確定・通知する。
	if (UHorseGameInstance* GI = Cast<UHorseGameInstance>(UGameplayStatics::GetGameInstance(this)))
	{
		GI->ApplyPendingFrontEndScene();
	}
}
