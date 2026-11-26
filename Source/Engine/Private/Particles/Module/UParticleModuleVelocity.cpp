#include "Engine/Particles/Module/UParticleModuleVelocity.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleEmitterInstance.h"

namespace Engine {

    Engine::UParticleModuleVelocity::UParticleModuleVelocity() {
        SetName("Initial Velocity");
        mSpawnModule = true;
        mStartVelocity.mMode = Engine::EDistributionMode::Uniform;
        mStartVelocity.mMinValue = FVector{-0.2f, -0.2f, 0.8f};
        mStartVelocity.mMaxValue = FVector{0.2f, 0.2f, 1.2f};

        // Build 전에도 기본값으로 동작하도록 미리 굽습니다.
        BakeDistributions();
    }

    Engine::UParticleModuleVelocity::~UParticleModuleVelocity() = default;

    void Engine::UParticleModuleVelocity::Spawn(Engine::FParticleEmitterInstance& Owner, Int32, float, Engine::FBaseParticle& Particle) const {
        FVector Velocity{Owner.EmitterToSimulationVector(mStartVelocity.GetValue(Owner.mEmitterTime, Owner.mRandomStream))};
        const FVector FromOrigin{(Particle.mLocation - Owner.GetEmitterOrigin()).Normalized()};

        Velocity += FromOrigin * mStartVelocityRadial.GetValue(Owner.mEmitterTime, Owner.mRandomStream);

        // 매 프레임 기준 속도로 되돌리므로 둘 다 더합니다.
        Particle.mVelocity += Velocity;
        Particle.mBaseVelocity += Velocity;
    }

    void Engine::UParticleModuleVelocity::BakeDistributions() {
        mStartVelocity.Bake();
        mStartVelocityRadial.Bake();
    }

    Engine::FVectorDistribution& Engine::UParticleModuleVelocity::GetStartVelocity() {
        return mStartVelocity;
    }

    Engine::FFloatDistribution& Engine::UParticleModuleVelocity::GetStartVelocityRadial() {
        return mStartVelocityRadial;
    }

}
