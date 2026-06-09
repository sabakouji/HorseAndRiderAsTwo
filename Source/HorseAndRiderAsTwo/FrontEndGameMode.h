#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FrontEndGameMode.generated.h"

/**
 * AFrontEndGameMode
 * FrontEnd Level（タイトル／モード選択／リザルト遷移元）用の GameMode。
 *
 * - 馬を spawn しないよう DefaultPawnClass に ASpectatorPawn を設定する。
 * - PlayerControllerClass に AFrontEndPlayerController を設定し、メニュー操作の入力モードにする。
 * - BeginPlay で UHorseGameInstance の PendingFrontEndScene を適用し、初期シーン（通常 Title）を確定する。
 */
UCLASS()
class HORSEANDRIDERASTWO_API AFrontEndGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFrontEndGameMode();

protected:
	virtual void BeginPlay() override;
};
