#include "Engine/Particles/Module/UParticleModuleSize.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleEmitterInstance.h"

namespace Engine {

    Engine::UParticleModuleSize::UParticleModuleSize() {
        SetName("Initial Size");
        mSpawnModule = true;
        mStartSize.mConstant = FVector{0.1f, 0.1f, 0.1f};
        // Uniform일 때 가로와 세로가 따로 뽑혀 찌그러지지 않게 합니다.
        mStartSize.mLockAxes = true;

        // Build 전에도 기본값으로 동작하도록 미리 굽습니다.
        BakeDistributions();
    }

    Engine::UParticleModuleSize::~UParticleModuleSize() = default;

    void Engine::UParticleModuleSize::Spawn(Engine::FParticleEmitterInstance& Owner, Int32, float, Engine::FBaseParticle& Particle) const {
        const FVector Size{mStartSize.GetValue(Owner.mEmitterTime, Owner.mRandomStream)};

        // 매 프레임 기준 크기로 되돌리므로 둘 다 더합니다.
        Particle.mSize += Size;
        Particle.mBaseSize += Size;
    }

    void Engine::UParticleModuleSize::BakeDistributions() {
        mStartSize.Bake();
    }

    Engine::FVectorDistribution& Engine::UParticleModuleSize::GetStartSize() {
        return mStartSize;
    }

}
