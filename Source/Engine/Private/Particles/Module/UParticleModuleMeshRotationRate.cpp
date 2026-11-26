#include "Engine/Particles/Module/UParticleModuleMeshRotationRate.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleMeshEmitterInstance.h"

namespace Engine {

    Engine::UParticleModuleMeshRotationRate::UParticleModuleMeshRotationRate() {
        SetName("Init Mesh Rotation Rate");
        mSpawnModule = true;
        mStartRotationRate.mMode = Engine::EDistributionMode::Uniform;
        mStartRotationRate.mMinValue = FVector{-180.0f, -180.0f, -180.0f};
        mStartRotationRate.mMaxValue = FVector{180.0f, 180.0f, 180.0f};
        mStartRotationRate.mLockAxes = false;

        // Build 전에도 기본값으로 동작하도록 미리 굽습니다.
        BakeDistributions();
    }

    Engine::UParticleModuleMeshRotationRate::~UParticleModuleMeshRotationRate() = default;

    void Engine::UParticleModuleMeshRotationRate::Spawn(Engine::FParticleEmitterInstance& Owner, Int32, float, Engine::FBaseParticle& Particle) const {
        auto* Mesh{dynamic_cast<Engine::FParticleMeshEmitterInstance*>(&Owner)};

        if (Mesh == nullptr) {
            return;
        }

        Engine::FMeshRotationPayloadData& Payload{Mesh->GetMeshRotationPayload(Particle)};
        const FVector Rate{mStartRotationRate.GetValue(Owner.mEmitterTime, Owner.mRandomStream)};

        Payload.mRotationRate += Rate;
        Payload.mRotationRateBase += Rate;
    }

    void Engine::UParticleModuleMeshRotationRate::BakeDistributions() {
        mStartRotationRate.Bake();
    }

    Engine::FVectorDistribution& Engine::UParticleModuleMeshRotationRate::GetStartRotationRate() {
        return mStartRotationRate;
    }

}
