#include "Engine/Particles/Module/UParticleModuleColor.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleEmitterInstance.h"

#include <algorithm>

namespace Engine {

    Engine::UParticleModuleColor::UParticleModuleColor() {
        SetName("Initial Color");
        mSpawnModule = true;
        mStartColor.mConstant = FVector{1.0f, 1.0f, 1.0f};
        mStartAlpha.mConstant = 1.0f;

        // Build 전에도 기본값으로 동작하도록 미리 굽습니다.
        BakeDistributions();
    }

    Engine::UParticleModuleColor::~UParticleModuleColor() = default;

    void Engine::UParticleModuleColor::Spawn(Engine::FParticleEmitterInstance& Owner, Int32, float, Engine::FBaseParticle& Particle) const {
        const FVector Color{mStartColor.GetValue(Owner.mEmitterTime, Owner.mRandomStream)};
        float Alpha{mStartAlpha.GetValue(Owner.mEmitterTime, Owner.mRandomStream)};

        if (mClampAlpha) {
            Alpha = std::clamp(Alpha, 0.0f, 1.0f);
        }

        Particle.mColor = FVector4{Color, Alpha};
        Particle.mBaseColor = Particle.mColor;
    }

    void Engine::UParticleModuleColor::BakeDistributions() {
        mStartColor.Bake();
        mStartAlpha.Bake();
    }

    Engine::FVectorDistribution& Engine::UParticleModuleColor::GetStartColor() {
        return mStartColor;
    }

    Engine::FFloatDistribution& Engine::UParticleModuleColor::GetStartAlpha() {
        return mStartAlpha;
    }

}
