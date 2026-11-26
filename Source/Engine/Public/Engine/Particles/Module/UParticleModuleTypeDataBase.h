/**
 * @file UParticleModuleTypeDataBase.h
 * @brief 에미터 종류(Mesh·Beam·Ribbon 등)를 바꾸는 TypeData 모듈의 기반 클래스입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Engine/Particles/UParticleModule.h"

#include <memory>

#include "UParticleModuleTypeDataBase.generated.h"

namespace Engine {

    class UParticleEmitter;
    class UParticleSystemComponent;

    /**
     * @brief LOD 레벨의 TypeData 슬롯에 두며 에미터가 만들 인스턴스 종류를 정합니다.
     */
    UCLASS(Abstract)
    class UParticleModuleTypeDataBase : public Engine::UParticleModule {
        GENERATED_BODY()

    public:
        UParticleModuleTypeDataBase();

        ~UParticleModuleTypeDataBase() override;

    public:
        Engine::EParticleModuleType GetModuleType() const override;

        /**
         * @brief 이 종류의 에미터 인스턴스를 만들고 Init까지 끝냅니다.
         * @return 기본 스프라이트 인스턴스를 쓰려면 nullptr입니다.
         */
        virtual std::unique_ptr<Engine::FParticleEmitterInstance> CreateInstance(const Engine::UParticleEmitter& Emitter, Engine::UParticleSystemComponent& Component) const;
    };

}
