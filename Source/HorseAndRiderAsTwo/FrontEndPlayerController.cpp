#include "FrontEndPlayerController.h"
#include "Camera/CameraActor.h"
#include "Kismet/GameplayStatics.h"

AFrontEndPlayerController::AFrontEndPlayerController()
{
	bShowMouseCursor = true;
	PrimaryActorTick.bCanEverTick = true;
}

void AFrontEndPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// メニュー操作のため UI 入力モードへ。フォーカス対象ウィジェットは BP 側で生成・指定する。
	bShowMouseCursor = true;
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	// 基準位置の収集とビュー対象（メニューカメラ）の確定。
	SetupMenuCamera();

	// シーン変更を購読し、位置の移動目標だけ更新する（角度・ビュー対象は変えない）。
	if (UHorseGameInstance* GI = Cast<UHorseGameInstance>(UGameplayStatics::GetGameInstance(this)))
	{
		GI->OnAppSceneChanged.AddDynamic(this, &AFrontEndPlayerController::HandleAppSceneChanged);
		FocusScene(GI->GetCurrentScene());
	}
}

void AFrontEndPlayerController::SetupMenuCamera()
{
	// Title / ModeSelect のタグ付き CameraActor から「位置」を収集する。
	// 角度は使わない（メニューカメラは Title の角度で固定し、位置だけ動かすため）。
	const EAppScene Scenes[] = { EAppScene::Title, EAppScene::ModeSelect };
	ACameraActor* TitleCamera = nullptr;

	for (EAppScene Scene : Scenes)
	{
		const FName Tag = SceneToCameraTag(Scene);
		if (Tag.IsNone())
		{
			continue;
		}

		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClassWithTag(this, ACameraActor::StaticClass(), Tag, Found);
		if (Found.Num() == 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[FrontEnd] タグ '%s' を持つ CameraActor が見つかりません。"), *Tag.ToString());
			continue;
		}

		if (ACameraActor* Cam = Cast<ACameraActor>(Found[0]))
		{
			SceneCameraLocations.Add(Scene, Cam->GetActorLocation());
			if (Scene == EAppScene::Title)
			{
				TitleCamera = Cam;
			}
		}
	}

	// ビュー対象は Title カメラ 1 台に固定。以後ビュー対象は切り替えず、位置のみ Tick で動かす。
	// これにより 2 台間ブレンドが起きず、サイドの黒帯（アスペクト/FOV ブレンド）が出ない。
	if (TitleCamera)
	{
		MenuCamera = TitleCamera;
		DesiredCameraLocation = MenuCamera->GetActorLocation();
		SetViewTarget(MenuCamera); // ブレンドなしで即固定
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[FrontEnd] FE_Title カメラが無いためメニューカメラを設定できません。"));
	}
}

void AFrontEndPlayerController::HandleAppSceneChanged(EAppScene NewScene)
{
	FocusScene(NewScene);
}

void AFrontEndPlayerController::FocusScene(EAppScene Scene)
{
	// 基準位置があれば移動目標を更新する。
	// MultiRoom / Race / Result は基準位置を持たないため、直前位置（＝ModeSelect）を維持する。
	if (const FVector* Loc = SceneCameraLocations.Find(Scene))
	{
		DesiredCameraLocation = *Loc;
	}
}

void AFrontEndPlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (MenuCamera)
	{
		// 位置のみ滑らかに補間。角度は authored のまま変更しない（要望: 角度は切替えず移動のみ）。
		const FVector Current = MenuCamera->GetActorLocation();
		const FVector NewLoc = FMath::VInterpTo(Current, DesiredCameraLocation, DeltaTime, CameraMoveSpeed);
		MenuCamera->SetActorLocation(NewLoc);
	}
}

FName AFrontEndPlayerController::SceneToCameraTag(EAppScene Scene)
{
	switch (Scene)
	{
	case EAppScene::Title:      return FName(TEXT("FE_Title"));
	case EAppScene::ModeSelect: return FName(TEXT("FE_ModeSelect"));
	// MultiRoom / Race / Result は基準位置なし（MultiRoom は ModeSelect の構図を維持）。
	default:                    return NAME_None;
	}
}
