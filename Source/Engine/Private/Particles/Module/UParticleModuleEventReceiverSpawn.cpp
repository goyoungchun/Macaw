#include "Engine/Particles/Module/UParticleModuleEventReceiverSpawn.h"
#include "Engine/Particles/FParticleEmitterInstance.h"
#include "Engine/Component/UParticleSystemComponent.h"

#include <cmath>

namespace Engine {

    Engine::UParticleModuleEventReceiverSpawn::UParticleModuleEventReceiverSpawn() {
        SetName("Event Receiver Spawn");
        mSpawnCount.mConstant = 5.0f;

        // Build 전에도 기본값으로 동작하도록 미리 굽습니다.
        BakeDistributions();
    }

    Engine::UParticleModuleEventReceiverSpawn::~UParticleModuleEventReceiverSpawn() = default;

    void Engine::UParticleModuleEventReceiverSpawn::BakeDistributions() {
        Engine::UParticleModuleEventReceiverBase::BakeDistributions();
        mSpawnCount.Bake();
    }

    void Engine::UParticleModuleEventReceiverSpawn::ProcessEvent(Engine::FParticleEmitterInstance& Owner, const Engine::FParticleEventData& Event) const {
        const Int32 Count{static_cast<Int32>(std::lround(mSpawnCount.GetValue(Owner.mEmitterTime, Owner.mRandomStream)))};

        if (Count <= 0) {
            return;
        }

        const FVector Location{mUseEmitterLocation && Owner.mComponent != nullptr ? Owner.mComponent->GetWorldMatrix().TransformPosition({}) : Event.mLocation};
        const FVector Velocity{mInheritVelocity ? Event.mVelocity * mInheritVelocityScale : FVector{}};

        Owner.ForceSpawn(Count, Location, Velocity);
    }

}
