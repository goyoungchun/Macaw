/**
 * @file UParticleModuleLocation.h
 * @brief 생성 위치를 에미터 원점에서 옮기는 모듈입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FVector4.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/FParticleDistribution.h"
#include "UParticleModuleLocation.generated.h"

namespace Engine {

    /**
     * @brief 생성 위치를 에미터 원점에서 옮기는 모듈입니다.
     * @details 오프셋은 에미터 기준 방향입니다.
     */
    UCLASS()
    class UParticleModuleLocation final : public Engine::UParticleModule {
        GENERATED_BODY()

    public:
        UParticleModuleLocation();

        ~UParticleModuleLocation() override;

    public:
        void Spawn(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float SpawnTime, Engine::FBaseParticle& Particle) const override;

        void BakeDistributions() override;

        Engine::FVectorDistribution& GetStartLocation();

    private:
        /**
         * @brief 에미터 원점 기준 오프셋(m)입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Location")
        Engine::FVectorDistribution mStartLocation{};
    };

}
