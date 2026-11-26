#include "Engine/Particles/Module/UParticleModuleLocation.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleEmitterInstance.h"

namespace Engine {

    Engine::UParticleModuleLocation::UParticleModuleLocation() {
        SetName("Initial Location");
        mSpawnModule = true;
        mStartLocation.mMode = Engine::EDistributionMode::Uniform;
        mStartLocation.mMinValue = FVector{-0.2f, -0.2f, 0.0f};
        mStartLocation.mMaxValue = FVector{0.2f, 0.2f, 0.0f};

        // Build 전에도 기본값으로 동작하도록 미리 굽습니다.
        BakeDistributions();
    }

    Engine::UParticleModuleLocation::~UParticleModuleLocation() = default;

    void Engine::UParticleModuleLocation::Spawn(Engine::FParticleEmitterInstance& Owner, Int32, float, Engine::FBaseParticle& Particle) const {
        Particle.mLocation += Owner.EmitterToSimulationVector(mStartLocation.GetValue(Owner.mEmitterTime, Owner.mRandomStream));
    }

    void Engine::UParticleModuleLocation::BakeDistributions() {
        mStartLocation.Bake();
    }

    Engine::FVectorDistribution& Engine::UParticleModuleLocation::GetStartLocation() {
        return mStartLocation;
    }

}
