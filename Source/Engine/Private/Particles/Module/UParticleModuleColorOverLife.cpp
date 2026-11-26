#include "Engine/Particles/Module/UParticleModuleColorOverLife.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleEmitterInstance.h"
#include "Engine/Particles/ParticleHelper.h"

#include <algorithm>

namespace Engine {

    Engine::UParticleModuleColorOverLife::UParticleModuleColorOverLife() {
        SetName("Color Over Life");
        mSpawnModule = true;
        mUpdateModule = true;
        mColorOverLife.mConstant = FVector{1.0f, 1.0f, 1.0f};
        mAlphaOverLife.mConstant = 1.0f;

        // Build 전에도 기본값으로 동작하도록 미리 굽습니다.
        BakeDistributions();
    }

    Engine::UParticleModuleColorOverLife::~UParticleModuleColorOverLife() = default;

    void Engine::UParticleModuleColorOverLife::Spawn(Engine::FParticleEmitterInstance& Owner, Int32, float, Engine::FBaseParticle& Particle) const {
        Particle.mColor = Evaluate(Owner, Particle.mRelativeTime);
        Particle.mBaseColor = Particle.mColor;
    }

    void Engine::UParticleModuleColorOverLife::Update(Engine::FParticleEmitterInstance& Owner, Int32, float) const {
        Engine::ParticleHelper::ForEachParticle(Owner, [this, &Owner](Engine::FBaseParticle& Particle, Uint8*) {
            Particle.mColor = Evaluate(Owner, Particle.mRelativeTime);
        });
    }

    void Engine::UParticleModuleColorOverLife::BakeDistributions() {
        mColorOverLife.Bake();
        mAlphaOverLife.Bake();
    }

    FVector4 Engine::UParticleModuleColorOverLife::Evaluate(Engine::FParticleEmitterInstance& Owner, float RelativeTime) const {
        const FVector Color{mColorOverLife.GetValue(RelativeTime, Owner.mRandomStream)};
        float Alpha{mAlphaOverLife.GetValue(RelativeTime, Owner.mRandomStream)};

        if (mClampAlpha) {
            Alpha = std::clamp(Alpha, 0.0f, 1.0f);
        }

        return FVector4{Color, Alpha};
    }

}
