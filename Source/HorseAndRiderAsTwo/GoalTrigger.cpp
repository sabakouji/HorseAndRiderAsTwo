#include "GoalTrigger.h"
#include "RaceManager.h"
#include "HorseCharacter.h"
#include "Jockey.h"
#include "Components/BoxComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"

// =====================================================================
// コンストラクタ
// =====================================================================
AGoalTrigger::AGoalTrigger()
{
	PrimaryActorTick.bCanEverTick = false;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->InitBoxExtent(FVector(100.0f, 500.0f, 200.0f));
	TriggerBox->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	TriggerBox->SetGenerateOverlapEvents(true);
	RootComponent = TriggerBox;
}

// =====================================================================
// BeginPlay
// =====================================================================
void AGoalTrigger::BeginPlay()
{
	Super::BeginPlay();

	if (TriggerBox)
	{
		TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &AGoalTrigger::OnTriggerBeginOverlap);
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
// オーバーラップ → 騎乗中ならゴール通知
// =====================================================================
void AGoalTrigger::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	// 進入ログ（誰が触れたか）
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow,
			FString::Printf(TEXT("[Goal] BeginOverlap: %s"),
				OtherActor ? *OtherActor->GetName() : TEXT("null")));
	}

	AHorseCharacter* Horse = Cast<AHorseCharacter>(OtherActor);
	if (!Horse)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Silver,
				FString::Printf(TEXT("[Goal] %s is not a Horse - ignored"),
					OtherActor ? *OtherActor->GetName() : TEXT("null")));
		}
		return;
	}

	// 騎乗中ジョッキーが存在しかつ IsRiding でなければゴール無効
	AJockey* Jockey = Horse->GetCurrentJockey();
	if (!Jockey || !Jockey->IsRiding())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Orange,
				FString::Printf(TEXT("[Goal] %s rejected - Jockey=%s IsRiding=%d"),
					*Horse->GetName(),
					Jockey ? *Jockey->GetName() : TEXT("null"),
					Jockey ? (Jockey->IsRiding() ? 1 : 0) : 0));
		}
		return;
	}

	// 逆走中の馬がゴールを踏んでもゴール扱いにしない
	if (Horse->IsWrongWay())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Orange,
				FString::Printf(TEXT("[Goal] %s wrong way - goal rejected"), *Horse->GetName()));
		}
		return;
	}

	if (!RaceManager)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
				FString::Printf(TEXT("[Goal] %s reached goal but RaceManager is null!"),
					*Horse->GetName()));
		}
		return;
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green,
			FString::Printf(TEXT("[Goal] %s -> OnGoalCrossed"), *Horse->GetName()));
	}

	// ラップ加算の権威は RaceManager::OnGoalCrossed に統合。
	// 周回数到達時に内部で NotifyHorseFinished 相当のゴール確定処理が走る。
	RaceManager->OnGoalCrossed(Horse);
}
