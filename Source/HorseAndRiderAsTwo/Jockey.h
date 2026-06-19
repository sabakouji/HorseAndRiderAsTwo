#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Jockey.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;
class UPhysicsConstraintComponent;
class AHorseCharacter;

/**
 * AJockey
 * 馬に騎乗するジョッキー。
 * - 馬にアタッチされて騎乗状態
 * - 衝撃を蓄積し、閾値を超えるとラグドール化（落馬）
 * - 蓄積値は時間経過で減衰
 * - 落馬後は Help 状態（自動復帰しない）
 */
UCLASS()
class HORSEANDRIDERASTWO_API AJockey : public AActor
{
	GENERATED_BODY()

public:
	AJockey();
	virtual void Tick(float DeltaTime) override;

	/** 馬にアタッチして騎乗状態にする */
	UFUNCTION(BlueprintCallable, Category = "Jockey")
	void AttachToHorse(AHorseCharacter* Horse);

	/**
	 * ChildActorComponent から生成された場合の騎乗セットアップ。
	 * 既にソケットへアタッチ済みのため AttachToComponent は呼ばず、
	 * 状態フラグ・コリジョン・部分ラグドールのみ適用する。
	 */
	UFUNCTION(BlueprintCallable, Category = "Jockey")
	void SetupAsChildActor(AHorseCharacter* Horse);

	/** 馬から切り離して落馬状態にする */
	UFUNCTION(BlueprintCallable, Category = "Jockey")
	void DetachFromHorse();

	/**
	 * 落馬状態から復帰し、馬に再騎乗する。
	 * メッシュの物理シミュレーションを停止し、状態をリセットしてからアタッチする。
	 */
	UFUNCTION(BlueprintCallable, Category = "Jockey")
	void WakeUpAndRide(AHorseCharacter* Horse);

	/**
	 * 外部（馬経由）から衝撃を受け取る。
	 * 馬の OnHit から呼ばれることで、ジョッキーにも衝撃を伝搬する。
	 */
	UFUNCTION(BlueprintCallable, Category = "Jockey")
	void ReceiveExternalImpact(float ImpactMagnitude);

	/**
	 * 方向付きで外部衝撃を受け取る。落馬時、衝撃を受けた方向（ImpactWorldDir）へ吹っ飛ぶ。
	 * ImpactWorldDir は「衝撃が押し出す向き」（攻撃側→被弾側など）のワールド方向。
	 */
	void ReceiveExternalImpact(float ImpactMagnitude, const FVector& ImpactWorldDir);

	/** 騎乗中か */
	UFUNCTION(BlueprintCallable, Category = "Jockey")
	bool IsRiding() const { return bIsRiding; }

	/**
	 * デバッグ用: 蓄積値や状態に関わらず即座にラグドール化（落馬）させる。
	 * BP の入力イベントから呼び出して挙動確認に使用する。
	 */
	UFUNCTION(BlueprintCallable, Category = "Jockey|Debug")
	void ForceRagdoll();

	/** 落馬してヘルプ状態か */
	UFUNCTION(BlueprintCallable, Category = "Jockey")
	bool IsKnockedOut() const { return bIsKnockedOut; }

	/**
	 * プレイヤーの急ブレーキ等により、ジョッキーを放物線で射出する。
	 * - 馬から detach
	 * - ラグドール化（メッシュ全体に物理を有効化）
	 * - InitialVelocity をメッシュに付与
	 * - bLaunchedAsProjectile を立て、着弾 (OnMeshHit) で爆発を発火
	 */
	UFUNCTION(BlueprintCallable, Category = "Jockey|Eject")
	void LaunchAsProjectile(const FVector& InitialVelocity);

	/**
	 * 着弾点で範囲内のすべての AJockey の衝撃蓄積をマックス化して落馬させる。
	 * 射出した本人 (this) も含めて影響する。
	 */
	UFUNCTION(BlueprintCallable, Category = "Jockey|Eject")
	void TriggerExplosion(const FVector& Location);

	/**
	 * 振り回しモードへ移行する。
	 * - 騎乗中ならソケットから detach
	 * - メッシュ全体をフル物理 (ラグドール) に切替
	 * - 縄のハンドグリップは維持（馬の動きで縄経由で振り回される）
	 * - hand_l 骨を SwingAnchor 周辺へスプリング拘束
	 *
	 * @param InAnchorComp 振り回しの拘束アンカー (例: 馬メッシュ)
	 * @param InAnchorSocket アンカーソケット名 (例: BridleSocket_L)
	 */
	UFUNCTION(BlueprintCallable, Category = "Jockey|Swing")
	void EnterSwingMode(USceneComponent* InAnchorComp, FName InAnchorSocket);

	/** 振り回しモードを抜けて通常ラグドール (KnockedOut) 状態へ */
	UFUNCTION(BlueprintCallable, Category = "Jockey|Swing")
	void ExitSwingMode();

	UFUNCTION(BlueprintCallable, Category = "Jockey|Swing")
	bool IsSwinging() const { return bIsSwinging; }

	/**
	 * 鞍ラグドール騎乗を開始する。
	 * - ジョッキーメッシュを完全ラグドール化（全身物理）
	 * - 鞍ソケット位置へ PhysicsConstraint（SetupSeatConstraint）で繋ぎ留める
	 * - 馬メッシュと物理接触するコリジョンを有効化
	 * - 手綱グリップを維持
	 * @param Horse 騎乗先の馬
	 * @param bAttachActorToSeat true でアクタールートを鞍ソケットへ再アタッチ（再騎乗時）
	 */
	void BeginRideAsRagdoll(AHorseCharacter* Horse, bool bAttachActorToSeat);

	/**
	 * ヘッドバンキング攻撃: 上体ボーンへ横方向インパルスを与えて振る。
	 * @param Direction 振る向き（+1=右 / -1=左）
	 */
	UFUNCTION(BlueprintCallable, Category = "Jockey|Attack")
	void DoHeadbang(float Direction);

	/**
	 * 攻撃判定の有効/無効を設定する。
	 * 馬が強く傾いている間 true にすると、その間の相手ジョッキーとの接触を攻撃として扱う。
	 */
	UFUNCTION(BlueprintCallable, Category = "Jockey|Attack")
	void SetHeadbangActive(bool bActive);

	/** ジョッキー実体（pelvis ボーン）のワールド位置。ピックアップ距離判定に使用 */
	UFUNCTION(BlueprintCallable, Category = "Jockey")
	FVector GetBodyWorldLocation() const;

protected:
	virtual void BeginPlay() override;

	// =====================================================================
	// コンポーネント
	// =====================================================================

	/** ルート（衝突用カプセル） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCapsuleComponent> CapsuleComp;

	/** スケルタルメッシュ（ラグドール用） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> MeshComp;

	/**
	 * 初期（騎乗）状態のメッシュ相対トランスフォーム。BeginPlay でキャッシュする。
	 * 再騎乗（ピックアップ）時にラグドールで散らばったメッシュをこの姿勢へ戻し、
	 * 初期と同じ着座位置・ポーズから着座ラグドールを再開するために使用する。
	 */
	FTransform InitialMeshRelativeTransform = FTransform::Identity;

	// =====================================================================
	// アタッチ設定
	// =====================================================================

	/** 馬メッシュ上のアタッチ先ソケット名 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Attach")
	FName AttachSocketName = FName("JockeySocket");

	/**
	 * 騎乗時にソケット基準で適用する相対回転。
	 * ソケットの向きが斜めでもこの値でジョッキーの姿勢を補正できる。
	 * 例: ソケットが横倒し (Yaw 90°) のときは (0, -90, 0) を設定。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Attach")
	FRotator AttachRelativeRotation = FRotator::ZeroRotator;

	/** 騎乗時の相対位置オフセット（鞍の高さ調整など） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Attach")
	FVector AttachRelativeLocation = FVector::ZeroVector;

	// =====================================================================
	// 衝撃蓄積システム
	// =====================================================================

	/** 落馬する衝撃蓄積閾値 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Impact",
		meta = (ClampMin = "0.0"))
	float ImpactThreshold = 5000.0f;

	/** 蓄積値の毎秒減衰量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Impact",
		meta = (ClampMin = "0.0"))
	float ImpactDecayRate = 1000.0f;

	/** 落馬時にメッシュに与えるインパルス強度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Impact",
		meta = (ClampMin = "0.0"))
	float KnockoffImpulseStrength = 2000.0f;

	/** 馬経由で受けた衝撃の伝搬倍率（1.0 = そのまま） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Impact",
		meta = (ClampMin = "0.0"))
	float HorseImpactTransferRatio = 1.0f;

	/**
	 * 衝撃計上の不応期(秒)。1回の衝突で複数フレーム・複数経路から多重計上されるのを防ぐ。
	 * この秒数内に届いた追加の衝撃は無視し、「1回の衝突につき1回」だけ蓄積する。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Impact",
		meta = (ClampMin = "0.0"))
	float ImpactHitRefractory = 0.3f;

	// =====================================================================
	// 騎乗中の部分ラグドール（TABS風フニャフニャ）
	// =====================================================================

	/** 騎乗中フニャフニャを有効化する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|RidingPhysics")
	bool bEnableRidingPhysicsBlend = true;

	/**
	 * 騎乗中の物理ブレンド比率。
	 * 0.0 = 完全アニメ / 1.0 = 完全物理。
	 * フニャフニャ感の強さは 0.2?0.5 が目安。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|RidingPhysics",
		meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableRidingPhysicsBlend"))
	float RidingPhysicsBlendWeight = 0.3f;

	/**
	 * 騎乗中に物理シミュレーションを開始するボーン名。
	 * このボーン以下が物理ブレンドの対象。
	 * 例: spine_01, spine_02, pelvis 等
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|RidingPhysics",
		meta = (EditCondition = "bEnableRidingPhysicsBlend"))
	FName RidingSimulateBelowBoneName = FName("spine_01");

	// =====================================================================
	// デバッグ
	// =====================================================================

	/** 画面に蓄積値を表示する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Debug")
	bool bShowDebugUI = true;

	// =====================================================================
	// 射出 / 爆発
	// =====================================================================

	/** 爆発の影響半径 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Eject",
		meta = (ClampMin = "0.0"))
	float ExplosionRadius = 600.0f;

	/** 爆発で範囲内ジョッキーに加算する衝撃量 (ImpactThreshold 以上推奨) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Eject",
		meta = (ClampMin = "0.0"))
	float ExplosionImpactMagnitude = 100000.0f;

	/** 爆発で範囲内ジョッキー (落馬済みも含む) のメッシュに与えるインパルス強度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Eject",
		meta = (ClampMin = "0.0"))
	float ExplosionImpulseStrength = 3000.0f;

	/**
	 * 着弾を検出するヒット相手の最小法線インパルス。
	 * これ未満は地面以外のかすめ判定とみなし爆発しない。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Eject",
		meta = (ClampMin = "0.0"))
	float LandingMinNormalImpulse = 1.0f;

	/** 射出後、最低この時間が経過してから着弾を有効化（出だしの誤爆防止） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Eject",
		meta = (ClampMin = "0.0"))
	float MinAirborneBeforeLanding = 0.15f;

	// =====================================================================
	// 振り回し
	// =====================================================================

	/** 剛体ロープの全長 (cm) ? Hand と Anchor の最大許容距離。これを超えないよう拘束する */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Swing",
		meta = (ClampMin = "0.0"))
	float SwingRestLength = 180.0f;

	/** バネ剛性。Hand→Anchor 方向への引き戻し力の係数 (F = k * Stretch)。値が大きいほど機敏に引き戻される */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Swing",
		meta = (ClampMin = "0.0"))
	float SwingStiffness = 4000.0f;

	/** バネ減衰。半径方向の相対速度に比例する制動力 (F -= c * v_radial)。揺れの収束を制御 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Swing",
		meta = (ClampMin = "0.0"))
	float SwingDamping = 80.0f;

	/** スプリングをかけるボーン名 (通常 hand_l) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Swing")
	FName SwingHandBoneName = FName("hand_l");

	/**
	 * アンカー (馬) の世界速度を Hand 速度に合成するか。
	 * true: 馬の運動が Jockey に伝わり「引きずられる／振り回される」
	 * false: 静止系基準でのみ拘束（旧挙動）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Swing")
	bool bUseAnchorVelocityTracking = true;

	/**
	 * アンカーの 1 フレーム移動量がこの値を超えた場合、テレポートとみなして
	 * アンカー速度合成をスキップする (NaN・暴走防止)。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Swing",
		meta = (ClampMin = "0.0"))
	float SwingMaxAnchorTeleportDist = 500.0f;

	/**
	 * Stretch (Dist - SwingRestLength) の絶対上限 (cm)。
	 * バネ力 F = k * Stretch がこの値を超える Stretch では飽和する。
	 * テレポート・物理破綻時に巨大な Stretch から指数発散することを防ぐ保険。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Swing",
		meta = (ClampMin = "0.0"))
	float SwingMaxExcess = 200.0f;

	/**
	 * バネ力の絶対上限 (UE 物理力単位)。
	 * F = k * Stretch - c * v_radial の合成力の大きさをこの値でクランプし、
	 * PhysicsAsset 制約経由の連鎖インパルスによる無限加速を物理的に断つ。
	 * 大きすぎると暴走、小さすぎると引き戻しが弱くなる。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Swing",
		meta = (ClampMin = "0.0"))
	float SwingMaxForce = 800000.0f;

	/**
	 * Stretch (Dist - SwingRestLength) の毎秒最大増加量 (cm/s)。
	 * 旋回などでアンカーが急動した際に Stretch が1フレームで跳ね上がると
	 * バネ力が突発発生して加速バーストになるため、Stretch の成長速度を
	 * 制限することで力の立ち上がりを緩やかにする。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Swing",
		meta = (ClampMin = "0.0"))
	float SwingMaxStretchGrowthRate = 300.0f;

	// =====================================================================
	// 鞍ラグドール騎乗（完全ラグドールを PhysicsConstraint で鞍へ拘束）
	// =====================================================================

	/** 鞍アンカー: 馬メッシュ上の着座ソケット名（拘束位置の基準） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Riding")
	FName SeatAnchorSocketName = FName("JockeySocket");

	/** 鞍へ拘束するジョッキー側ボーン（通常 pelvis） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Riding")
	FName SeatBoneName = FName("pelvis");

	/** 着座拘束の角度スイング上限 (deg)。上体の傾きの許容範囲。大きいほどフニャっとする */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Riding",
		meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float SeatAngularSwingLimit = 35.0f;

	/** 着座拘束の角度ツイスト上限 (deg)。ひねりの許容範囲 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Riding",
		meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float SeatAngularTwistLimit = 25.0f;

	/** 着座姿勢を直立へ戻す角度ドライブの剛性。大きいほど素早く起き上がる */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Riding",
		meta = (ClampMin = "0.0"))
	float SeatAngularDriveStiffness = 5000.0f;

	/** 着座姿勢ドライブの減衰。揺れの収束を制御 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Riding",
		meta = (ClampMin = "0.0"))
	float SeatAngularDriveDamping = 500.0f;

	/**
	 * 着座拘束を物理破断させるか。
	 * false（既定）= 物理破断しない（落馬は衝撃蓄積 ImpactThreshold 経由で判定＝安定）。
	 * true = 下記しきい値超過で拘束自体が破断する（要調整。settle 中の力で誤破断しやすい）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Riding")
	bool bSeatBreakable = false;

	/** 着座拘束が破断する並進力のしきい値（bSeatBreakable=true のとき有効） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Riding",
		meta = (ClampMin = "0.0", EditCondition = "bSeatBreakable"))
	float SeatLinearBreakForce = 150000.0f;

	/** 着座拘束が破断する回転トルクのしきい値（bSeatBreakable=true のとき有効） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Riding",
		meta = (ClampMin = "0.0", EditCondition = "bSeatBreakable"))
	float SeatAngularBreakTorque = 100000.0f;

	// =====================================================================
	// ヘッドバンキング攻撃
	// =====================================================================

	/** 横振りインパルスを与える上体ボーン名 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Attack")
	FName HeadbangBoneName = FName("spine_03");

	/** ヘッドバンキングの横方向インパルス強度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Attack",
		meta = (ClampMin = "0.0"))
	float HeadbangImpulse = 50000.0f;

	/** ヘッドバンキング命中時に相手ジョッキーへ加える衝撃値（大） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Attack",
		meta = (ClampMin = "0.0"))
	float HeadbangHitImpact = 3000.0f;

	/** ヘッドバンキング時に自分のジョッキーへ蓄積する衝撃値（少量） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Attack",
		meta = (ClampMin = "0.0"))
	float HeadbangSelfImpact = 300.0f;

	/** 攻撃ヒットと判定する最小法線インパルス */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Attack",
		meta = (ClampMin = "0.0"))
	float HeadbangHitMinNormalImpulse = 1.0f;

	/** ヘッドバンキング後、命中を攻撃として扱う有効時間 (秒) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Attack",
		meta = (ClampMin = "0.0"))
	float HeadbangActiveWindow = 0.25f;

	/**
	 * 近接ベースの攻撃ヒット判定半径 (cm)。物理衝突コールバックに依存せず、
	 * 攻撃有効中にこの半径内の相手ジョッキー（pelvis 間距離）へ衝撃を与える。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Attack",
		meta = (ClampMin = "0.0"))
	float HeadbangHitRadius = 120.0f;

	/**
	 * 射出ジョッキーが「着地した」とみなすメッシュ速度しきい値 (cm/s)。
	 * 空中時間が MinAirborneBeforeLanding を超え、速度がこれ以下に落ちたら爆発する。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jockey|Attack",
		meta = (ClampMin = "0.0"))
	float LandingSpeedThreshold = 200.0f;

private:
	// 状態
	bool bIsRiding = false;
	bool bIsKnockedOut = false;

	/** 現在の衝撃蓄積値 */
	float ImpactAccumulation = 0.0f;

	/** 衝撃計上の不応期タイマー(秒)。>0 の間は新規衝撃を無視する */
	float ImpactRefractoryTimer = 0.0f;

	/**
	 * 落馬時に吹っ飛ぶ水平方向（直近に受けた衝撃の押し出し方向）。
	 * ZeroVector のときは方向未指定とし、EnterRagdollState は後方へフォールバックする。
	 */
	FVector PendingKnockoffDir = FVector::ZeroVector;

	/** 騎乗中の馬 */
	UPROPERTY()
	TObjectPtr<AHorseCharacter> OwningHorse;

	/** 衝撃を加算し、閾値を超えれば落馬／引きずりへ移行させる */
	void AddImpact(float ImpactMagnitude);

	/** 全身ラグドールを解除（再騎乗・状態リセット時に呼ぶ） */
	void ClearRidingPhysicsBlend();

	/** ラグドール状態へ移行（落馬） */
	void EnterRagdollState();

	// 射出中フラグ（true なら着弾 OnMeshHit で爆発）
	bool bLaunchedAsProjectile = false;
	float AirborneTime = 0.0f;

	// 振り回し中フラグとアンカー
	bool bIsSwinging = false;
	UPROPERTY()
	TObjectPtr<USceneComponent> SwingAnchorComp;
	FName SwingAnchorSocket = NAME_None;

	// アンカー世界速度算出用の前フレームキャッシュ
	FVector PrevSwingAnchorWorld = FVector::ZeroVector;
	bool bSwingAnchorPrevValid = false;

	// Stretch 成長レート制限用の前フレームキャッシュ
	float PrevSwingStretch = 0.0f;
	bool bSwingPrevStretchValid = false;

	/** 振り回し中の剛体ロープ拘束 (位置クランプ＋速度合成 = 方針A) */
	void ApplySwingSpring(float DeltaTime);

	/** アンカー世界位置を解決 (ソケット優先、無効ならコンポーネント位置) */
	FVector ResolveSwingAnchorWorld() const;

	/**
	 * 近接ベースの攻撃ヒット判定（Tick から呼ぶ）。
	 * 攻撃有効中(HeadbangActiveTimer>0)に HeadbangHitRadius 内の相手ジョッキーへ衝撃を与える。
	 * 物理 OnComponentHit に依存せず確定的に当てる。
	 */
	void UpdateHeadbangProximityHit();

	/**
	 * 射出ジョッキーの着地判定（Tick から呼ぶ）。
	 * 空中時間が一定を超え、メッシュ速度が LandingSpeedThreshold 以下に落ちたら爆発する。
	 */
	void UpdateProjectileLanding();

	// 鞍ラグドール騎乗用（PhysicsConstraint 着座拘束）
	/** 着座拘束コンポーネント（実行時生成。馬カプセル↔ジョッキー pelvis を拘束） */
	UPROPERTY()
	TObjectPtr<UPhysicsConstraintComponent> SeatConstraint;

	/** 着座拘束を生成・設定する（鞍位置に Linear Lock＋Angular Limited/Drive＋Break） */
	void SetupSeatConstraint();

	/** 着座拘束を破断・破棄する */
	void BreakSeatConstraint();

	/** 騎乗ラグドールから引きずり状態へ遷移する（拘束破断・被弾時） */
	void TransitionToDragging();

	/** 着座拘束が物理破断したときのコールバック */
	UFUNCTION()
	void HandleSeatConstraintBroken(int32 ConstraintIndex);

	// ヘッドバンキング攻撃の有効残り時間 (秒)。>0 の間の接触を攻撃として扱う
	float HeadbangActiveTimer = 0.0f;

	/** デバッグUI描画 */
	void DrawDebugUI();

	/** メッシュの OnHit コールバック */
	UFUNCTION()
	void OnMeshHit(UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		FVector NormalImpulse,
		const FHitResult& Hit);

	/** カプセルの OnHit コールバック（騎乗中の衝突検出用） */
	UFUNCTION()
	void OnCapsuleHit(UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		FVector NormalImpulse,
		const FHitResult& Hit);
};
