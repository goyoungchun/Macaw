/**
 * @file UParticleModuleMeshRotationRate.h
 * @brief 메시 파티클의 회전 속도를 정하는 모듈입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/FParticleDistribution.h"
#include "UParticleModuleMeshRotationRate.generated.h"

namespace Engine {

    /**
     * @brief 메시 에미터가 아니면 아무것도 하지 않습니다.
     */
    UCLASS()
    class UParticleModuleMeshRotationRate final : public Engine::UParticleModule {
        GENERATED_BODY()

    public:
        UParticleModuleMeshRotationRate();

        ~UParticleModuleMeshRotationRate() override;

    public:
        void Spawn(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float SpawnTime, Engine::FBaseParticle& Particle) const override;

        void BakeDistributions() override;

        Engine::FVectorDistribution& GetStartRotationRate();

    private:
        /**
         * @brief 회전 속도(도/초)입니다. X = Roll, Y = Pitch, Z = Yaw입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Rotation")
        Engine::FVectorDistribution mStartRotationRate{};
    };

}
