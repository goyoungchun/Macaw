/**
 * @file UParticleModuleColor.h
 * @brief 생성할 때 색과 알파를 정하는 모듈입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FVector4.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/FParticleDistribution.h"
#include "UParticleModuleColor.generated.h"

namespace Engine {

    /**
     * @brief 생성할 때 색과 알파를 정하는 모듈입니다.
     * @details RGB와 알파를 따로 정합니다.
     */
    UCLASS()
    class UParticleModuleColor final : public Engine::UParticleModule {
        GENERATED_BODY()

    public:
        UParticleModuleColor();

        ~UParticleModuleColor() override;

    public:
        void Spawn(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float SpawnTime, Engine::FBaseParticle& Particle) const override;

        void BakeDistributions() override;

        Engine::FVectorDistribution& GetStartColor();

        Engine::FFloatDistribution& GetStartAlpha();

    private:
        UPROPERTY(Edit, Serialize, Category = "Color")
        Engine::FVectorDistribution mStartColor{};

        UPROPERTY(Edit, Serialize, Category = "Color")
        Engine::FFloatDistribution mStartAlpha{};

        /**
         * @brief true면 알파를 [0, 1]로 자릅니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Color")
        bool mClampAlpha{true};
    };

}
