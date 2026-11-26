/**
 * @file UParticleModuleSubUV.h
 * @brief 텍스처 아틀라스의 어느 칸을 그릴지 파티클마다 정하는 모듈입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/FParticleDistribution.h"
#include "UParticleModuleSubUV.generated.h"

namespace Engine {

    class UParticleModuleRequired;

    /**
     * @brief SubUV 모듈이 파티클마다 저장하는 값입니다.
     */
    struct FFullSubUVPayload {
    public:
        /**
         * @brief 정수부는 칸 번호, 소수부는 다음 칸과 섞는 비율입니다.
         */
        float mImageIndex{};

        /**
         * @brief Random 방식에서 마지막으로 칸을 뽑은 수명 진행도입니다. 0이면 아직 뽑지 않았습니다.
         */
        float mRandomImageTime{};
    };

    /**
     * @brief 칸 수와 방식은 Required 모듈의 SubUV 설정을 따릅니다.
     * @details Linear 방식은 수명 진행도(0~1)를 SubImageIndex 분포에 넣어 칸 번호를 얻습니다.
     */
    UCLASS()
    class UParticleModuleSubUV final : public Engine::UParticleModule {
        GENERATED_BODY()

    public:
        UParticleModuleSubUV();

        ~UParticleModuleSubUV() override;

    public:
        Engine::EParticleModuleType GetModuleType() const override;

        void Spawn(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float SpawnTime, Engine::FBaseParticle& Particle) const override;

        void Update(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float DeltaTime) const override;

        Uint32 RequiredBytes() const override;

        void BakeDistributions() override;

        Engine::FFloatDistribution& GetSubImageIndex();

    private:
        float DetermineImageIndex(Engine::FParticleEmitterInstance& Owner, const Engine::UParticleModuleRequired& Required, const Engine::FBaseParticle& Particle, Engine::FFullSubUVPayload& Payload) const;

    private:
        /**
         * @brief Linear 방식에서 수명 진행도를 칸 번호로 바꿉니다. 마지막 키 값을 (칸 수 - 1)로 맞춰야 전체 칸을 돕니다.
         */
        UPROPERTY(Edit, Serialize, Category = "SubUV")
        Engine::FFloatDistribution mSubImageIndex{};
    };

}
