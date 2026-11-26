#include "Engine/Particles/Module/UParticleModuleTypeDataBeam.h"
#include "Engine/Particles/FParticleBeamEmitterInstance.h"

#include <algorithm>

namespace Engine {

    Engine::UParticleModuleTypeDataBeam::UParticleModuleTypeDataBeam() {
        SetName("Beam Data");
    }

    Engine::UParticleModuleTypeDataBeam::~UParticleModuleTypeDataBeam() = default;

    std::unique_ptr<Engine::FParticleEmitterInstance> Engine::UParticleModuleTypeDataBeam::CreateInstance(const Engine::UParticleEmitter& Emitter, Engine::UParticleSystemComponent& Component) const {
        auto Instance{std::make_unique<Engine::FParticleBeamEmitterInstance>()};

        Instance->Init(Emitter, Component);

        return Instance;
    }

    Engine::EParticleModuleType Engine::UParticleModuleTypeDataBeam::GetModuleType() const {
        return Engine::EParticleModuleType::TypeData;
    }

    Engine::EBeamMethod Engine::UParticleModuleTypeDataBeam::GetBeamMethod() const {
        return mBeamMethod;
    }

    Uint32 Engine::UParticleModuleTypeDataBeam::GetMaxBeamCount() const {
        return std::max<Uint32>(mMaxBeamCount, 1);
    }

    Uint32 Engine::UParticleModuleTypeDataBeam::GetSheets() const {
        return std::max<Uint32>(mSheets, 1);
    }

    Uint32 Engine::UParticleModuleTypeDataBeam::GetSegments() const {
        return std::max<Uint32>(mSegments, 1);
    }

    float Engine::UParticleModuleTypeDataBeam::GetTextureTile() const {
        return mTextureTile;
    }

    Core::FLength Engine::UParticleModuleTypeDataBeam::GetDistance() const {
        return mDistance;
    }

    const FVector& Engine::UParticleModuleTypeDataBeam::GetTargetPoint() const {
        return mTargetPoint;
    }

}
