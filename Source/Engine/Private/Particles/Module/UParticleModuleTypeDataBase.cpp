#include "Engine/Particles/Module/UParticleModuleTypeDataBase.h"
#include "Engine/Particles/FParticleEmitterInstance.h"

namespace Engine {

    Engine::UParticleModuleTypeDataBase::UParticleModuleTypeDataBase() = default;

    Engine::UParticleModuleTypeDataBase::~UParticleModuleTypeDataBase() = default;

    Engine::EParticleModuleType Engine::UParticleModuleTypeDataBase::GetModuleType() const {
        return Engine::EParticleModuleType::TypeData;
    }

    std::unique_ptr<Engine::FParticleEmitterInstance> Engine::UParticleModuleTypeDataBase::CreateInstance(const Engine::UParticleEmitter&, Engine::UParticleSystemComponent&) const {
        return nullptr;
    }

}
