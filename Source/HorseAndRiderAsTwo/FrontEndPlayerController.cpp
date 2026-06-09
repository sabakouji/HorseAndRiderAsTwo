#include "FrontEndPlayerController.h"

AFrontEndPlayerController::AFrontEndPlayerController()
{
	bShowMouseCursor = true;
}

void AFrontEndPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// メニュー操作のため UI 入力モードへ。フォーカス対象ウィジェットは BP 側で生成・指定する。
	bShowMouseCursor = true;
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
}
