/**
 * @file UParticleModuleColorOverLife.h
 * @brief 수명 진행도(0~1)에 따라 색과 알파를 덮어쓰는 모듈입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FVector4.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/FParticleDistribution.h"
#include "UParticleModuleColorOverLife.generated.h"

namespace Engine {

    /**
     * @brief 수명 진행도(0~1)에 따라 색과 알파를 덮어쓰는 모듈입니다.
     * @details 분포의 시간 축은 초가 아니라 수명 진행도입니다.
     */
    UCLASS()
    class UParticleModuleColorOverLife final : public Engine::UParticleModule {
        GENERATED_BODY()

    public:
        UParticleModuleColorOverLife();

        ~UParticleModuleColorOverLife() override;

    public:
        void Spawn(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float SpawnTime, Engine::FBaseParticle& Particle) const override;

        void Update(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float DeltaTime) const override;

        void BakeDistributions() override;

    private:
        FVector4 Evaluate(Engine::FParticleEmitterInstance& Owner, float RelativeTime) const;

    private:
        UPROPERTY(Edit, Serialize, Category = "Color")
        Engine::FVectorDistribution mColorOverLife{};

        UPROPERTY(Edit, Serialize, Category = "Color")
        Engine::FFloatDistribution mAlphaOverLife{};

        UPROPERTY(Edit, Serialize, Category = "Color")
        bool mClampAlpha{true};
    };

}
