/**
 * @file UParticleModuleSizeMultiplyLife.h
 * @brief 수명 진행도(0~1)에 따른 배율을 크기에 곱하는 모듈입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FVector4.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/FParticleDistribution.h"
#include "UParticleModuleSizeMultiplyLife.generated.h"

namespace Engine {

    /**
     * @brief 수명 진행도(0~1)에 따른 배율을 크기에 곱하는 모듈입니다.
     * @details 크기는 매 프레임 기준 크기로 되돌린 뒤 곱합니다.
     */
    UCLASS()
    class UParticleModuleSizeMultiplyLife final : public Engine::UParticleModule {
        GENERATED_BODY()

    public:
        UParticleModuleSizeMultiplyLife();

        ~UParticleModuleSizeMultiplyLife() override;

    public:
        void Spawn(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float SpawnTime, Engine::FBaseParticle& Particle) const override;

        void Update(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float DeltaTime) const override;

        void BakeDistributions() override;

    private:
        void Apply(Engine::FParticleEmitterInstance& Owner, Engine::FBaseParticle& Particle) const;

    private:
        UPROPERTY(Edit, Serialize, Category = "Size")
        Engine::FVectorDistribution mLifeMultiplier{};

        UPROPERTY(Edit, Serialize, Category = "Size")
        bool mMultiplyX{true};

        UPROPERTY(Edit, Serialize, Category = "Size")
        bool mMultiplyY{true};

        UPROPERTY(Edit, Serialize, Category = "Size")
        bool mMultiplyZ{true};
    };

}
