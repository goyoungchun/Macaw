/**
 * @file UParticleModuleEventReceiverKillAll.h
 * @brief 이벤트를 받으면 이 에미터의 파티클을 모두 지우는 모듈입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Engine/Particles/Module/UParticleModuleEventReceiverBase.h"
#include "UParticleModuleEventReceiverKillAll.generated.h"

namespace Engine {

    /**
     * @brief 보호막이 맞으면 빛 효과를 바로 끌 때처럼 씁니다.
     */
    UCLASS()
    class UParticleModuleEventReceiverKillAll final : public Engine::UParticleModuleEventReceiverBase {
        GENERATED_BODY()

    public:
        UParticleModuleEventReceiverKillAll();

        ~UParticleModuleEventReceiverKillAll() override;

    public:
        void ProcessEvent(Engine::FParticleEmitterInstance& Owner, const Engine::FParticleEventData& Event) const override;

    private:
        /**
         * @brief true면 지운 뒤 이 에미터의 생성도 멈춥니다. 컴포넌트를 다시 활성화하면 재개합니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Kill")
        bool mStopSpawning{};
    };

}
