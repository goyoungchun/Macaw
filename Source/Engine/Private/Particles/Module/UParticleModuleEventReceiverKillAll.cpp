#include "Engine/Particles/Module/UParticleModuleEventReceiverKillAll.h"
#include "Engine/Particles/FParticleEmitterInstance.h"

namespace Engine {

    Engine::UParticleModuleEventReceiverKillAll::UParticleModuleEventReceiverKillAll() {
        SetName("Event Receiver Kill All");
    }

    Engine::UParticleModuleEventReceiverKillAll::~UParticleModuleEventReceiverKillAll() = default;

    void Engine::UParticleModuleEventReceiverKillAll::ProcessEvent(Engine::FParticleEmitterInstance& Owner, const Engine::FParticleEventData&) const {
        Owner.KillAllParticles();

        if (mStopSpawning) {
            Owner.mSpawningEnabled = false;
        }
    }

}
