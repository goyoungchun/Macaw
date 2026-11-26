/**
 * @file UParticleModuleEventReceiverBase.h
 * @brief 같은 컴포넌트의 에미터가 낸 이벤트를 받아 행동하는 모듈의 공통 부분입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/FName.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/FParticleEventTypes.h"
#include "UParticleModuleEventReceiverBase.generated.h"

namespace Engine {

    /**
     * @brief 종류와 이름으로 받을 이벤트를 고르고, 무엇을 할지는 하위 클래스가 ProcessEvent에서 정합니다.
     */
    UCLASS(Abstract)
    class UParticleModuleEventReceiverBase : public Engine::UParticleModule {
        GENERATED_BODY()

    public:
        UParticleModuleEventReceiverBase();

        ~UParticleModuleEventReceiverBase() override;

    public:
        Engine::EParticleModuleType GetModuleType() const override;

        /**
         * @brief 받을 이름을 FName으로 바꿔 둡니다.
         */
        void BakeDistributions() override;

        /**
         * @brief 종류(Any면 모두)와 이름(비어 있으면 모두)이 맞으면 true입니다.
         */
        bool WillProcessEvent(const Engine::FParticleEventData& Event) const;

        /**
         * @brief 배달할 때 이 모듈을 가진 에미터 인스턴스로 부릅니다.
         */
        virtual void ProcessEvent(Engine::FParticleEmitterInstance& Owner, const Engine::FParticleEventData& Event) const;

        Engine::EParticleEventType GetEventGeneratorType() const;

        const Core::FString& GetEventName() const;

    private:
        UPROPERTY(Edit, Serialize, Category = "Source")
        Engine::EParticleEventType mEventGeneratorType{Engine::EParticleEventType::Any};

        UPROPERTY(Edit, Serialize, Category = "Source")
        Core::FString mEventName{};

        Core::FName mCachedEventName{};
    };

}
