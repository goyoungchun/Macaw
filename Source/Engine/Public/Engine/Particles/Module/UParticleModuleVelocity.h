/**
 * @file UParticleModuleVelocity.h
 * @brief 생성할 때 초기 속도를 더하는 모듈입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FVector4.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/FParticleDistribution.h"
#include "UParticleModuleVelocity.generated.h"

namespace Engine {

    /**
     * @brief 생성할 때 초기 속도를 더하는 모듈입니다.
     * @details StartVelocity는 에미터 기준 방향이며, StartVelocityRadial은 에미터 원점에서 파티클 쪽으로 더합니다.
     */
    UCLASS()
    class UParticleModuleVelocity final : public Engine::UParticleModule {
        GENERATED_BODY()

    public:
        UParticleModuleVelocity();

        ~UParticleModuleVelocity() override;

    public:
        void Spawn(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float SpawnTime, Engine::FBaseParticle& Particle) const override;

        void BakeDistributions() override;

        Engine::FVectorDistribution& GetStartVelocity();

        Engine::FFloatDistribution& GetStartVelocityRadial();

    private:
        /**
         * @brief 에미터 기준 초기 속도(m/s)입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Velocity")
        Engine::FVectorDistribution mStartVelocity{};

        /**
         * @brief 에미터 원점에서 파티클 방향으로 더하는 속력(m/s)입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Velocity")
        Engine::FFloatDistribution mStartVelocityRadial{};
    };

}
