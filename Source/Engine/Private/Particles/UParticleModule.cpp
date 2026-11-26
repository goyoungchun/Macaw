#include "Engine/Particles/UParticleModule.h"

#include "ParticleSerialization.h"

namespace Engine {

    Engine::UParticleModule::UParticleModule() = default;

    Engine::UParticleModule::~UParticleModule() = default;

    void Engine::UParticleModule::Spawn(Engine::FParticleEmitterInstance&, Int32, float, Engine::FBaseParticle&) const {
    }

    void Engine::UParticleModule::Update(Engine::FParticleEmitterInstance&, Int32, float) const {
    }

    Uint32 Engine::UParticleModule::RequiredBytes() const {
        return 0;
    }

    void Engine::UParticleModule::BakeDistributions() {
    }

    Engine::EParticleModuleType Engine::UParticleModule::GetModuleType() const {
        return Engine::EParticleModuleType::General;
    }

    bool Engine::UParticleModule::IsSpawnModule() const {
        return mSpawnModule;
    }

    bool Engine::UParticleModule::IsUpdateModule() const {
        return mUpdateModule;
    }

    bool Engine::UParticleModule::IsEnabled() const {
        return mEnabled;
    }

    void Engine::UParticleModule::SetEnabled(bool Enabled) {
        mEnabled = Enabled;
    }

    void Engine::UParticleModule::Serialize(Core::FArchive& Archive) {
        Engine::ParticleSerialization::SerializeProperties(*this, Archive);

        if (Archive.IsLoading() && !Archive.HasError()) {
            BakeDistributions();
        }
    }

    void Engine::UParticleModule::PostEditChangeProperty(const CoreUObject::FPropertyChangedEvent& Event) {
        CoreUObject::UObject::PostEditChangeProperty(Event);
        BakeDistributions();
    }

}
