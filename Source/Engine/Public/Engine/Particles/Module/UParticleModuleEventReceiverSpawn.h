/**
 * @file UParticleModuleEventReceiverSpawn.h
 * @brief 이벤트를 받으면 이 에미터의 파티클을 이벤트 위치에 만드는 모듈입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Engine/Particles/FParticleDistribution.h"
#include "Engine/Particles/Module/UParticleModuleEventReceiverBase.h"
#include "UParticleModuleEventReceiverSpawn.generated.h"

namespace Engine {

    /**
     * @brief 빗방울 충돌에서 물보라, 로켓 소멸에서 폭발을 만들 때 씁니다. 받는 에미터는 보통 Spawn Rate를 0으로 둡니다.
     */
    UCLASS()
    class UParticleModuleEventReceiverSpawn final : public Engine::UParticleModuleEventReceiverBase {
        GENERATED_BODY()

    public:
        UParticleModuleEventReceiverSpawn();

        ~UParticleModuleEventReceiverSpawn() override;

    public:
        void BakeDistributions() override;

        void ProcessEvent(Engine::FParticleEmitterInstance& Owner, const Engine::FParticleEventData& Event) const override;

    private:
        /**
         * @brief 이벤트 하나당 만들 개수입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Spawn")
        Engine::FFloatDistribution mSpawnCount{};

        /**
         * @brief true면 이벤트 위치 대신 에미터 위치에 만듭니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Location")
        bool mUseEmitterLocation{};

        /**
         * @brief true면 이벤트 속도에 배율을 곱해 생성 속도에 더합니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Velocity")
        bool mInheritVelocity{};

        UPROPERTY(Edit, Serialize, Category = "Velocity")
        float mInheritVelocityScale{1.0f};
    };

}
