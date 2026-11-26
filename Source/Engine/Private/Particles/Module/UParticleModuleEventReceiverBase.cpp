#include "Engine/Particles/Module/UParticleModuleEventReceiverBase.h"

namespace Engine {

    Engine::UParticleModuleEventReceiverBase::UParticleModuleEventReceiverBase() = default;

    Engine::UParticleModuleEventReceiverBase::~UParticleModuleEventReceiverBase() = default;

    Engine::EParticleModuleType Engine::UParticleModuleEventReceiverBase::GetModuleType() const {
        return Engine::EParticleModuleType::Event;
    }

    void Engine::UParticleModuleEventReceiverBase::BakeDistributions() {
        // 이벤트마다 문자열을 비교하지 않도록 FName으로 바꿔 둡니다.
        mCachedEventName = mEventName.empty() ? Core::FName{} : Core::FName{mEventName};
    }

    bool Engine::UParticleModuleEventReceiverBase::WillProcessEvent(const Engine::FParticleEventData& Event) const {
        if (mEventGeneratorType != Engine::EParticleEventType::Any && mEventGeneratorType != Event.mType) {
            return false;
        }

        return mCachedEventName.IsNone() || mCachedEventName == Event.mEventName;
    }

    void Engine::UParticleModuleEventReceiverBase::ProcessEvent(Engine::FParticleEmitterInstance&, const Engine::FParticleEventData&) const {
    }

    Engine::EParticleEventType Engine::UParticleModuleEventReceiverBase::GetEventGeneratorType() const {
        return mEventGeneratorType;
    }

    const Core::FString& Engine::UParticleModuleEventReceiverBase::GetEventName() const {
        return mEventName;
    }

}
