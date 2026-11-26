/**
 * @file AParticleSystemActor.h
 * @brief 파티클 시스템 컴포넌트 하나를 가진 배치용 액터입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/ReflectionMacros.h"
#include "Engine/AActor.h"
#include "AParticleSystemActor.generated.h"

namespace Engine {

    class UParticleSystemComponent;

    /**
     * @brief 레벨에 파티클 이펙트를 배치할 때 쓰는 액터입니다.
     */
    UCLASS()
    class AParticleSystemActor final : public Engine::AActor {
        GENERATED_BODY()

    public:
        AParticleSystemActor();

        ~AParticleSystemActor() override;

    public:
        bool InitializeDefaultComponents() override;

        Engine::UParticleSystemComponent* GetParticleSystemComponent() const;
    };

}
