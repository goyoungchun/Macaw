/**
 * @file UParticleModuleSpawn.h
 * @brief 초당 생성 수와 Burst 목록으로 생성 개수를 정하는 필수 모듈입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Engine/Particles/UParticleModule.h"
#include "Engine/Particles/FParticleDistribution.h"
#include "UParticleModuleSpawn.generated.h"

struct FRandomStream;

namespace Engine {

    /**
     * @brief 루프 안의 정해진 시간에 한꺼번에 만드는 생성입니다.
     */
    USTRUCT()
    struct FParticleBurst {
        GENERATED_BODY()

    public:
        UPROPERTY(Edit, Serialize)
        Int32 mCount{};

        /**
         * @brief -1이면 mCount로 고정하고, 아니면 [mCountLow, mCount]에서 고릅니다.
         */
        UPROPERTY(Edit, Serialize)
        Int32 mCountLow{-1};

        /**
         * @brief 루프 시작부터의 시간(초)입니다.
         */
        UPROPERTY(Edit, Serialize)
        float mTime{};
    };

    /**
     * @brief 생성 개수를 정합니다. LOD 레벨의 Spawn 슬롯에만 둡니다.
     */
    UCLASS()
    class UParticleModuleSpawn final : public Engine::UParticleModule {
        GENERATED_BODY()

    public:
        UParticleModuleSpawn();

        ~UParticleModuleSpawn() override;

    public:
        Engine::EParticleModuleType GetModuleType() const override;

        void BakeDistributions() override;

        /**
         * @brief 에미터 시간에 맞는 초당 생성 수이며 음수는 0으로 자릅니다.
         */
        float GetSpawnRate(float EmitterTime, FRandomStream& Random) const;

        /**
         * @brief Burst 하나가 만들 개수입니다. 범위 밖이면 0입니다.
         */
        Int32 GetBurstCount(Index_t BurstIndex, FRandomStream& Random) const;

        Engine::FFloatDistribution& GetRate();

        Engine::FFloatDistribution& GetRateScale();

        const Core::TArray<Engine::FParticleBurst>& GetBurstList() const;

        void AddBurst(const Engine::FParticleBurst& Burst);

        void ClearBursts();

    private:
        /**
         * @brief 초당 생성 수입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Spawn")
        Engine::FFloatDistribution mRate{};

        /**
         * @brief mRate에 곱하는 배율입니다.
         */
        UPROPERTY(Edit, Serialize, Category = "Spawn")
        Engine::FFloatDistribution mRateScale{};

        UPROPERTY(Edit, Serialize, Category = "Burst")
        Core::TArray<Engine::FParticleBurst> mBurstList{};
    };

}
