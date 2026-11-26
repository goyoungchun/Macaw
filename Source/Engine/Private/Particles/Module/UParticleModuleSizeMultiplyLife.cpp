#include "Engine/Particles/Module/UParticleModuleSizeMultiplyLife.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleEmitterInstance.h"
#include "Engine/Particles/ParticleHelper.h"

namespace Engine {

    Engine::UParticleModuleSizeMultiplyLife::UParticleModuleSizeMultiplyLife() {
        SetName("Size By Life");
        mSpawnModule = true;
        mUpdateModule = true;
        mLifeMultiplier.mConstant = FVector{1.0f, 1.0f, 1.0f};
        mLifeMultiplier.mLockAxes = true;

        // Build 전에도 기본값으로 동작하도록 미리 굽습니다.
        BakeDistributions();
    }

    Engine::UParticleModuleSizeMultiplyLife::~UParticleModuleSizeMultiplyLife() = default;

    void Engine::UParticleModuleSizeMultiplyLife::Spawn(Engine::FParticleEmitterInstance& Owner, Int32, float, Engine::FBaseParticle& Particle) const {
        Apply(Owner, Particle);
    }

    void Engine::UParticleModuleSizeMultiplyLife::Update(Engine::FParticleEmitterInstance& Owner, Int32, float) const {
        Engine::ParticleHelper::ForEachParticle(Owner, [this, &Owner](Engine::FBaseParticle& Particle, Uint8*) {
            Apply(Owner, Particle);
        });
    }

    void Engine::UParticleModuleSizeMultiplyLife::BakeDistributions() {
        mLifeMultiplier.Bake();
    }

    void Engine::UParticleModuleSizeMultiplyLife::Apply(Engine::FParticleEmitterInstance& Owner, Engine::FBaseParticle& Particle) const {
        const FVector Scale{mLifeMultiplier.GetValue(Particle.mRelativeTime, Owner.mRandomStream)};

        if (mMultiplyX) {
            Particle.mSize.x *= Scale.x;
        }

        if (mMultiplyY) {
            Particle.mSize.y *= Scale.y;
        }

        if (mMultiplyZ) {
            Particle.mSize.z *= Scale.z;
        }
    }

}
