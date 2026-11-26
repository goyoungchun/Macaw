/**
 * @file UParticleModuleSize.h
 * @brief 생성할 때 크기를 정하는 모듈입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FVector4.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/FParticleDistribution.h"
#include "UParticleModuleSize.generated.h"

namespace Engine {

    /**
     * @brief 생성할 때 크기를 정하는 모듈입니다.
     * @details 스프라이트는 X가 가로, Y가 세로입니다.
     */
    UCLASS()
    class UParticleModuleSize final : public Engine::UParticleModule {
        GENERATED_BODY()

    public:
        UParticleModuleSize();

        ~UParticleModuleSize() override;

    public:
        void Spawn(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float SpawnTime, Engine::FBaseParticle& Particle) const override;

        void BakeDistributions() override;

        Engine::FVectorDistribution& GetStartSize();

    private:
        /**
         * @brief 초기 크기(m)입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Size")
        Engine::FVectorDistribution mStartSize{};
    };

}
