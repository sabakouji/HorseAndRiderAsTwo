#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FrontEndPlayerController.generated.h"

/**
 * AFrontEndPlayerController
 * FrontEnd Level（タイトル／モード選択）用の PlayerController。
 * メニュー操作のため、BeginPlay でマウスカーソルを表示し UI 入力モードに設定する。
 * 実際のウィジェット生成・配置は BP（見た目担当）側で行う。
 */
UCLASS()
class HORSEANDRIDERASTWO_API AFrontEndPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AFrontEndPlayerController();

protected:
	virtual void BeginPlay() override;
};
