/**
 * @file UParticleModuleAcceleration.h
 * @brief 파티클마다 가속도를 하나 뽑아 매 프레임 속도에 더하는 모듈입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FVector4.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/FParticleDistribution.h"
#include "UParticleModuleAcceleration.generated.h"

namespace Engine {

    /**
     * @brief 파티클마다 가속도를 하나 뽑아 매 프레임 속도에 더하는 모듈입니다.
     * @details 중력·바람에 씁니다. 뽑은 가속도는 시뮬레이션 공간으로 바꿔 Payload에 둡니다.
     */
    UCLASS()
    class UParticleModuleAcceleration final : public Engine::UParticleModule {
        GENERATED_BODY()

    public:
        UParticleModuleAcceleration();

        ~UParticleModuleAcceleration() override;

    public:
        void Spawn(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float SpawnTime, Engine::FBaseParticle& Particle) const override;

        void Update(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float DeltaTime) const override;

        void BakeDistributions() override;

        /**
         * @brief 뽑은 가속도(FVector)를 파티클마다 저장합니다.
         */
        Uint32 RequiredBytes() const override;

        Engine::FVectorDistribution& GetAcceleration();

    private:
        /**
         * @brief 가속도(m/s²)입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Acceleration")
        Engine::FVectorDistribution mAcceleration{};

        /**
         * @brief true면 월드 방향이라 컴포넌트가 회전해도 같은 방향이고, false면 컴포넌트를 따라 회전합니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Acceleration")
        bool mAlwaysInWorldSpace{true};
    };

}
