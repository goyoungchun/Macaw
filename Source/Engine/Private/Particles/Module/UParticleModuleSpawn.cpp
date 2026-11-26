#include "Engine/Particles/Module/UParticleModuleSpawn.h"
#include "Core/Math/FRandomStream.h"

namespace Engine {

    Engine::UParticleModuleSpawn::UParticleModuleSpawn() {
        SetName("Spawn");
        mRate.mConstant = 20.0f;
        mRateScale.mConstant = 1.0f;

        // Build 전에도 기본값으로 동작하도록 미리 굽습니다.
        BakeDistributions();
    }

    Engine::UParticleModuleSpawn::~UParticleModuleSpawn() = default;

    Engine::EParticleModuleType Engine::UParticleModuleSpawn::GetModuleType() const {
        return Engine::EParticleModuleType::Spawn;
    }

    void Engine::UParticleModuleSpawn::BakeDistributions() {
        mRate.Bake();
        mRateScale.Bake();
    }

    float Engine::UParticleModuleSpawn::GetSpawnRate(float EmitterTime, FRandomStream& Random) const {
        const float Value{mRate.GetValue(EmitterTime, Random) * mRateScale.GetValue(EmitterTime, Random)};

        return Value > 0.0f ? Value : 0.0f;
    }

    Int32 Engine::UParticleModuleSpawn::GetBurstCount(Index_t BurstIndex, FRandomStream& Random) const {
        if (BurstIndex >= mBurstList.size()) {
            return 0;
        }

        const Engine::FParticleBurst& Burst{mBurstList[BurstIndex]};

        return Burst.mCountLow > -1 ? Random.RandRange(Burst.mCountLow, Burst.mCount) : Burst.mCount;
    }

    Engine::FFloatDistribution& Engine::UParticleModuleSpawn::GetRate() {
        return mRate;
    }

    Engine::FFloatDistribution& Engine::UParticleModuleSpawn::GetRateScale() {
        return mRateScale;
    }

    const Core::TArray<Engine::FParticleBurst>& Engine::UParticleModuleSpawn::GetBurstList() const {
        return mBurstList;
    }

    void Engine::UParticleModuleSpawn::AddBurst(const Engine::FParticleBurst& Burst) {
        mBurstList.push_back(Burst);
    }

    void Engine::UParticleModuleSpawn::ClearBursts() {
        mBurstList.clear();
    }

}
