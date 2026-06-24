#include "CheckpointActor.h"
#include "RaceManager.h"
#include "HorseCharacter.h"
#include "Components/BoxComponent.h"
#include "Kismet/GameplayStatics.h"

// =====================================================================
// コンストラクタ
// =====================================================================
ACheckpointActor::ACheckpointActor()
{
	PrimaryActorTick.bCanEverTick = false;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->InitBoxExtent(FVector(100.0f, 500.0f, 200.0f));
	TriggerBox->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	TriggerBox->SetGenerateOverlapEvents(true);
	RootComponent = TriggerBox;
}

// =====================================================================
// コース幅に合わせてトリガーボックスを構成（横幅連動＋底面化）
// =====================================================================
void ACheckpointActor::ConfigureForTrack(float TrackHalfWidth)
{
	if (!TriggerBox) { return; }

	FVector Ext = TriggerBox->GetUnscaledBoxExtent();
	Ext.Y = FMath::Max(1.0f, TrackHalfWidth); // 横幅をコース片側半幅に連動
	TriggerBox->SetBoxExtent(Ext);

	// 中心を半高ぶん上げ、底面を原点（路面）に揃える（埋没防止）。絶対指定で冪等。
	TriggerBox->SetRelativeLocation(FVector(0.0f, 0.0f, Ext.Z));
}

// =====================================================================
// BeginPlay
// =====================================================================
void ACheckpointActor::BeginPlay()
{
	Super::BeginPlay();

	if (TriggerBox)
	{
		TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ACheckpointActor::OnTriggerBeginOverlap);
	}

	// RaceManager 未指定ならレベルから自動検索
	if (!RaceManager)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ARaceManager::StaticClass(), Found);
		if (Found.Num() > 0)
		{
			RaceManager = Cast<ARaceManager>(Found[0]);
		}
	}
}

// =====================================================================
// オーバーラップ → RaceManager に通過通知
// =====================================================================
void ACheckpointActor::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	AHorseCharacter* Horse = Cast<AHorseCharacter>(OtherActor);
	if (!Horse) { return; }

	if (RaceManager)
	{
		RaceManager->OnCheckpointPassed(Horse, CheckpointIndex);
	}
}
