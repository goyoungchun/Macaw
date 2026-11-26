/**
 * @file UParticleModuleLifetime.h
 * @brief 파티클마다 수명을 정하는 모듈입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FVector4.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/FParticleDistribution.h"
#include "UParticleModuleLifetime.generated.h"

namespace Engine {

    /**
     * @brief 파티클마다 수명을 정하는 모듈입니다.
     * @details 수명이 0 이하인 파티클은 죽지 않습니다.
     */
    UCLASS()
    class UParticleModuleLifetime final : public Engine::UParticleModule {
        GENERATED_BODY()

    public:
        UParticleModuleLifetime();

        ~UParticleModuleLifetime() override;

    public:
        void Spawn(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float SpawnTime, Engine::FBaseParticle& Particle) const override;

        void BakeDistributions() override;

        Engine::FFloatDistribution& GetLifetime();

    private:
        /**
         * @brief 수명(초)입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Lifetime")
        Engine::FFloatDistribution mLifetime{};
    };

}
