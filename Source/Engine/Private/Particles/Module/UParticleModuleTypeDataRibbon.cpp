#include "Engine/Particles/Module/UParticleModuleTypeDataRibbon.h"
#include "Engine/Particles/FParticleRibbonEmitterInstance.h"

#include <algorithm>

namespace Engine {

    Engine::UParticleModuleTypeDataRibbon::UParticleModuleTypeDataRibbon() {
        SetName("Ribbon Data");
    }

    Engine::UParticleModuleTypeDataRibbon::~UParticleModuleTypeDataRibbon() = default;

    std::unique_ptr<Engine::FParticleEmitterInstance> Engine::UParticleModuleTypeDataRibbon::CreateInstance(const Engine::UParticleEmitter& Emitter, Engine::UParticleSystemComponent& Component) const {
        auto Instance{std::make_unique<Engine::FParticleRibbonEmitterInstance>()};

        Instance->Init(Emitter, Component);

        return Instance;
    }

    Uint32 Engine::UParticleModuleTypeDataRibbon::GetMaxParticleInTrailCount() const {
        return std::max<Uint32>(mMaxParticleInTrailCount, 2);
    }

    Core::FLength Engine::UParticleModuleTypeDataRibbon::GetSpawnDistance() const {
        return mSpawnDistance;
    }

    Core::FLength Engine::UParticleModuleTypeDataRibbon::GetTilingDistance() const {
        return mTilingDistance;
    }

    Core::FLength Engine::UParticleModuleTypeDataRibbon::GetMaxSpawnDistance() const {
        return mMaxSpawnDistance;
    }

    Uint32 Engine::UParticleModuleTypeDataRibbon::GetSheets() const {
        return std::max<Uint32>(mSheets, 1);
    }

    bool Engine::UParticleModuleTypeDataRibbon::IsRenderHead() const {
        return mRenderHead;
    }

    bool Engine::UParticleModuleTypeDataRibbon::IsSpawnInitialParticle() const {
        return mSpawnInitialParticle;
    }

    Core::FLength Engine::UParticleModuleTypeDataRibbon::GetDistanceTessellationStepSize() const {
        return mDistanceTessellationStepSize;
    }

    Uint32 Engine::UParticleModuleTypeDataRibbon::GetMaxTessellationBetweenParticles() const {
        return mMaxTessellationBetweenParticles;
    }

}
