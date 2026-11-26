/**
 * @file UParticleSystemComponent.h
 * @brief 파티클 시스템 에셋을 월드에서 실행하는 컴포넌트입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/Literals/QuantityTypes.h"
#include "CoreUObject/TDelegate.h"
#include "CoreUObject/TObjectRef.h"
#include "Engine/Component/UPrimitiveComponent.h"
#include "Engine/Particles/FParticleEventTypes.h"

#include <memory>

#include "UParticleSystemComponent.generated.h"

namespace Asset {
    class UMaterial;
    class UStaticMesh;
}

namespace Engine {

    class FParticleEmitterInstance;
    class UParticleSystem;
    struct FParticleRenderData;

    /**
     * @brief 템플릿 에셋의 에미터마다 인스턴스를 만들어 매 Tick 시뮬레이션합니다.
     * @details 템플릿은 에셋 캐시가 소유하며 읽기만 합니다. 에디터 월드에서도 Tick해 배치한 자리에서 미리 보입니다.
     */
    UCLASS()
    class UParticleSystemComponent final : public Engine::UPrimitiveComponent {
        GENERATED_BODY()

    public:
        using FParticleSpawnDelegate = CoreUObject::TDelegate<Engine::UParticleSystemComponent*, const Engine::FParticleEventSpawnData&>;

        using FParticleDeathDelegate = CoreUObject::TDelegate<Engine::UParticleSystemComponent*, const Engine::FParticleEventDeathData&>;

        using FParticleCollideDelegate = CoreUObject::TDelegate<Engine::UParticleSystemComponent*, const Engine::FParticleEventCollisionData&>;

        using FParticleBurstDelegate = CoreUObject::TDelegate<Engine::UParticleSystemComponent*, const Engine::FParticleEventBurstData&>;

    public:
        UParticleSystemComponent();

        ~UParticleSystemComponent() override;

    public:
        const Core::FString& GetTemplatePath() const;

        /**
         * @brief 템플릿 에셋 경로를 바꿉니다. 등록된 상태면 바로 다시 로드하고 자동 활성화 설정에 따라 시작합니다.
         */
        bool SetTemplatePath(const Core::FString& Path);

        bool SetPreviewTemplate(Engine::UParticleSystem* Template);

        /**
         * @brief 로드한 템플릿이며 경로가 비었거나 로드에 실패하면 nullptr입니다.
         */
        Engine::UParticleSystem* GetTemplate() const;

        bool IsAutoActivate() const;

        void SetAutoActivate(bool AutoActivate);

        /**
         * @brief 인스턴스를 새로 만들고 LOD를 다시 적용합니다. 생성은 활성화 상태와 상관없이 켜진 상태로 시작합니다.
         */
        void InitializeSystem();

        /**
         * @brief 생성을 시작합니다. 인스턴스가 없거나 Reset이면 처음부터 다시 만듭니다.
         */
        void ActivateSystem(bool Reset = false);

        /**
         * @brief 생성만 멈춥니다. 살아 있는 파티클은 수명이 끝날 때까지 갱신됩니다.
         */
        void DeactivateSystem();

        /**
         * @brief 모든 인스턴스를 지웁니다.
         */
        void ResetParticles();

        /**
         * @brief 모든 에미터가 생성을 끝내고 살아 있는 파티클도 없으면 true입니다.
         */
        bool HasCompleted() const;

        const Core::TArray<std::unique_ptr<Engine::FParticleEmitterInstance>>& GetEmitterInstances() const;

        /**
         * @brief 인스턴스의 에미터 번호입니다.
         * @return 없으면 -1입니다.
         */
        Int32 GetEmitterIndex(const Engine::FParticleEmitterInstance* Instance) const;

        /**
         * @brief 현재 LOD 레벨입니다.
         */
        Int32 GetLODLevel() const;

        /**
         * @brief LOD 레벨을 직접 정합니다. DirectSet 방식에서 게임 코드가 씁니다.
         */
        void SetLODLevel(Int32 LODLevel);

        /**
         * @brief 에디터 미리보기에서 강제할 LOD이며 -1이면 끕니다. 게임 월드에서는 무시합니다.
         */
        Int32 GetPreviewLODOverride() const;

        void SetPreviewLODOverride(Int32 LODLevel);

        /**
         * @brief 살아 있는 파티클 수의 합입니다.
         */
        Int32 GetActiveParticleCount() const;

        void Tick(float DeltaTime) override;

        void UnregisterComponent() override;

        Engine::EPrimitiveType GetPrimitiveType() const override;

        const Asset::FMeshData& GetMeshData() const override;

        FBox GetWorldBounds() const override;

        bool ShouldCreateRenderState() const override;

        /**
         * @brief 그림자와 데칼을 받지 않고 마지막으로 만든 그리기 데이터를 담은 프록시입니다.
         */
        Engine::FPrimitiveSceneProxy CreateSceneProxy() const override;

        /**
         * @brief 에미터가 쓰는 머티리얼 경로를 로드합니다. 한 번 찾은 결과(실패 포함)는 이 컴포넌트가 기억합니다.
         * @return 경로가 비었거나 로드에 실패하면 nullptr입니다.
         */
        Asset::UMaterial* ResolveMaterial(const Core::FString& Path);

        /**
         * @brief 메시 에미터가 쓰는 메시 경로를 로드합니다. 한 번 찾은 결과(실패 포함)는 이 컴포넌트가 기억합니다.
         * @return 경로가 비었거나 로드에 실패하면 nullptr입니다.
         */
        Asset::UStaticMesh* ResolveMesh(const Core::FString& Path);

        /**
         * @brief 에미터 인스턴스가 이벤트를 보고합니다. 모든 에미터 Tick이 끝난 뒤 한꺼번에 배달합니다.
         */
        void ReportEventSpawn(const Engine::FParticleEventSpawnData& Event);

        void ReportEventDeath(const Engine::FParticleEventDeathData& Event);

        void ReportEventCollision(const Engine::FParticleEventCollisionData& Event);

        void ReportEventBurst(const Engine::FParticleEventBurstData& Event);

        /**
         * @brief Generator가 낸 이벤트를 게임 코드가 받는 델리게이트입니다. Tick 맨 끝에 부릅니다.
         */
        FParticleSpawnDelegate& GetOnParticleSpawn();

        FParticleDeathDelegate& GetOnParticleDeath();

        FParticleCollideDelegate& GetOnParticleCollide();

        FParticleBurstDelegate& GetOnParticleBurst();

    private:
        void OnRegister() override;

        /**
         * @brief 경로의 템플릿을 로드합니다. 경로가 비었거나 실패하면 비웁니다.
         */
        void ResolveTemplate();

        /**
         * @brief 템플릿을 바꾼 뒤 인스턴스를 다시 만들고 필요하면 시작합니다.
         */
        void RestartForTemplate();

        /**
         * @brief 에셋의 LOD 방식에 따라 주기적으로 LOD를 다시 고릅니다.
         */
        void UpdateLODLevel(Core::FTime DeltaTime);

        /**
         * @brief 범위를 맞춰 저장하고 모든 인스턴스에 전달합니다.
         */
        void ApplyLODLevel(Int32 LODLevel);

        /**
         * @brief 가장 가까운 뷰 거리로 고른 LOD이며 뷰를 알 수 없으면 -1입니다.
         */
        Int32 ComputeDesiredLOD() const;

        bool IsPreviewLODActive() const;

        /**
         * @brief 인스턴스 상태로 그리기 데이터를 새로 만들고 렌더 상태 갱신을 예약합니다.
         */
        void UpdateRenderData();

        /**
         * @brief 모인 이벤트를 각 에미터의 Receiver에 배달하고 버퍼를 비웁니다.
         * @details 배달 중 새로 생긴 이벤트는 다음 Tick에 배달합니다. 서로 부르는 설정이어도 한 프레임에 한 단계만 진행합니다.
         */
        void DispatchEvents();

        /**
         * @brief 이번 Tick에 배달한 이벤트를 게임 코드 델리게이트로 보냅니다. 콜백이 이 컴포넌트를 바꾸거나 파괴할 수 있어 맨 마지막에 부릅니다.
         */
        void BroadcastGameEvents();

        void ClearEvents();

    private:
        UPROPERTY(Edit, Serialize, Category = "Particles", Name = "Template", Meta = (AssetType = "ParticleSystem", EmptyLabel = "None"), Getter = GetTemplatePath, Setter = SetTemplatePath)
        Core::FString mTemplatePath{};

        UPROPERTY(Edit, Serialize, Category = "Activation", Getter = IsAutoActivate, Setter = SetAutoActivate)
        bool mAutoActivate{true};

        CoreUObject::TObjectRef<Engine::UParticleSystem> mTemplate{};

        /**
         * @brief 인스턴스를 만든 템플릿입니다. 템플릿이 바뀌면 이 값과 달라집니다.
         */
        CoreUObject::TObjectRef<Engine::UParticleSystem> mInitializedTemplate{};

        Core::TArray<std::unique_ptr<Engine::FParticleEmitterInstance>> mEmitterInstances{};

        bool mWasCompleted{};

        Int32 mLODLevel{};

        Core::FTime mLODCheckTimer{};

        Int32 mPreviewLODOverride{-1};

        /**
         * @brief 활성화할 때 뷰가 없어 LOD를 정하지 못했으면 true입니다.
         */
        bool mPendingActivateLOD{};

        /**
         * @brief 마지막으로 만든 그리기 데이터이며 프록시가 공유합니다.
         */
        std::shared_ptr<const Engine::FParticleRenderData> mRenderData{};

        /**
         * @brief 경로별로 찾은 에미터 머티리얼이며 실패한 경로는 nullptr로 남깁니다.
         */
        Core::TMap<Core::FString, CoreUObject::TObjectRef<Asset::UMaterial>> mMaterials{};

        /**
         * @brief 경로별로 찾은 메시이며 실패한 경로는 nullptr로 남깁니다.
         */
        Core::TMap<Core::FString, CoreUObject::TObjectRef<Asset::UStaticMesh>> mMeshes{};

        Core::TArray<Engine::FParticleEventSpawnData> mSpawnEvents{};

        Core::TArray<Engine::FParticleEventDeathData> mDeathEvents{};

        Core::TArray<Engine::FParticleEventCollisionData> mCollisionEvents{};

        Core::TArray<Engine::FParticleEventBurstData> mBurstEvents{};

        /**
         * @brief 이번 Tick에 Receiver로 배달했고 델리게이트로 보낼 이벤트입니다.
         */
        Core::TArray<Engine::FParticleEventSpawnData> mDispatchingSpawnEvents{};

        Core::TArray<Engine::FParticleEventDeathData> mDispatchingDeathEvents{};

        Core::TArray<Engine::FParticleEventCollisionData> mDispatchingCollisionEvents{};

        Core::TArray<Engine::FParticleEventBurstData> mDispatchingBurstEvents{};

        FParticleSpawnDelegate mOnParticleSpawn{};

        FParticleDeathDelegate mOnParticleDeath{};

        FParticleCollideDelegate mOnParticleCollide{};

        FParticleBurstDelegate mOnParticleBurst{};
    };

}
