#include "Engine/Particles/Module/UParticleModuleLifetime.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleEmitterInstance.h"

namespace Engine {

    Engine::UParticleModuleLifetime::UParticleModuleLifetime() {
        SetName("Lifetime");
        mSpawnModule = true;
        mLifetime.mConstant = 1.0f;

        // Build 전에도 기본값으로 동작하도록 미리 굽습니다.
        BakeDistributions();
    }

    Engine::UParticleModuleLifetime::~UParticleModuleLifetime() = default;

    void Engine::UParticleModuleLifetime::Spawn(Engine::FParticleEmitterInstance& Owner, Int32, float SpawnTime, Engine::FBaseParticle& Particle) const {
        const float MaxLifetime{mLifetime.GetValue(Owner.mEmitterTime, Owner.mRandomStream)};

        Particle.mOneOverMaxLifetime = MaxLifetime > 0.0f ? 1.0f / MaxLifetime : 0.0f;

        // 프레임 중간에 태어난 만큼 수명도 미리 진행합니다.
        Particle.mRelativeTime = SpawnTime * Particle.mOneOverMaxLifetime;
    }

    void Engine::UParticleModuleLifetime::BakeDistributions() {
        mLifetime.Bake();
    }

    Engine::FFloatDistribution& Engine::UParticleModuleLifetime::GetLifetime() {
        return mLifetime;
    }

}
